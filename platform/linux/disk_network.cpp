#include "samplers.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <dirent.h>
#include <optional>
#include <sys/stat.h>
#include <sys/statvfs.h>

namespace fg {

namespace {

constexpr std::uint64_t kSectorBytes = 512; // /proc/diskstats always counts in 512-byte units
constexpr std::int64_t kFilesystemRefreshMs = 30000;
constexpr std::int64_t kWifiRefreshMs = 10000;

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

// /proc/self/mounts writes a space in a path as \\040, and likewise tab, newline and backslash.
std::string unescapedMount(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 3 < text.size() && text[i + 1] >= '0' && text[i + 1] <= '3' && text[i + 2] >= '0' && text[i + 2] <= '7' && text[i + 3] >= '0' && text[i + 3] <= '7') {
            out += static_cast<char>((text[i + 1] - '0') * 64 + (text[i + 2] - '0') * 8 + (text[i + 3] - '0'));
            i += 3;
        } else {
            out += text[i];
        }
    }
    return out;
}

// nvme0n1p2 -> nvme0n1 and sda3 -> sda. An encrypted, LVM or RAID volume such as dm-0
// lists what holds it under slaves, so this follows those down to the disks.
void collectDisks(const std::string& root, const std::string& name, std::vector<std::string>& disks, int depth = 0)
{
    constexpr int kMaxLayers = 8;
    if (depth > kMaxLayers)
        return;
    const std::string block = root + "/sys/block/";
    if (exists(block + name)) {
        bool layered = false;
        if (DIR* slaves = ::opendir((block + name + "/slaves").c_str())) {
            while (const dirent* entry = ::readdir(slaves))
                if (entry->d_name[0] != '.') {
                    layered = true;
                    collectDisks(root, entry->d_name, disks, depth + 1);
                }
            ::closedir(slaves);
        }
        if (!layered && std::find(disks.begin(), disks.end(), name) == disks.end())
            disks.push_back(name);
        return;
    }
    std::string candidate = name;
    while (!candidate.empty()) {
        candidate.pop_back();
        if (exists(block + candidate + "/" + name)) {
            collectDisks(root, candidate, disks, depth + 1);
            return;
        }
    }
}

std::vector<std::string> disksUnder(const std::string& root, std::string_view device)
{
    std::vector<std::string> disks;
    if (!device.starts_with("/dev/"))
        return disks;
    // /dev/mapper/name is a link to /dev/dm-0.
    std::string name(device.substr(5));
    char resolved[PATH_MAX];
    if (::realpath((root + std::string(device)).c_str(), resolved)) {
        const std::string_view path(resolved);
        name = std::string(path.substr(path.rfind('/') + 1));
    }
    collectDisks(root, name, disks);
    return disks;
}

} // namespace

DiskSampler::DiskSampler(const std::string& root) : m_root(root), m_diskstats(root + "/proc/diskstats") {}

DiskSampler::Identity& DiskSampler::identity(const std::string& name)
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
        id.size = CachedFile(dir + "/size");
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
    std::unordered_map<std::string, bool> countedDevice;
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
        const std::string mountPoint = unescapedMount(line.substr(a + 1, b - a - 1));
        const std::string_view type = line.substr(b + 1, c == std::string_view::npos ? c : c - b - 1);
        if (isNetworkOrVirtualFs(type))
            continue;

        for (const std::string& disk : disksUnder(m_root, device)) {
            Filesystems& fs = m_filesystems[disk];
            fs.mounts.push_back(mountPoint);
            // A btrfs volume is mounted many times; count its space only once.
            bool& counted = countedDevice[disk + " " + std::string(device)];
            if (counted)
                continue;
            struct statvfs info;
            if (::statvfs((m_root + mountPoint).c_str(), &info) == 0) {
                counted = true;
                fs.measured = true;
                fs.total += static_cast<std::uint64_t>(info.f_blocks) * info.f_frsize;
                fs.free += static_cast<std::uint64_t>(info.f_bavail) * info.f_frsize;
            }
        }
    }
}

void DiskSampler::sample(std::int64_t nowMs, std::vector<DiskSample>& out)
{
    const ReadStatus status = m_diskstats.read(m_buffer);
    if (status != ReadStatus::Ok)
        return; // the empty list tells the UI there are no measured drives
    parseDiskstats(m_buffer, m_counters);

    // A device that left may come back under the same name as a different disk.
    const auto gone = [this](const auto& item) {
        return std::none_of(m_counters.begin(), m_counters.end(), [&item](const DiskCounters& counters) { return counters.name == item.first; });
    };
    std::erase_if(m_identity, gone);
    std::erase_if(m_previous, gone);

    if (m_filesystemsAtMs == 0 || nowMs - m_filesystemsAtMs >= kFilesystemRefreshMs) {
        refreshFilesystems();
        m_filesystemsAtMs = nowMs;
    }

    const double seconds = m_previousMs ? static_cast<double>(nowMs - m_previousMs) / 1000.0 : 0;
    for (const DiskCounters& counters : m_counters) {
        const std::string name(counters.name);
        Identity& id = identity(name);
        if (!id.physical)
            continue;
        DiskSample disk;
        disk.name = name;
        disk.model = id.model;
        disk.rotational = id.rotational;
        disk.removable = id.removable;

        if (id.size.read(m_sizeText) == ReadStatus::Ok)
            if (const auto sectors = parseU64(m_sizeText))
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
        }
        if (fs != m_filesystems.end() && fs->second.measured) {
            disk.fsTotal = Reading<std::uint64_t>::measured(fs->second.total);
            disk.fsFree = Reading<std::uint64_t>::measured(fs->second.free);
        } else {
            disk.fsTotal = disk.fsFree = Reading<std::uint64_t>::missing(Availability::Unsupported);
        }
        out.push_back(std::move(disk));
    }
    m_previousMs = nowMs;
}

