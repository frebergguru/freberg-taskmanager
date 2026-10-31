// The Flight Recorder's data: one frame per sample, kept in a size-capped buffer,
// saved as an open .fgtrace file. The format is written byte by byte in little
// endian, so files move freely between platforms. See Docs/fgtrace-format.md.
#pragma once

#include "snapshot.h"

#include <cstdint>
#include <deque>
#include <limits>
#include <string>
#include <vector>

namespace fg {

struct TraceProcess {
    std::string name;
    int pid = 0;
    float cpuPct = 0;
    float rssBytes = 0;
};

// A value we did not have is NaN, and is drawn as a gap on replay.
struct TraceFrame {
    std::int64_t ms = 0;
    float cpuPct, memoryUsedBytes, memoryTotalBytes;
    float diskReadBps, diskWriteBps;
    float netRxBps, netTxBps;
    float gpuPct;
    float cpuTempC;
    float cpuPowerW;
    float pressureCpu, pressureMemory, pressureIo;
    std::vector<TraceProcess> top;
};

// Each one of these is one panel on the replay page.
enum class TraceChannel { Cpu, Memory, DiskRead, DiskWrite, NetRx, NetTx, Gpu, Temp, Power, PressureCpu, PressureMemory, PressureIo, Count };
float channelValue(const TraceFrame& frame, TraceChannel channel);
const char* channelKey(TraceChannel channel);

TraceFrame frameFromSnapshot(const Snapshot& snapshot, std::size_t topCount = 5);

// Holds as many frames as fit in byteLimit, dropping the oldest.
class TraceBuffer {
public:
    explicit TraceBuffer(std::size_t byteLimit = 16 * 1024 * 1024) : m_byteLimit(byteLimit) {}
    void setByteLimit(std::size_t byteLimit);
    void add(TraceFrame frame);
    void clear();
    const std::deque<TraceFrame>& frames() const { return m_frames; }
    std::size_t bytesUsed() const { return m_bytes; }

private:
    static std::size_t costOf(const TraceFrame& frame);
    void trim();

    std::deque<TraceFrame> m_frames;
    std::size_t m_byteLimit;
    std::size_t m_bytes = 0;
};

// Both return false and fill error on failure. A damaged file is refused, not guessed at.
bool saveTrace(const std::string& path, const std::deque<TraceFrame>& frames, std::string& error);
bool loadTrace(const std::string& path, std::deque<TraceFrame>& frames, std::string& error);

bool exportCsv(const std::string& path, const std::deque<TraceFrame>& frames, std::string& error);
bool exportJson(const std::string& path, const std::deque<TraceFrame>& frames, std::string& error);

// "Why was it slow?": who used the most over a stretch of the recording, and the peaks.
struct TraceContributor {
    std::string name;
    double averageCpuPct = 0;
    double peakRssBytes = 0;
};
inline constexpr double kNoPeak = std::numeric_limits<double>::quiet_NaN();
struct TraceAnalysis {
    std::size_t frames = 0;
    std::size_t gaps = 0; // frames where the CPU reading was missing
    double seconds = 0;
    // NaN when nothing was measured in this stretch, never 0.
    double peakCpu = kNoPeak, peakMemoryBytes = kNoPeak, peakDiskBps = kNoPeak, peakNetBps = kNoPeak, peakTempC = kNoPeak, peakPowerW = kNoPeak, peakPressure = kNoPeak;
    std::vector<TraceContributor> contributors;
};
TraceAnalysis analyseTrace(const std::deque<TraceFrame>& frames, std::size_t first, std::size_t last, std::size_t limit = 8);

} // namespace fg
