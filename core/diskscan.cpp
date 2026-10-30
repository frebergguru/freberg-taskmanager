#include "diskscan.h"

#include <algorithm>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_set>

namespace fg {

namespace {

constexpr std::uint64_t kBlockBytes = 512; // st_blocks always counts in this unit

struct Scan {
    const std::atomic<bool>& cancel;
    std::atomic<std::uint64_t>* progress;
    dev_t device = 0;
    std::uint64_t deniedFolders = 0;
    std::uint64_t filesSeen = 0;
    std::unordered_set<std::uint64_t> hardLinks;
};

// Each level holds one descriptor, and the depth is capped so we can't run out of them.
constexpr int kMaxDepth = 200;

void walk(int dirFd, DiskNode& node, Scan& scan, int depth)
{
    DIR* dir = ::fdopendir(dirFd);
    if (!dir) {
        ::close(dirFd);
        return;
    }
    while (const dirent* entry = ::readdir(dir)) {
        if (scan.cancel.load(std::memory_order_relaxed))
            break;
        const std::string_view name = entry->d_name;
        if (name == "." || name == "..")
            continue;

        struct stat info;
        if (::fstatat(dirFd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0)
            continue;

        if (S_ISDIR(info.st_mode)) {
            if (info.st_dev != scan.device)
                continue; // another filesystem is mounted here
            auto child = std::make_unique<DiskNode>();
            child->name = entry->d_name;
            child->isDir = true;
            child->parent = &node;
            child->bytes = static_cast<std::uint64_t>(info.st_blocks) * kBlockBytes;
            const int fd = depth < kMaxDepth ? ::openat(dirFd, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) : -1;
            if (fd >= 0) {
                walk(fd, *child, scan, depth + 1);
            } else {
                child->denied = true;
                ++scan.deniedFolders;
            }
            node.bytes += child->bytes;
            node.files += child->files;
            node.children.push_back(std::move(child));
        } else {
            std::uint64_t bytes = static_cast<std::uint64_t>(info.st_blocks) * kBlockBytes;
            if (info.st_nlink > 1 && !scan.hardLinks.insert(static_cast<std::uint64_t>(info.st_ino)).second)
                bytes = 0;
            auto child = std::make_unique<DiskNode>();
            child->name = entry->d_name;
            child->parent = &node;
            child->bytes = bytes;
            child->files = 1;
            node.bytes += bytes;
            node.files += 1;
            node.children.push_back(std::move(child));
            if (scan.progress && (++scan.filesSeen & 1023) == 0)
                scan.progress->store(scan.filesSeen, std::memory_order_relaxed);
        }
    }
    ::closedir(dir); // also closes dirFd

    std::sort(node.children.begin(), node.children.end(), [](const auto& a, const auto& b) { return a->bytes > b->bytes; });
    node.children.shrink_to_fit();
}

} // namespace

DiskScanResult scanFolder(const std::string& path, const std::atomic<bool>& cancel, std::atomic<std::uint64_t>* progressFiles)
{
    DiskScanResult result;
    result.root = std::make_unique<DiskNode>();
    result.root->name = path;
    result.root->isDir = true;

    struct stat info;
    const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0 || ::fstat(fd, &info) != 0) {
        if (fd >= 0)
            ::close(fd);
        result.root->denied = true;
        result.deniedFolders = 1;
        return result;
    }
    Scan scan{cancel, progressFiles};
    scan.device = info.st_dev;
    walk(fd, *result.root, scan, 0);
    result.deniedFolders = scan.deniedFolders;
    result.cancelled = cancel.load();
    return result;
}

std::string nodePath(const DiskNode& node)
{
    if (!node.parent)
        return node.name;
    std::string parent = nodePath(*node.parent);
    if (!parent.ends_with('/'))
        parent += '/';
    return parent + node.name;
}

} // namespace fg
