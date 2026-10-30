#include "benchmark.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <numeric>
#include <random>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fg {

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// Mixed integer and floating point work, kept from being optimized away.
std::uint64_t cpuWorkUnit(std::uint64_t seed)
{
    std::uint64_t x = seed | 1;
    double y = 1.0;
    for (int i = 0; i < 4096; ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        y = std::sqrt(y + static_cast<double>(x & 0xffff));
    }
    return x + static_cast<std::uint64_t>(y);
}

} // namespace

double benchCpu(int threads, double seconds, const std::atomic<bool>& cancel)
{
    std::atomic<std::uint64_t> total{0};
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(threads));
    const auto start = Clock::now();
    for (int t = 0; t < threads; ++t)
        workers.emplace_back([&, t] {
            std::uint64_t seed = static_cast<std::uint64_t>(t) + 1;
            std::uint64_t units = 0;
            while (!cancel.load(std::memory_order_relaxed) && secondsSince(start) < seconds) {
                for (int i = 0; i < 64; ++i)
                    seed = cpuWorkUnit(seed);
                units += 64;
            }
            total += units;
            volatile std::uint64_t keep = seed;
            (void)keep;
        });
    for (std::thread& worker : workers)
        worker.join();
    return static_cast<double>(total.load()) / secondsSince(start);
}

double benchMemoryBandwidth(std::size_t bytes, double seconds, const std::atomic<bool>& cancel)
{
    std::vector<char> from(bytes, 1), to(bytes, 2);
    std::uint64_t copied = 0;
    const auto start = Clock::now();
    while (!cancel.load(std::memory_order_relaxed) && secondsSince(start) < seconds) {
        std::memcpy(to.data(), from.data(), bytes);
        copied += bytes;
        from[copied % bytes] = to[(copied + 1) % bytes];
    }
    const double elapsed = secondsSince(start);
    return elapsed > 0 ? static_cast<double>(copied) / (1024.0 * 1024.0) / elapsed : 0;
}

double benchMemoryLatency(std::size_t bytes, double seconds, const std::atomic<bool>& cancel)
{
    // A single random cycle through the block, so the prefetcher can't help.
    const std::size_t count = bytes / sizeof(std::size_t);
    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::mt19937_64 random(42);
    for (std::size_t i = count - 1; i > 0; --i)
        std::swap(order[i], order[random() % i]);
    std::vector<std::size_t> next(count);
    for (std::size_t i = 0; i < count; ++i)
        next[order[i]] = order[(i + 1) % count];

    std::size_t at = 0;
    std::uint64_t hops = 0;
    const auto start = Clock::now();
    while (!cancel.load(std::memory_order_relaxed) && secondsSince(start) < seconds) {
        for (int i = 0; i < 100000; ++i)
            at = next[at];
        hops += 100000;
    }
    volatile std::size_t keep = at;
    (void)keep;
    const double elapsed = secondsSince(start);
    return hops ? elapsed * 1e9 / static_cast<double>(hops) : 0;
}

std::optional<DiskBenchResult> benchDisk(const std::string& folder, std::size_t bytes, const std::atomic<bool>& cancel)
{
    constexpr std::size_t kBlock = 1024 * 1024;
    std::string path = folder + "/.fg-benchmark-XXXXXX";
    const int fd = ::mkstemp(path.data());
    if (fd < 0)
        return std::nullopt;
    ::unlink(path.c_str()); // gone when closed, even if we crash

    void* raw = nullptr;
    if (::posix_memalign(&raw, 4096, kBlock) != 0) {
        ::close(fd);
        return std::nullopt;
    }
    std::unique_ptr<void, decltype(&std::free)> block(raw, &std::free);
    std::mt19937_64 random(7);
    for (std::size_t i = 0; i < kBlock / sizeof(std::uint64_t); ++i)
        static_cast<std::uint64_t*>(raw)[i] = random(); // not compressible

    DiskBenchResult result;
    std::size_t written = 0;
    auto start = Clock::now();
    while (written < bytes && !cancel.load(std::memory_order_relaxed)) {
        const ssize_t got = ::write(fd, raw, kBlock);
        if (got <= 0) {
            ::close(fd);
            return std::nullopt;
        }
        written += static_cast<std::size_t>(got);
    }
    ::fsync(fd);
    double elapsed = secondsSince(start);
    result.writeMBs = elapsed > 0 ? static_cast<double>(written) / (1024.0 * 1024.0) / elapsed : 0;

    // Drop the cached copy, or we would be measuring memory instead of the disk.
    ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
    std::size_t read = 0;
    off_t offset = 0;
    start = Clock::now();
    while (read < written && !cancel.load(std::memory_order_relaxed)) {
        const ssize_t got = ::pread(fd, raw, kBlock, offset);
        if (got <= 0)
            break;
        read += static_cast<std::size_t>(got);
        offset += got;
    }
    elapsed = secondsSince(start);
    result.readMBs = elapsed > 0 ? static_cast<double>(read) / (1024.0 * 1024.0) / elapsed : 0;
    ::close(fd);
    return result;
}

} // namespace fg
