#include "samplers.h"

#include <sys/stat.h>
#include <sys/statvfs.h>

namespace fg {

namespace {

constexpr std::uint64_t kSectorBytes = 512; // /proc/diskstats always counts in 512-byte units
constexpr std::int64_t kFilesystemRefreshMs = 30000;

bool exists(const std::string& path)
{
    struct stat info;
    return ::stat(path.c_str(), &info) == 0;
}

std::string readTrimmed(const std::string& path)
{
    std::string text;
    if (readFile(path, text) != ReadStatus::Ok)
        return {};
    return std::string(trimmed(text));
}

bool isNetworkOrVirtualFs(std::string_view type)
{
    // statvfs on a dead network mount can block, so those are never asked.
    return type == "nfs" || type == "nfs4" || type == "cifs" || type == "smb3" || type == "sshfs" || type == "fuse.sshfs"
        || type == "tmpfs" || type == "proc" || type == "sysfs" || type == "devtmpfs" || type == "overlay" || type == "squashfs";
}

// /dev/nvme0n1p2 -> nvme0n1, /dev/sda3 -> sda, using sysfs to find the parent.
std::string parentDisk(const std::string& root, std::string_view device)
{
    if (!device.starts_with("/dev/"))
        return {};
    std::string name(device.substr(5));
    if (exists(root + "/sys/block/" + name))
        return name;
    std::string candidate = name;
    while (!candidate.empty()) {
        candidate.pop_back();
        if (exists(root + "/sys/block/" + candidate + "/" + name))
            return candidate;
    }
    return {};
}

} // namespace

DiskSampler::DiskSampler(const std::string& root) : m_root(root), m_diskstats(root + "/proc/diskstats") {}

const DiskSampler::Identity& DiskSampler::identity(const std::string& name)
{
    const auto found = m_identity.find(name);
    if (found != m_identity.end())
        return found->second;
    Identity id;
    const std::string dir = m_root + "/sys/block/" + name;
    // A real disk has a backing device; loop, ram, zram and dm do not count as drives.
    const bool virtualName = name.starts_with("loop") || name.starts_with("ram") || name.starts_with("zram") || name.starts_with("dm-") || name.starts_with("md");
    id.physical = !virtualName && exists(dir + "/device");
    if (id.physical) {
        id.model = readTrimmed(dir + "/device/model");
        id.rotational = readTrimmed(dir + "/queue/rotational") == "1";
        id.removable = readTrimmed(dir + "/removable") == "1";
    }
    return m_identity.emplace(name, std::move(id)).first->second;
}

void DiskSampler::refreshFilesystems()
{
    m_filesystems.clear();
    std::string mounts;
    if (readFile(m_root + "/proc/self/mounts", mounts) != ReadStatus::Ok)
        return;
    std::string_view text = mounts;
    std::unordered_map<std::string, bool> seenDevice;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);

        const std::size_t a = line.find(' ');
        if (a == std::string_view::npos)
            continue;
        const std::size_t b = line.find(' ', a + 1);
        if (b == std::string_view::npos)
            continue;
        const std::size_t c = line.find(' ', b + 1);
        const std::string_view device = line.substr(0, a);
        const std::string mountPoint(line.substr(a + 1, b - a - 1));
        const std::string_view type = line.substr(b + 1, c == std::string_view::npos ? c : c - b - 1);
        if (isNetworkOrVirtualFs(type))
            continue;

        const std::string disk = parentDisk(m_root, device);
        if (disk.empty())
            continue;
        Filesystems& fs = m_filesystems[disk];
        fs.mounts.push_back(mountPoint);
        // A btrfs volume is mounted many times; count its space only once.
        if (seenDevice[std::string(device)])
            continue;
        seenDevice[std::string(device)] = true;
        struct statvfs info;
        if (::statvfs((m_root + mountPoint).c_str(), &info) == 0) {
            fs.total += static_cast<std::uint64_t>(info.f_blocks) * info.f_frsize;
            fs.free += static_cast<std::uint64_t>(info.f_bavail) * info.f_frsize;
        }
    }
}

