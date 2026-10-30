// Read-once details for the Power & Freq and Drivers pages.
#include "samplers.h"

#include <algorithm>
#include <climits>
#include <dirent.h>
#include <map>
#include <unistd.h>

namespace fg {

namespace {

std::vector<std::string> entries(const std::string& path)
{
    std::vector<std::string> names;
    if (DIR* dir = ::opendir(path.c_str())) {
        while (const dirent* entry = ::readdir(dir))
            if (entry->d_name[0] != '.')
                names.emplace_back(entry->d_name);
        ::closedir(dir);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string linkTail(const std::string& path)
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

PowerDetails readPowerDetails(const std::string& root)
{
    PowerDetails details;
    const std::string cpuDir = root + "/sys/devices/system/cpu/";
    std::string text;

    std::map<std::string, std::uint64_t> idle;
    for (const std::string& name : entries(cpuDir)) {
        if (!name.starts_with("cpu") || !parseU64(std::string_view(name).substr(3)))
            continue;
        const std::string statesDir = cpuDir + name + "/cpuidle/";
        for (const std::string& state : entries(statesDir)) {
            if (!state.starts_with("state"))
                continue;
            std::string label, time;
            if (readFile(statesDir + state + "/name", label) == ReadStatus::Ok && readFile(statesDir + state + "/time", time) == ReadStatus::Ok)
                idle[std::string(trimmed(label))] += parseU64(time).value_or(0);
        }
    }
    for (const auto& [name, time] : idle)
        details.idleStates.push_back({name, time});

    const std::string cpu0 = cpuDir + "cpu0/cpufreq/";
    const ReadStatus governors = readFile(cpu0 + "scaling_available_governors", text);
    if (governors == ReadStatus::Ok) {
        std::vector<std::string> list;
        std::string_view rest = trimmed(text);
        while (!rest.empty()) {
            const std::size_t gap = rest.find(' ');
            list.emplace_back(rest.substr(0, gap));
            rest = gap == std::string_view::npos ? std::string_view{} : trimmed(rest.substr(gap));
        }
        details.availableGovernors = Reading<std::vector<std::string>>::measured(std::move(list));
    } else {
        details.availableGovernors = Reading<std::vector<std::string>>::missing(toAvailability(governors));
    }

    const auto mhz = [&text](const std::string& path) {
        const ReadStatus status = readFile(path, text);
        const auto value = status == ReadStatus::Ok ? parseU64(text) : std::nullopt;
        return value ? Reading<double>::measured(static_cast<double>(*value) / 1000.0) : Reading<double>::missing(toAvailability(status));
    };
    details.minFreqMHz = mhz(cpu0 + "cpuinfo_min_freq");
    details.maxFreqMHz = mhz(cpu0 + "cpuinfo_max_freq");

    const ReadStatus boost = readFile(cpuDir + "cpufreq/boost", text);
    details.boost = boost == ReadStatus::Ok ? Reading<std::string>::measured(std::string(trimmed(text))) : Reading<std::string>::missing(toAvailability(boost));
    return details;
}

std::vector<KernelModule> readKernelModules(const std::string& root)
{
    std::vector<KernelModule> modules;
    std::string text;
    if (readFile(root + "/proc/modules", text) != ReadStatus::Ok)
        return modules;

    // Which module drives which device.
    std::map<std::string, std::vector<std::string>> devices;
    for (const std::string& bus : entries(root + "/sys/bus/")) {
        const std::string busDevices = root + "/sys/bus/" + bus + "/devices/";
        for (const std::string& device : entries(busDevices)) {
            const std::string module = linkTail(busDevices + device + "/driver/module");
            if (!module.empty())
                devices[module].push_back(bus + ":" + device);
        }
    }

    // name size refcount usedby state address
    std::string_view rest = text;
    while (!rest.empty()) {
        const std::size_t end = rest.find('\n');
        std::string_view line = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
        std::string_view fields[5];
        int count = 0;
        while (count < 5 && !line.empty()) {
            line = trimmed(line);
            const std::size_t gap = line.find(' ');
            fields[count++] = line.substr(0, gap);
            line = gap == std::string_view::npos ? std::string_view{} : line.substr(gap);
        }
        if (count < 5)
            continue;
        KernelModule module;
        module.name = std::string(fields[0]);
        module.sizeBytes = parseU64(fields[1]).value_or(0);
        module.useCount = static_cast<int>(parseI64(fields[2]).value_or(0));
        module.usedBy = fields[3] == "-" ? std::string() : std::string(fields[3]);
        if (!module.usedBy.empty() && module.usedBy.back() == ',')
            module.usedBy.pop_back();
        module.state = std::string(fields[4]);
        if (const auto found = devices.find(module.name); found != devices.end())
            module.devices = found->second;
        modules.push_back(std::move(module));
    }
    return modules;
}

} // namespace fg
