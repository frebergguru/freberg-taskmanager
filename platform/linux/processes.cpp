#include "samplers.h"

#include <algorithm>
#include <climits>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>

namespace fg {

ProcessSampler::ProcessSampler(const std::string& root) : m_root(root), m_pageSize(::sysconf(_SC_PAGESIZE)) {}

std::shared_ptr<const ProcessInfo> ProcessSampler::readInfo(int pid, const PidStat& stat)
{
    auto info = std::make_shared<ProcessInfo>();
    info->id = {pid, stat.startTicks};
    info->name = std::string(stat.comm);

    const std::string dir = m_root + "/proc/" + std::to_string(pid);
    struct stat owner;
    if (::stat(dir.c_str(), &owner) == 0)
        info->uid = owner.st_uid;

    std::string text;
    if (readFile(dir + "/cmdline", text) == ReadStatus::Ok) {
        while (!text.empty() && text.back() == '\0')
            text.pop_back();
        for (char& c : text)
            if (c == '\0')
                c = ' ';
        info->commandLine = text;
    }

    char target[PATH_MAX];
    const ssize_t size = ::readlink((dir + "/exe").c_str(), target, sizeof target);
    if (size > 0)
        info->exePath.assign(target, static_cast<std::size_t>(size));

    const ReadStatus cgroupStatus = readFile(dir + "/cgroup", text);
    if (cgroupStatus == ReadStatus::Ok) {
        // cgroup v2 has a single line, "0::/path".
        const std::size_t colons = text.find("::");
        info->cgroup = colons == std::string::npos ? Reading<std::string>::missing(Availability::Unsupported)
                                                   : Reading<std::string>::measured(std::string(trimmed(std::string_view(text).substr(colons + 2))));
    } else {
        info->cgroup = Reading<std::string>::missing(toAvailability(cgroupStatus));
    }
    return info;
}

// Adds up time on all GPU engines from the DRM fdinfo files. A client's descriptors
// can share one context, which is counted once by its drm-client-id.
Reading<double> ProcessSampler::gpuShare(int pid, Known& known, double seconds)
{
    const std::string dir = m_root + "/proc/" + std::to_string(pid) + "/fdinfo/";
    DIR* fds = ::opendir(dir.c_str());
    if (!fds)
        return Reading<double>::missing(Availability::AccessDenied);
    std::vector<std::string> clients;
    std::uint64_t total = 0;
    bool any = false;
    std::string text;
    while (const dirent* entry = ::readdir(fds)) {
        if (entry->d_name[0] == '.' || readFile(dir + entry->d_name, text) != ReadStatus::Ok)
            continue;
        const std::size_t client = text.find("drm-client-id:");
        if (client == std::string::npos)
            continue;
        const std::string id(trimmed(std::string_view(text).substr(client + 14, text.find('\n', client) - client - 14)));
        if (std::find(clients.begin(), clients.end(), id) != clients.end())
            continue;
        clients.push_back(id);
        std::string_view rest = text;
        while (!rest.empty()) {
            const std::size_t end = rest.find('\n');
            const std::string_view line = rest.substr(0, end);
            rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
            if (!line.starts_with("drm-engine-") || line.starts_with("drm-engine-capacity-"))
                continue;
            const std::size_t colon = line.find(':');
            if (colon == std::string_view::npos)
                continue;
            std::string_view value = trimmed(line.substr(colon + 1));
            if (value.ends_with(" ns"))
                value.remove_suffix(3);
            if (const auto ns = parseU64(value)) {
                total += *ns;
                any = true;
            }
        }
    }
    ::closedir(fds);
    if (!any)
        return Reading<double>::measured(0.0); // this process has no GPU contexts at all

    Reading<double> share;
    if (known.haveGpu && seconds > 0 && total >= known.gpuNs)
        share = Reading<double>::measured(std::min(100.0, static_cast<double>(total - known.gpuNs) / (seconds * 1e7)));
    known.gpuNs = total;
    known.haveGpu = true;
    return share;
}

void ProcessSampler::sample(std::int64_t nowMs, std::uint64_t totalCpuTicksDelta, Subsystems wanted, std::vector<ProcessSample>& out)
{
    // TCP byte counts per process, only while that column is on screen.
    std::unordered_map<int, std::uint64_t> networkBytes;
    if (wanted & SubProcessNetwork)
        for (const NetworkConnection& connection : readConnections(m_root))
            if (connection.owner.pid != 0 && connection.sent.known() && connection.received.known())
                networkBytes[connection.owner.pid] += connection.sent.value + connection.received.value;

    DIR* proc = ::opendir((m_root + "/proc").c_str());
    if (!proc)
        return;
    ++m_pass;
    out.reserve(m_known.size() + 16);

    std::string path = m_root + "/proc/";
    const std::size_t baseLength = path.size();
    while (const dirent* entry = ::readdir(proc)) {
        const auto pid = parseI64(entry->d_name);
        if (!pid)
            continue;

        path.resize(baseLength);
        path += entry->d_name;
        const std::size_t pidLength = path.size();
        path += "/stat";
        if (readFile(path, m_buffer) != ReadStatus::Ok)
            continue; // it has already exited
        PidStat stat;
        if (!parsePidStat(m_buffer, stat))
            continue;

        Known& known = m_known[static_cast<int>(*pid)];
        const bool samePerson = known.info && known.info->id.startTicks == stat.startTicks;
        if (!samePerson) {
            // New process, or this PID was reused: forget every previous counter.
            known = {};
            known.info = readInfo(static_cast<int>(*pid), stat);
        }

        ProcessSample sample;
        sample.info = known.info;
        sample.ppid = stat.ppid;
        sample.state = stat.state;
        sample.threads = stat.threads;
        sample.nice = stat.nice;
        sample.rssBytes = Reading<std::uint64_t>::measured(static_cast<std::uint64_t>(stat.rssPages < 0 ? 0 : stat.rssPages) * static_cast<std::uint64_t>(m_pageSize));

        const std::uint64_t ticks = stat.utime + stat.stime;
        if (samePerson && totalCpuTicksDelta > 0 && ticks >= known.cpuTicks)
            sample.cpuPct = Reading<double>::measured(100.0 * static_cast<double>(ticks - known.cpuTicks) / static_cast<double>(totalCpuTicksDelta));
        known.cpuTicks = ticks;

        path.resize(pidLength);
        path += "/io";
        const ReadStatus ioStatus = readFile(path, m_buffer);
        PidIo io;
        if (ioStatus == ReadStatus::Ok && parsePidIo(m_buffer, io)) {
            const double seconds = known.haveIo && nowMs > known.atMs ? static_cast<double>(nowMs - known.atMs) / 1000.0 : 0;
            if (seconds > 0) {
                if (io.readBytes >= known.readBytes)
                    sample.readBps = Reading<double>::measured(static_cast<double>(io.readBytes - known.readBytes) / seconds);
                if (io.writeBytes >= known.writeBytes)
                    sample.writeBps = Reading<double>::measured(static_cast<double>(io.writeBytes - known.writeBytes) / seconds);
            }
            known.readBytes = io.readBytes;
            known.writeBytes = io.writeBytes;
            known.haveIo = true;
        } else if (ioStatus != ReadStatus::Ok) {
            // Other users' processes are normally closed to us.
            sample.readBps = sample.writeBps = Reading<double>::missing(toAvailability(ioStatus));
        } else {
            sample.readBps = sample.writeBps = Reading<double>::missing(Availability::Unsupported);
        }
        const double elapsed = known.atMs != 0 && nowMs > known.atMs ? static_cast<double>(nowMs - known.atMs) / 1000.0 : 0;
        if (wanted & SubProcessGpu)
            sample.gpuPct = gpuShare(static_cast<int>(*pid), known, elapsed);
        if (wanted & SubProcessNetwork) {
            // A process with no TCP sockets is not at 0: we only know about TCP.
            const auto bytes = networkBytes.find(static_cast<int>(*pid));
            if (bytes == networkBytes.end()) {
                sample.networkBps = Reading<double>::missing(Availability::Unsupported);
                known.haveNetwork = false;
            } else {
                if (known.haveNetwork && elapsed > 0)
                    sample.networkBps = Reading<double>::measured(bytes->second >= known.networkBytes ? static_cast<double>(bytes->second - known.networkBytes) / elapsed : 0.0);
                known.networkBytes = bytes->second;
                known.haveNetwork = true;
            }
        }
        known.atMs = nowMs;
        known.seenAt = m_pass;
        out.push_back(std::move(sample));
    }
    ::closedir(proc);

    std::erase_if(m_known, [this](const auto& item) { return item.second.seenAt != m_pass; });
}

std::uint64_t ProcessSampler::currentStartTicks(int pid)
{
    std::string text;
    if (readFile(m_root + "/proc/" + std::to_string(pid) + "/stat", text) != ReadStatus::Ok)
        return 0;
    PidStat stat;
    return parsePidStat(text, stat) ? stat.startTicks : 0;
}

} // namespace fg
