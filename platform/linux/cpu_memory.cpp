#include "samplers.h"

#include <algorithm>
#include <dirent.h>
#include <set>

namespace fg {

CpuSampler::CpuSampler(const std::string& root) : m_root(root), m_stat(root + "/proc/stat") {}

void CpuSampler::sample(CpuSample& out)
{
    const ReadStatus status = m_stat.read(m_buffer);
    if (status != ReadStatus::Ok) {
        out.usagePct = Reading<double>::missing(toAvailability(status));
        return;
    }
    parseCpuStat(m_buffer, m_current);
    if (m_current.empty()) {
        out.usagePct = Reading<double>::missing(Availability::Unsupported);
        return;
    }

    const std::size_t logical = m_current.size() - 1;
    out.cores.resize(logical);
    const bool havePrevious = m_previous.size() == m_current.size();

    if (havePrevious) {
        if (const auto busy = cpuBusyPercent(m_previous[0], m_current[0]))
            out.usagePct = Reading<double>::measured(*busy);
        for (std::size_t cpu = 0; cpu < logical; ++cpu)
            if (const auto busy = cpuBusyPercent(m_previous[cpu + 1], m_current[cpu + 1]))
                out.cores[cpu].usagePct = Reading<double>::measured(*busy);
    }
    m_previous.swap(m_current);

    if (m_freq.size() != logical) {
        m_freq.clear();
        m_freq.reserve(logical);
        for (std::size_t cpu = 0; cpu < logical; ++cpu)
            m_freq.emplace_back(m_root + "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpufreq/scaling_cur_freq");
    }
    double sum = 0;
    int counted = 0;
    Availability why = Availability::Unsupported;
    for (std::size_t cpu = 0; cpu < logical; ++cpu) {
        const ReadStatus freqStatus = m_freq[cpu].read(m_buffer);
        const auto kHz = freqStatus == ReadStatus::Ok ? parseU64(m_buffer) : std::nullopt;
        if (kHz) {
            out.cores[cpu].freqMHz = Reading<double>::measured(static_cast<double>(*kHz) / 1000.0);
            sum += static_cast<double>(*kHz) / 1000.0;
            ++counted;
        } else {
            why = toAvailability(freqStatus);
            out.cores[cpu].freqMHz = Reading<double>::missing(why);
        }
    }
    out.freqMHz = counted ? Reading<double>::measured(sum / counted) : Reading<double>::missing(why);
}

CpuTopology CpuSampler::readTopology() const
{
    CpuTopology topology;
    std::string text;

    if (readFile(m_root + "/proc/cpuinfo", text) == ReadStatus::Ok) {
        const std::size_t key = text.find("model name");
        if (key != std::string::npos) {
            const std::size_t colon = text.find(':', key);
            const std::size_t end = text.find('\n', key);
            if (colon != std::string::npos && end != std::string::npos && colon < end)
                topology.model = std::string(trimmed(std::string_view(text).substr(colon + 1, end - colon - 1)));
        }
    }

    const std::string cpuDir = m_root + "/sys/devices/system/cpu/";
    if (readFile(cpuDir + "online", text) == ReadStatus::Ok)
        topology.logical = static_cast<int>(parseCpuList(text).size());
    if (topology.logical == 0) {
        std::string stat;
        std::vector<CpuTimes> times;
        if (readFile(m_root + "/proc/stat", stat) == ReadStatus::Ok) {
            parseCpuStat(stat, times);
            topology.logical = times.empty() ? 0 : static_cast<int>(times.size()) - 1;
        }
    }

    topology.coreNode.assign(topology.logical, 0);
    topology.efficiency.assign(topology.logical, false);

    std::set<std::pair<std::string, std::string>> physical;
    for (int cpu = 0; cpu < topology.logical; ++cpu) {
        const std::string dir = cpuDir + "cpu" + std::to_string(cpu) + "/topology/";
        std::string package, core;
        if (readFile(dir + "physical_package_id", package) == ReadStatus::Ok && readFile(dir + "core_id", core) == ReadStatus::Ok)
            physical.emplace(package, core);
    }
    topology.physical = static_cast<int>(physical.size());

    // Intel hybrid processors list their efficiency cores here.
    if (readFile(m_root + "/sys/devices/cpu_atom/cpus", text) == ReadStatus::Ok) {
        for (const int cpu : parseCpuList(text))
            if (cpu >= 0 && cpu < topology.logical) {
                topology.efficiency[cpu] = true;
                topology.hybrid = true;
            }
    } else {
        // Elsewhere (Arm and others) they show as a lower capacity.
        std::vector<std::uint64_t> capacity(topology.logical, 0);
        std::uint64_t highest = 0;
        for (int cpu = 0; cpu < topology.logical; ++cpu)
            if (readFile(cpuDir + "cpu" + std::to_string(cpu) + "/cpu_capacity", text) == ReadStatus::Ok) {
                capacity[cpu] = parseU64(text).value_or(0);
                highest = std::max(highest, capacity[cpu]);
            }
        for (int cpu = 0; cpu < topology.logical; ++cpu)
            if (capacity[cpu] != 0 && capacity[cpu] * 10 < highest * 8) {
                topology.efficiency[cpu] = true;
                topology.hybrid = true;
            }
    }

    if (DIR* nodes = ::opendir((m_root + "/sys/devices/system/node").c_str())) {
        while (const dirent* entry = ::readdir(nodes)) {
            const std::string_view name = entry->d_name;
            if (!name.starts_with("node"))
                continue;
            const auto node = parseI64(name.substr(4));
            if (!node)
                continue;
            ++topology.numaNodes;
            if (readFile(m_root + "/sys/devices/system/node/" + std::string(name) + "/cpulist", text) == ReadStatus::Ok)
                for (const int cpu : parseCpuList(text))
                    if (cpu >= 0 && cpu < topology.logical)
                        topology.coreNode[cpu] = static_cast<int>(*node);
        }
        ::closedir(nodes);
    }
    topology.numaNodes = std::max(topology.numaNodes, 1);

    const ReadStatus maxStatus = readFile(cpuDir + "cpu0/cpufreq/cpuinfo_max_freq", text);
    const auto maxKHz = maxStatus == ReadStatus::Ok ? parseU64(text) : std::nullopt;
    topology.maxFreqMHz = maxKHz ? Reading<double>::measured(static_cast<double>(*maxKHz) / 1000.0)
                                 : Reading<double>::missing(toAvailability(maxStatus));
    return topology;
}

MemorySampler::MemorySampler(const std::string& root) : m_root(root), m_meminfo(root + "/proc/meminfo") {}

void MemorySampler::sample(MemorySample& out)
{
    const ReadStatus status = m_meminfo.read(m_buffer);
    if (status != ReadStatus::Ok) {
        const auto why = Reading<std::uint64_t>::missing(toAvailability(status));
        out.total = out.used = out.available = out.free = out.cached = out.buffers = out.shared = why;
        out.swapTotal = out.swapUsed = out.zswapStored = out.zswapCompressed = why;
        out.zramOriginal = out.zramCompressed = Reading<std::uint64_t>::missing(Availability::Unsupported);
        return;
    }

    const auto field = [this](std::string_view key) {
        const auto value = meminfoBytes(m_buffer, key);
        return value ? Reading<std::uint64_t>::measured(*value) : Reading<std::uint64_t>::missing(Availability::Unsupported);
    };
    out.total = field("MemTotal");
    out.available = field("MemAvailable");
    out.free = field("MemFree");
    out.buffers = field("Buffers");
    out.shared = field("Shmem");
    out.swapTotal = field("SwapTotal");
    out.zswapStored = field("Zswapped");
    out.zswapCompressed = field("Zswap");

    const auto cached = meminfoBytes(m_buffer, "Cached");
    const auto reclaimable = meminfoBytes(m_buffer, "SReclaimable");
    out.cached = cached ? Reading<std::uint64_t>::measured(*cached + reclaimable.value_or(0))
                        : Reading<std::uint64_t>::missing(Availability::Unsupported);

    // Used is defined as total minus available: exact arithmetic on two kernel counters.
    if (out.total.known() && out.available.known() && out.total.value >= out.available.value)
        out.used = Reading<std::uint64_t>::measured(out.total.value - out.available.value);
    else
        out.used = Reading<std::uint64_t>::missing(Availability::Unsupported);

    const auto swapFree = meminfoBytes(m_buffer, "SwapFree");
    if (out.swapTotal.known() && swapFree && out.swapTotal.value >= *swapFree)
        out.swapUsed = Reading<std::uint64_t>::measured(out.swapTotal.value - *swapFree);
    else
        out.swapUsed = Reading<std::uint64_t>::missing(Availability::Unsupported);

    const ReadStatus vmstat = readFile(m_root + "/proc/vmstat", m_buffer);
    out.oomKills = Reading<std::uint64_t>::missing(toAvailability(vmstat));
    if (vmstat == ReadStatus::Ok) {
        const std::size_t key = m_buffer.find("oom_kill ");
        if (key != std::string::npos) {
            const std::size_t end = m_buffer.find('\n', key);
            if (const auto kills = parseU64(std::string_view(m_buffer).substr(key + 9, end - key - 9)))
                out.oomKills = Reading<std::uint64_t>::measured(*kills);
        } else {
            out.oomKills = Reading<std::uint64_t>::missing(Availability::Unsupported);
        }
    }

    // zram: mm_stat starts with original size, then compressed size.
    std::uint64_t original = 0, compressed = 0;
    bool anyZram = false;
    if (DIR* blocks = ::opendir((m_root + "/sys/block").c_str())) {
        while (const dirent* entry = ::readdir(blocks)) {
            if (std::string_view(entry->d_name).starts_with("zram")
                && readFile(m_root + "/sys/block/" + entry->d_name + "/mm_stat", m_buffer) == ReadStatus::Ok) {
                std::string_view text = trimmed(m_buffer);
                const std::size_t gap = text.find(' ');
                if (gap == std::string_view::npos)
                    continue;
                const auto first = parseU64(text.substr(0, gap));
                text = trimmed(text.substr(gap));
                const auto second = parseU64(text.substr(0, text.find(' ')));
                if (first && second) {
                    original += *first;
                    compressed += *second;
                    anyZram = true;
                }
            }
        }
        ::closedir(blocks);
    }
    out.zramOriginal = anyZram ? Reading<std::uint64_t>::measured(original) : Reading<std::uint64_t>::missing(Availability::Unsupported);
    out.zramCompressed = anyZram ? Reading<std::uint64_t>::measured(compressed) : Reading<std::uint64_t>::missing(Availability::Unsupported);
}

PressureSampler::PressureSampler(const std::string& root)
    : m_cpu(root + "/proc/pressure/cpu"), m_memory(root + "/proc/pressure/memory"), m_io(root + "/proc/pressure/io")
{
}

void PressureSampler::sample(PressureSample& out)
{
    const auto readLine = [this](CachedFile& file, PressureLine& line) {
        const ReadStatus status = file.read(m_buffer);
        if (status != ReadStatus::Ok) {
            line.some10 = line.full10 = Reading<double>::missing(toAvailability(status));
            return;
        }
        const PressureValues values = parsePressure(m_buffer);
        line.some10 = values.some10 ? Reading<double>::measured(*values.some10) : Reading<double>::missing(Availability::Unsupported);
        line.full10 = values.full10 ? Reading<double>::measured(*values.full10) : Reading<double>::missing(Availability::Unsupported);
    };
    readLine(m_cpu, out.cpu);
    readLine(m_memory, out.memory);
    readLine(m_io, out.io);
}

} // namespace fg
