// Walks a folder and adds up what it really occupies on disk. Stays on one
// filesystem, and counts a file with several hard links only once.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fg {

struct DiskNode {
    std::string name;
    std::uint64_t bytes = 0;
    std::uint64_t files = 0;
    bool isDir = false;
    bool denied = false; // we could not look inside, so the true size is larger
    DiskNode* parent = nullptr;
    std::vector<std::unique_ptr<DiskNode>> children; // largest first
};

struct DiskScanResult {
    std::unique_ptr<DiskNode> root;
    std::uint64_t deniedFolders = 0;
    bool cancelled = false;
};

// progressFiles, if given, is updated as the scan goes.
DiskScanResult scanFolder(const std::string& path, const std::atomic<bool>& cancel, std::atomic<std::uint64_t>* progressFiles = nullptr);

std::string nodePath(const DiskNode& node);

} // namespace fg
