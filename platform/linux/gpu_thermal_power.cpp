#include "samplers.h"

#include <algorithm>
#include <climits>
#include <optional>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fg {

namespace {

constexpr std::uint64_t kSupplyRescanPasses = 30; // a battery or charger can come and go

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
auto readNumber(CachedFile& file, std::string& buffer, Convert convert)
{
    using Value = decltype(convert(std::uint64_t{}));
    const ReadStatus status = file.read(buffer);
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
        const std::string device = drm + name + "/device";
        card.driver = baseName(device + "/driver");
        card.busy = CachedFile(device + "/gpu_busy_percent");
        card.vramUsed = CachedFile(device + "/mem_info_vram_used");
        card.vramTotal = CachedFile(device + "/mem_info_vram_total");
        card.gtFreq = CachedFile(drm + name + "/gt_act_freq_mhz");
        for (const std::string& hwmon : listDir(device + "/hwmon")) {
            const std::string dir = device + "/hwmon/" + hwmon;
            card.hasHwmon = true;
            card.temp = CachedFile(dir + "/temp1_input");
            card.powerAverage = CachedFile(dir + "/power1_average");
            card.powerInput = CachedFile(dir + "/power1_input");
            card.hwmonFreq = CachedFile(dir + "/freq1_input");
            break;
        }
        m_cards.push_back(std::move(card));
    }
}

