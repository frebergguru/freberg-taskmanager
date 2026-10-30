#include "samplers.h"

#include <algorithm>
#include <climits>
#include <optional>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fg {

namespace {

std::vector<std::string> listDir(const std::string& path)
{
    std::vector<std::string> names;
    if (DIR* dir = ::opendir(path.c_str())) {
        while (const dirent* entry = ::readdir(dir)) {
            const std::string_view name = entry->d_name;
            if (name != "." && name != "..")
                names.emplace_back(name);
        }
        ::closedir(dir);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string readTrimmed(const std::string& path)
{
    std::string text;
    if (readFile(path, text) != ReadStatus::Ok)
        return {};
    return std::string(trimmed(text));
}

template <class Convert>
auto readNumber(const std::string& path, std::string& buffer, Convert convert)
{
    using Value = decltype(convert(std::uint64_t{}));
    const ReadStatus status = readFile(path, buffer);
    if (status != ReadStatus::Ok)
        return Reading<Value>::missing(toAvailability(status));
    const auto value = parseU64(buffer);
    if (!value)
        return Reading<Value>::missing(Availability::Unsupported);
    return Reading<Value>::measured(convert(*value));
}

std::string baseName(const std::string& path)
{
    char target[PATH_MAX];
    const ssize_t size = ::readlink(path.c_str(), target, sizeof target);
    if (size <= 0)
        return {};
    const std::string_view link(target, static_cast<std::size_t>(size));
    const std::size_t slash = link.rfind('/');
    return std::string(slash == std::string_view::npos ? link : link.substr(slash + 1));
}

} // namespace

GpuSampler::GpuSampler(const std::string& root) : m_root(root)
{
    const std::string drm = root + "/sys/class/drm/";
    for (const std::string& name : listDir(drm)) {
        // card0 and card1 are devices; card1-DP-1 and the like are their outputs.
        if (!name.starts_with("card") || name.find('-') != std::string::npos)
            continue;
        Card card;
        card.name = name;
        card.device = drm + name + "/device";
        card.driver = baseName(card.device + "/driver");
        for (const std::string& hwmon : listDir(card.device + "/hwmon")) {
            card.hwmon = card.device + "/hwmon/" + hwmon;
            break;
        }
        card.gtFreq = drm + name + "/gt_act_freq_mhz";
        m_cards.push_back(std::move(card));
    }
}

void GpuSampler::sample(std::vector<GpuSample>& out)
{
    const auto toDouble = [](std::uint64_t v) { return static_cast<double>(v); };
    const auto same = [](std::uint64_t v) { return v; };

    for (const Card& card : m_cards) {
        GpuSample gpu;
        gpu.card = card.name;
        gpu.driver = card.driver;
        gpu.busyPct = readNumber(card.device + "/gpu_busy_percent", m_buffer, toDouble);
        gpu.vramUsed = readNumber(card.device + "/mem_info_vram_used", m_buffer, same);
        gpu.vramTotal = readNumber(card.device + "/mem_info_vram_total", m_buffer, same);

        gpu.freqMHz = readNumber(card.gtFreq, m_buffer, toDouble);
        if (!card.hwmon.empty()) {
            gpu.tempC = readNumber(card.hwmon + "/temp1_input", m_buffer, [](std::uint64_t v) { return static_cast<double>(v) / 1000.0; });
            const auto microwatts = [](std::uint64_t v) { return static_cast<double>(v) / 1000000.0; };
            gpu.powerW = readNumber(card.hwmon + "/power1_average", m_buffer, microwatts);
            if (!gpu.powerW.known())
                gpu.powerW = readNumber(card.hwmon + "/power1_input", m_buffer, microwatts);
            if (!gpu.freqMHz.known())
                gpu.freqMHz = readNumber(card.hwmon + "/freq1_input", m_buffer, [](std::uint64_t v) { return static_cast<double>(v) / 1000000.0; });
        } else {
            gpu.tempC = gpu.powerW = Reading<double>::missing(Availability::Unsupported);
        }
        out.push_back(std::move(gpu));
    }
}

ThermalSampler::ThermalSampler(const std::string& root) : m_root(root)
{
    constexpr double kHighestRealLimitC = 500.0;
    const auto unsupported = Reading<double>::missing(Availability::Unsupported);
    const auto milliToC = [](std::string_view text) -> std::optional<double> {
        const auto value = parseI64(text);
        return value ? std::optional<double>(static_cast<double>(*value) / 1000.0) : std::nullopt;
    };

    std::vector<std::string> chips;
    const std::string base = root + "/sys/class/hwmon/";
    for (const std::string& hwmon : listDir(base)) {
        const std::string dir = base + hwmon + "/";
        const std::string chip = readTrimmed(dir + "name");
        chips.push_back(chip);
        for (const std::string& file : listDir(dir)) {
            if (file.starts_with("temp") && file.ends_with("_input")) {
                const std::string stem = file.substr(0, file.size() - 6);
                std::string label = readTrimmed(dir + stem + "_label");
                if (label.empty())
                    label = stem;
                const bool cpu = chip == "k10temp" || chip == "coretemp" || chip == "zenpower" || chip == "cpu_thermal";
                // The lower of the two limits is the one reached first. Chips say they have none
                // with 0 or a placeholder, such as NVMe's 65261.85 degrees.
                Reading<double> limit = unsupported;
                for (const char* kind : {"_crit", "_max"})
                    if (const auto value = milliToC(readTrimmed(dir + stem + kind)); value && *value > 0 && *value < kHighestRealLimitC)
                        if (!limit.known() || *value < limit.value)
                            limit = Reading<double>::measured(*value);
                m_sensors.push_back({chip, std::move(label), CachedFile(dir + file), cpu, limit});
            } else if (file.starts_with("fan") && file.ends_with("_input")) {
                const std::string stem = file.substr(0, file.size() - 6);
                std::string label = readTrimmed(dir + stem + "_label");
                if (label.empty())
                    label = stem;
                const std::string duty = dir + "pwm" + stem.substr(3);
                struct stat info;
                m_fans.push_back({chip, std::move(label), CachedFile(dir + file), CachedFile(duty), ::stat(duty.c_str(), &info) == 0});
            }
        }
    }

    // Boards with no hwmon driver, such as many Arm systems, report through these.
    const std::string zones = root + "/sys/class/thermal/";
    for (const std::string& zone : listDir(zones)) {
        if (!zone.starts_with("thermal_zone"))
            continue;
        const std::string dir = zones + zone + "/";
        const std::string type = readTrimmed(dir + "type");
        if (std::find(chips.begin(), chips.end(), type) != chips.end())
            continue; // the same sensor already came in through hwmon
        const bool cpu = type == "x86_pkg_temp" || type == "cpu-thermal" || type == "cpu_thermal" || type == "soc_thermal" || type == "cpu";
        Reading<double> limit = unsupported;
        for (const std::string& file : listDir(dir))
            if (file.starts_with("trip_point_") && file.ends_with("_type") && readTrimmed(dir + file) == "critical")
                if (const auto value = milliToC(readTrimmed(dir + file.substr(0, file.size() - 5) + "_temp")); value && *value > 0 && *value < kHighestRealLimitC)
                    limit = Reading<double>::measured(*value);
        m_sensors.push_back({"thermal zone", type.empty() ? zone : type, CachedFile(dir + "temp"), cpu, limit});
    }

    const std::string cpuDir = root + "/sys/devices/system/cpu/";
    for (const std::string& name : listDir(cpuDir)) {
        const std::string path = cpuDir + name + "/thermal_throttle/package_throttle_count";
        struct stat info;
        if (name.starts_with("cpu") && ::stat(path.c_str(), &info) == 0)
            m_throttle.push_back(path);
    }
}

void ThermalSampler::sample(ThermalSample& out)
{
    double hotspot = 0;
    bool haveHotspot = false;
    out.sensors.reserve(m_sensors.size());
    for (Sensor& sensor : m_sensors) {
        ThermalSensor reading{sensor.chip, sensor.label, {}, sensor.limitC};
        const ReadStatus status = sensor.input.read(m_buffer);
        const auto milli = status == ReadStatus::Ok ? parseI64(m_buffer) : std::nullopt;
        if (milli) {
            reading.tempC = Reading<double>::measured(static_cast<double>(*milli) / 1000.0);
            if (sensor.cpu) {
                hotspot = haveHotspot ? std::max(hotspot, reading.tempC.value) : reading.tempC.value;
                haveHotspot = true;
            }
        } else {
            reading.tempC = Reading<double>::missing(toAvailability(status));
        }
        out.sensors.push_back(std::move(reading));
    }
    out.fans.reserve(m_fans.size());
    for (Fan& fan : m_fans) {
        FanSample reading{fan.chip, fan.label, {}, Reading<double>::missing(Availability::Unsupported)};
        const ReadStatus status = fan.input.read(m_buffer);
        const auto rpm = status == ReadStatus::Ok ? parseU64(m_buffer) : std::nullopt;
        reading.rpm = rpm ? Reading<double>::measured(static_cast<double>(*rpm)) : Reading<double>::missing(toAvailability(status));
        if (fan.hasDuty) {
            const ReadStatus dutyStatus = fan.duty.read(m_buffer);
            const auto duty = dutyStatus == ReadStatus::Ok ? parseU64(m_buffer) : std::nullopt;
            reading.dutyPct = duty ? Reading<double>::measured(static_cast<double>(*duty) * 100.0 / 255.0) : Reading<double>::missing(toAvailability(dutyStatus));
        }
        out.fans.push_back(std::move(reading));
    }

    out.cpuHotspotC = haveHotspot ? Reading<double>::measured(hotspot) : Reading<double>::missing(Availability::Unsupported);

    if (m_throttle.empty()) {
        out.throttleEvents = Reading<std::uint64_t>::missing(Availability::Unsupported);
    } else {
        std::uint64_t events = 0;
        for (const std::string& path : m_throttle)
            if (readFile(path, m_buffer) == ReadStatus::Ok)
                events += parseU64(m_buffer).value_or(0);
        out.throttleEvents = Reading<std::uint64_t>::measured(events);
    }
}

PowerSampler::PowerSampler(const std::string& root) : m_root(root)
{
    const std::string base = root + "/sys/class/powercap/";
    for (const std::string& name : listDir(base)) {
        // intel-rapl:0 is a package domain; intel-rapl:0:0 is a part of it.
        if (name.starts_with("intel-rapl:") && name.find(':', 11) == std::string::npos) {
            m_raplDomain = base + name;
            break;
        }
    }
    if (!m_raplDomain.empty())
        m_raplRange = parseU64(readTrimmed(m_raplDomain + "/max_energy_range_uj")).value_or(0);
}

void PowerSampler::sample(std::int64_t nowMs, PowerSample& out)
{
    if (m_raplDomain.empty()) {
        out.cpuPackageW = Reading<double>::missing(Availability::Unsupported);
    } else {
        const ReadStatus status = readFile(m_raplDomain + "/energy_uj", m_buffer);
        const auto energy = status == ReadStatus::Ok ? parseU64(m_buffer) : std::nullopt;
        if (!energy) {
            // Root-only since kernel 5.10, unless the user has granted group access.
            out.cpuPackageW = Reading<double>::missing(toAvailability(status));
        } else {
            if (m_previousMs != 0 && nowMs > m_previousMs) {
                std::uint64_t delta = *energy >= m_previousEnergy ? *energy - m_previousEnergy : 0;
                if (*energy < m_previousEnergy && m_raplRange != 0)
                    delta = m_raplRange - m_previousEnergy + *energy; // the counter wrapped
                out.cpuPackageW = Reading<double>::measured(static_cast<double>(delta) / (static_cast<double>(nowMs - m_previousMs) * 1000.0));
            }
            m_previousEnergy = *energy;
            m_previousMs = nowMs;
        }
    }

    const std::string supplies = m_root + "/sys/class/power_supply/";
    bool haveAc = false, acOnline = false;
    for (const std::string& name : listDir(supplies)) {
        const std::string dir = supplies + name + "/";
        const std::string type = readTrimmed(dir + "type");
        if (type == "Mains") {
            const std::string online = readTrimmed(dir + "online");
            if (!online.empty()) {
                haveAc = true;
                acOnline = acOnline || online == "1";
            }
        } else if (type == "Battery" && readTrimmed(dir + "scope") != "Device") {
            BatterySample battery;
            battery.name = name;
            battery.status = readTrimmed(dir + "status");
            battery.percent = readNumber(dir + "capacity", m_buffer, [](std::uint64_t v) { return static_cast<double>(v); });
            battery.powerW = readNumber(dir + "power_now", m_buffer, [](std::uint64_t v) { return static_cast<double>(v) / 1000000.0; });
            if (!battery.powerW.known()) {
                const auto current = parseU64(readTrimmed(dir + "current_now"));
                const auto voltage = parseU64(readTrimmed(dir + "voltage_now"));
                if (current && voltage)
                    battery.powerW = Reading<double>::measured(static_cast<double>(*current) * static_cast<double>(*voltage) / 1e12);
            }
            out.batteries.push_back(std::move(battery));
        }
    }
    out.onAc = haveAc ? Reading<bool>::measured(acOnline) : Reading<bool>::missing(Availability::Unsupported);

    const auto text = [this](const std::string& path) {
        const ReadStatus status = readFile(path, m_buffer);
        return status == ReadStatus::Ok ? Reading<std::string>::measured(std::string(trimmed(m_buffer)))
                                        : Reading<std::string>::missing(toAvailability(status));
    };
    const std::string cpu0 = m_root + "/sys/devices/system/cpu/cpu0/cpufreq/";
    out.governor = text(cpu0 + "scaling_governor");
    out.energyPreference = text(cpu0 + "energy_performance_preference");
    out.platformProfile = text(m_root + "/sys/firmware/acpi/platform_profile");
}

} // namespace fg
