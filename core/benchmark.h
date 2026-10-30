// Short CPU, memory and disk tests. Nothing runs until the user starts it.
#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <string>

namespace fg {

// Work units per second, so a higher number is faster. Comparable only between
// runs of this app, not with other benchmarks.
double benchCpu(int threads, double seconds, const std::atomic<bool>& cancel);

// Sequential copy speed in MB/s, on a block larger than the CPU's caches.
double benchMemoryBandwidth(std::size_t bytes, double seconds, const std::atomic<bool>& cancel);

// Average time for one random memory access, in nanoseconds.
double benchMemoryLatency(std::size_t bytes, double seconds, const std::atomic<bool>& cancel);

struct DiskBenchResult {
    double readMBs = 0;
    double writeMBs = 0;
};
// Writes a scratch file in the folder, reads it back, and deletes it.
// Empty if the file could not be written.
std::optional<DiskBenchResult> benchDisk(const std::string& folder, std::size_t bytes, const std::atomic<bool>& cancel);

} // namespace fg