NetworkSampler::NetworkSampler(const std::string& root) : m_root(root), m_netdev(root + "/proc/net/dev"), m_wireless(root + "/proc/net/wireless") {}

void NetworkSampler::sample(std::int64_t nowMs, std::vector<NetworkSample>& out)
{
    if (m_netdev.read(m_buffer) != ReadStatus::Ok)
        return;
    parseNetDev(m_buffer, m_counters);

    // Containers come and go with fresh names, and a name that returns may be a new interface.
    const auto gone = [this](const auto& item) {
        return std::none_of(m_counters.begin(), m_counters.end(), [&item](const NetCounters& counters) { return counters.name == item.first; });
    };
    std::erase_if(m_faces, gone);
    std::erase_if(m_previous, gone);
    std::erase_if(m_wifi, gone);

    // Face | status | link | level | noise ...  The trailing dots are part of the format.
    if (m_wireless.read(m_wirelessText) != ReadStatus::Ok)
        m_wirelessText.clear();
    const std::string& wireless = m_wirelessText;

    const double seconds = m_previousMs ? static_cast<double>(nowMs - m_previousMs) / 1000.0 : 0;
    for (const NetCounters& counters : m_counters) {
        NetworkSample net;
        net.name = std::string(counters.name);
        auto known = m_faces.find(net.name);
        if (known == m_faces.end()) {
            const std::string dir = m_root + "/sys/class/net/" + net.name;
            Face created;
            created.address = CachedFile(dir + "/address");
            created.operState = CachedFile(dir + "/operstate");
            created.speed = CachedFile(dir + "/speed");
            created.loopback = readTrimmed(dir + "/type") == "772";
            created.wireless = exists(dir + "/wireless") || exists(dir + "/phy80211");
            created.hasDevice = exists(dir + "/device");
            known = m_faces.emplace(net.name, std::move(created)).first;
        }
        Face& face = known->second;
        net.loopback = face.loopback;
        net.wireless = face.wireless;
        net.hasDevice = face.hasDevice;
        if (face.address.read(m_faceText) == ReadStatus::Ok)
            net.mac = std::string(trimmed(m_faceText));
        if (face.operState.read(m_faceText) == ReadStatus::Ok)
            net.operState = std::string(trimmed(m_faceText));
        net.rxTotal = counters.rxBytes;
        net.txTotal = counters.txBytes;

        const ReadStatus speedStatus = face.speed.read(m_faceText);
        const auto mbps = speedStatus == ReadStatus::Ok ? parseI64(m_faceText) : std::nullopt;
        // Wi-Fi and down links report -1 or fail with EINVAL. That is not a speed of 0.
        net.linkMbps = mbps && *mbps > 0 ? Reading<double>::measured(static_cast<double>(*mbps))
                                         : Reading<double>::missing(Availability::Unsupported);

        net.wifiSignalDbm = net.wifiQualityPct = Reading<double>::missing(Availability::Unsupported);
        net.wifiNetwork = Reading<std::string>::missing(Availability::Unsupported);
        if (net.wireless) {
            // With a fake root in tests there is no kernel behind it, so ask only the real one.
            if (m_root.empty()) {
                KnownWifi& wifi = m_wifi[net.name];
                if (wifi.atMs == 0 || nowMs - wifi.atMs >= kWifiRefreshMs) {
                    wifi.link = readWifiLink(net.name);
                    wifi.atMs = nowMs;
                }
                net.wifiNetwork = wifi.link.network;
                if (wifi.link.sendMbps.known())
                    net.linkMbps = wifi.link.sendMbps;
            }
            const std::string key = net.name + ":";
            const std::size_t at = wireless.find(key);
            if (at != std::string::npos) {
                const std::size_t end = wireless.find('\n', at);
                std::string_view line = std::string_view(wireless).substr(at + key.size(), end - at - key.size());
                std::string_view fields[3];
                for (std::string_view& field : fields) {
                    line = trimmed(line);
                    const std::size_t gap = line.find(' ');
                    field = line.substr(0, gap);
                    line = gap == std::string_view::npos ? std::string_view{} : line.substr(gap);
                }
                const auto number = [](std::string_view text) -> std::optional<std::int64_t> {
                    if (!text.empty() && text.back() == '.')
                        text.remove_suffix(1);
                    return parseI64(text);
                };
                if (const auto link = number(fields[1]))
                    net.wifiQualityPct = Reading<double>::estimated(std::min(100.0, static_cast<double>(*link) * 100.0 / 70.0)); // assumes the scale most drivers use
                if (const auto level = number(fields[2]))
                    net.wifiSignalDbm = Reading<double>::measured(static_cast<double>(*level));
            }
        }

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