void GpuSampler::sample(std::vector<GpuSample>& out)
{
    const auto toDouble = [](std::uint64_t v) { return static_cast<double>(v); };
    const auto same = [](std::uint64_t v) { return v; };

    for (Card& card : m_cards) {
        GpuSample gpu;
        gpu.card = card.name;
        gpu.driver = card.driver;
        gpu.busyPct = readNumber(card.busy, m_buffer, toDouble);
        gpu.vramUsed = readNumber(card.vramUsed, m_buffer, same);
        gpu.vramTotal = readNumber(card.vramTotal, m_buffer, same);

        gpu.freqMHz = readNumber(card.gtFreq, m_buffer, toDouble);
        if (card.hasHwmon) {
            gpu.tempC = readNumber(card.temp, m_buffer, [](std::uint64_t v) { return static_cast<double>(v) / 1000.0; });
            const auto microwatts = [](std::uint64_t v) { return static_cast<double>(v) / 1000000.0; };
            gpu.powerW = readNumber(card.powerAverage, m_buffer, microwatts);
            if (!gpu.powerW.known())
                gpu.powerW = readNumber(card.powerInput, m_buffer, microwatts);
            if (!gpu.freqMHz.known())
                gpu.freqMHz = readNumber(card.hwmonFreq, m_buffer, [](std::uint64_t v) { return static_cast<double>(v) / 1000000.0; });
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

    // Every processor keeps its own copy of its package's count, so one per package is the real total.
    const std::string cpuDir = root + "/sys/devices/system/cpu/";
    std::vector<std::string> packages;
    for (const std::string& name : listDir(cpuDir)) {
        const std::string path = cpuDir + name + "/thermal_throttle/package_throttle_count";
        struct stat info;
        if (!name.starts_with("cpu") || ::stat(path.c_str(), &info) != 0)
            continue;
        const std::string package = readTrimmed(cpuDir + name + "/topology/physical_package_id");
        if (!package.empty()) {
            if (std::find(packages.begin(), packages.end(), package) != packages.end())
                continue;
            packages.push_back(package);
        }
        m_throttle.emplace_back(path);
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
        bool anyRead = false;
        Availability why = Availability::Unsupported;
        for (CachedFile& file : m_throttle) {
            const ReadStatus status = file.read(m_buffer);
            const auto count = status == ReadStatus::Ok ? parseU64(m_buffer) : std::nullopt;
            if (count) {
                events += *count;
                anyRead = true;
            } else if (status != ReadStatus::Ok) {
                why = toAvailability(status);
            }
        }
        out.throttleEvents = anyRead ? Reading<std::uint64_t>::measured(events) : Reading<std::uint64_t>::missing(why);
    }
}

PowerSampler::PowerSampler(const std::string& root)
    : m_root(root),
      m_governor(root + "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"),
      m_energyPreference(root + "/sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference"),
      m_platformProfile(root + "/sys/firmware/acpi/platform_profile")
{
    const std::string base = root + "/sys/class/powercap/";
    for (const std::string& name : listDir(base)) {
        // intel-rapl:0 is a package domain; intel-rapl:0:0 is a part of it.
        if (name.starts_with("intel-rapl:") && name.find(':', 11) == std::string::npos) {
            RaplDomain domain;
            domain.energy = CachedFile(base + name + "/energy_uj");
            domain.range = parseU64(readTrimmed(base + name + "/max_energy_range_uj")).value_or(0);
            m_rapl.push_back(std::move(domain));
        }
    }
}

void PowerSampler::findSupplies()
{
    m_supplies.clear();
    const std::string supplies = m_root + "/sys/class/power_supply/";
    for (const std::string& name : listDir(supplies)) {
        const std::string dir = supplies + name + "/";
        const std::string type = readTrimmed(dir + "type");
        if (type != "Mains" && (type != "Battery" || readTrimmed(dir + "scope") == "Device"))
            continue;
        Supply supply;
        supply.name = name;
        supply.isMains = type == "Mains";
        supply.online = CachedFile(dir + "online");
        supply.status = CachedFile(dir + "status");
        supply.capacity = CachedFile(dir + "capacity");
        supply.powerNow = CachedFile(dir + "power_now");
        supply.currentNow = CachedFile(dir + "current_now");
        supply.voltageNow = CachedFile(dir + "voltage_now");
        m_supplies.push_back(std::move(supply));
    }
}

std::string PowerSampler::readText(CachedFile& file)
{
    return file.read(m_buffer) == ReadStatus::Ok ? std::string(trimmed(m_buffer)) : std::string();
}

void PowerSampler::sample(std::int64_t nowMs, PowerSample& out)
{
    if (m_rapl.empty()) {
        out.cpuPackageW = Reading<double>::missing(Availability::Unsupported);
    } else {
        std::uint64_t delta = 0;
        bool haveDelta = m_previousMs != 0 && nowMs > m_previousMs;
        bool readAll = true;
        Availability why = Availability::Unsupported;
        for (RaplDomain& domain : m_rapl) {
            const ReadStatus status = domain.energy.read(m_buffer);
            const auto energy = status == ReadStatus::Ok ? parseU64(m_buffer) : std::nullopt;
            if (!energy) {
                // Root-only since kernel 5.10, unless the user has granted group access.
                readAll = false;
                why = toAvailability(status);
                domain.havePrevious = false;
                continue;
            }
            if (!domain.havePrevious)
                haveDelta = false;
            else if (*energy >= domain.previous)
                delta += *energy - domain.previous;
            else if (domain.range != 0)
                delta += domain.range - domain.previous + *energy; // the counter wrapped
            else
                haveDelta = false; // it wrapped, and we don't know where: a gap, not 0 W
            domain.previous = *energy;
            domain.havePrevious = true;
        }
        if (!readAll) {
            out.cpuPackageW = Reading<double>::missing(why);
        } else {
            if (haveDelta)
                out.cpuPackageW = Reading<double>::measured(static_cast<double>(delta) / (static_cast<double>(nowMs - m_previousMs) * 1000.0));
            m_previousMs = nowMs;
        }
    }

    if (m_passes++ % kSupplyRescanPasses == 0)
        findSupplies();
    bool haveAc = false, acOnline = false;
    for (Supply& supply : m_supplies) {
        if (supply.isMains) {
            const std::string online = readText(supply.online);
            if (!online.empty()) {
                haveAc = true;
                acOnline = acOnline || online == "1";
            }
            continue;
        }
        BatterySample battery;
        battery.name = supply.name;
        battery.status = readText(supply.status);
        battery.percent = readNumber(supply.capacity, m_buffer, [](std::uint64_t v) { return static_cast<double>(v); });
        battery.powerW = readNumber(supply.powerNow, m_buffer, [](std::uint64_t v) { return static_cast<double>(v) / 1000000.0; });
        if (!battery.powerW.known()) {
            const auto current = parseU64(readText(supply.currentNow));
            const auto voltage = parseU64(readText(supply.voltageNow));
            if (current && voltage)
                battery.powerW = Reading<double>::estimated(static_cast<double>(*current) * static_cast<double>(*voltage) / 1e12); // worked out from two separate reads
        }
        out.batteries.push_back(std::move(battery));
    }
    out.onAc = haveAc ? Reading<bool>::measured(acOnline) : Reading<bool>::missing(Availability::Unsupported);

    const auto text = [this](CachedFile& file) {
        const ReadStatus status = file.read(m_buffer);
        return status == ReadStatus::Ok ? Reading<std::string>::measured(std::string(trimmed(m_buffer)))
                                        : Reading<std::string>::missing(toAvailability(status));
    };
    out.governor = text(m_governor);
    out.energyPreference = text(m_energyPreference);
    out.platformProfile = text(m_platformProfile);
}

} // namespace fg