void DiskSampler::sample(std::int64_t nowMs, std::vector<DiskSample>& out)
{
    const ReadStatus status = m_diskstats.read(m_buffer);
    if (status != ReadStatus::Ok)
        return; // the empty list tells the UI there are no measured drives
    parseDiskstats(m_buffer, m_counters);

    if (m_filesystemsAtMs == 0 || nowMs - m_filesystemsAtMs >= kFilesystemRefreshMs) {
        refreshFilesystems();
        m_filesystemsAtMs = nowMs;
    }

    const double seconds = m_previousMs ? static_cast<double>(nowMs - m_previousMs) / 1000.0 : 0;
    for (const DiskCounters& counters : m_counters) {
        const std::string name(counters.name);
        const Identity& id = identity(name);
        if (!id.physical)
            continue;
        DiskSample disk;
        disk.name = name;
        disk.model = id.model;
        disk.rotational = id.rotational;
        disk.removable = id.removable;

        std::string size;
        if (readFile(m_root + "/sys/block/" + name + "/size", size) == ReadStatus::Ok)
            if (const auto sectors = parseU64(size))
                disk.sizeBytes = Reading<std::uint64_t>::measured(*sectors * kSectorBytes);

        const auto previous = m_previous.find(name);
        if (previous != m_previous.end() && seconds > 0) {
            const Previous& p = previous->second;
            if (counters.sectorsRead >= p.sectorsRead)
                disk.readBps = Reading<double>::measured(static_cast<double>((counters.sectorsRead - p.sectorsRead) * kSectorBytes) / seconds);
            if (counters.sectorsWritten >= p.sectorsWritten)
                disk.writeBps = Reading<double>::measured(static_cast<double>((counters.sectorsWritten - p.sectorsWritten) * kSectorBytes) / seconds);
            if (counters.ioTicksMs >= p.ioTicksMs) {
                const double pct = static_cast<double>(counters.ioTicksMs - p.ioTicksMs) / (seconds * 10.0);
                disk.activePct = Reading<double>::measured(pct > 100.0 ? 100.0 : pct);
            }
        }
        m_previous[name] = {counters.sectorsRead, counters.sectorsWritten, counters.ioTicksMs};

        const auto fs = m_filesystems.find(name);
        if (fs != m_filesystems.end()) {
            disk.mounts = fs->second.mounts;
            disk.fsTotal = Reading<std::uint64_t>::measured(fs->second.total);
            disk.fsFree = Reading<std::uint64_t>::measured(fs->second.free);
        } else {
            disk.fsTotal = disk.fsFree = Reading<std::uint64_t>::missing(Availability::Unsupported);
        }
        out.push_back(std::move(disk));
    }
    m_previousMs = nowMs;
}

NetworkSampler::NetworkSampler(const std::string& root) : m_root(root), m_netdev(root + "/proc/net/dev") {}

void NetworkSampler::sample(std::int64_t nowMs, std::vector<NetworkSample>& out)
{
    if (m_netdev.read(m_buffer) != ReadStatus::Ok)
        return;
    parseNetDev(m_buffer, m_counters);

    const double seconds = m_previousMs ? static_cast<double>(nowMs - m_previousMs) / 1000.0 : 0;
    for (const NetCounters& counters : m_counters) {
        NetworkSample net;
        net.name = std::string(counters.name);
        const std::string dir = m_root + "/sys/class/net/" + net.name;
        net.loopback = readTrimmed(dir + "/type") == "772";
        net.mac = readTrimmed(dir + "/address");
        net.operState = readTrimmed(dir + "/operstate");
        net.wireless = exists(dir + "/wireless") || exists(dir + "/phy80211");
        net.rxTotal = counters.rxBytes;
        net.txTotal = counters.txBytes;

        std::string speed;
        const ReadStatus speedStatus = readFile(dir + "/speed", speed);
        const auto mbps = speedStatus == ReadStatus::Ok ? parseI64(speed) : std::nullopt;
        // Wi-Fi and down links report -1 or fail with EINVAL. That is not a speed of 0.
        net.linkMbps = mbps && *mbps > 0 ? Reading<double>::measured(static_cast<double>(*mbps))
                                         : Reading<double>::missing(Availability::Unsupported);

        const auto previous = m_previous.find(net.name);
        if (previous != m_previous.end() && seconds > 0) {
            if (counters.rxBytes >= previous->second.rx)
                net.rxBps = Reading<double>::measured(static_cast<double>(counters.rxBytes - previous->second.rx) / seconds);
            if (counters.txBytes >= previous->second.tx)
                net.txBps = Reading<double>::measured(static_cast<double>(counters.txBytes - previous->second.tx) / seconds);
        }
        m_previous[net.name] = {counters.rxBytes, counters.txBytes};
        out.push_back(std::move(net));
    }
    m_previousMs = nowMs;
}

} // namespace fg
