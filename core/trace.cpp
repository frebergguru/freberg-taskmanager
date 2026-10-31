#include "trace.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string_view>
#include <unordered_map>

namespace fg {

namespace {

constexpr char kMagic[8] = {'F', 'G', 'T', 'R', 'A', 'C', 'E', '\n'};
constexpr std::uint32_t kVersion = 1;

// Limits for reading. A value past these means a damaged file, and memory is
// never reserved on the word of the file alone.
constexpr std::uint32_t kMaxTopProcesses = 64;
constexpr std::uint32_t kMaxNameBytes = 4096;
constexpr std::uint64_t kMaxFrames = 100000000;
constexpr float kMaxValue = 1e18f; // far past any real figure, and still fits every integer it is turned into

constexpr float kGap = std::numeric_limits<float>::quiet_NaN();

template <class T>
float asFloat(const Reading<T>& reading)
{
    return reading.known() ? static_cast<float>(reading.value) : kGap;
}

struct FileCloser {
    void operator()(std::FILE* file) const { std::fclose(file); }
};
using File = std::unique_ptr<std::FILE, FileCloser>;

void putBytes(std::string& out, const void* data, std::size_t size)
{
    out.append(static_cast<const char*>(data), size);
}

void putU32(std::string& out, std::uint32_t value)
{
    const unsigned char bytes[4] = {static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8), static_cast<unsigned char>(value >> 16), static_cast<unsigned char>(value >> 24)};
    putBytes(out, bytes, 4);
}

void putU64(std::string& out, std::uint64_t value)
{
    putU32(out, static_cast<std::uint32_t>(value));
    putU32(out, static_cast<std::uint32_t>(value >> 32));
}

void putFloat(std::string& out, float value)
{
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    putU32(out, bits);
}

// Stored values are NaN for a gap, or a figure that later code can safely convert and sum.
bool plausibleChannel(float value, bool mayBeNegative)
{
    return std::isnan(value) || (value <= kMaxValue && value >= (mayBeNegative ? -kMaxValue : 0.0f));
}

class Reader {
public:
    explicit Reader(std::FILE* file) : m_file(file) {}

    bool bytes(void* data, std::size_t size) { return std::fread(data, 1, size, m_file) == size; }

    bool u32(std::uint32_t& value)
    {
        unsigned char b[4];
        if (!bytes(b, 4))
            return false;
        value = static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) | (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
        return true;
    }

    bool u64(std::uint64_t& value)
    {
        std::uint32_t low, high;
        if (!u32(low) || !u32(high))
            return false;
        value = low | (static_cast<std::uint64_t>(high) << 32);
        return true;
    }

    bool f32(float& value)
    {
        std::uint32_t bits;
        if (!u32(bits))
            return false;
        std::memcpy(&value, &bits, 4);
        return true;
    }

private:
    std::FILE* m_file;
};

float* channelSlot(TraceFrame& frame, int channel)
{
    switch (static_cast<TraceChannel>(channel)) {
    case TraceChannel::Cpu: return &frame.cpuPct;
    case TraceChannel::Memory: return &frame.memoryUsedBytes;
    case TraceChannel::DiskRead: return &frame.diskReadBps;
    case TraceChannel::DiskWrite: return &frame.diskWriteBps;
    case TraceChannel::NetRx: return &frame.netRxBps;
    case TraceChannel::NetTx: return &frame.netTxBps;
    case TraceChannel::Gpu: return &frame.gpuPct;
    case TraceChannel::Temp: return &frame.cpuTempC;
    case TraceChannel::Power: return &frame.cpuPowerW;
    case TraceChannel::PressureCpu: return &frame.pressureCpu;
    case TraceChannel::PressureMemory: return &frame.pressureMemory;
    case TraceChannel::PressureIo: return &frame.pressureIo;
    case TraceChannel::Count: break;
    }
    return nullptr;
}

std::string errnoText(const std::string& what)
{
    return what + ": " + std::strerror(errno);
}

// CSV and JSON text for a number. Unknown values stay empty or null, never 0.
std::string numberOr(float value, const char* missing)
{
    if (std::isnan(value))
        return missing;
    char text[32];
    const auto end = std::to_chars(text, text + sizeof text, value, std::chars_format::general, 6).ptr;
    return std::string(text, end);
}

// Always with a point, whatever the locale: a decimal comma would break both formats.
std::string secondsText(std::int64_t ms)
{
    char text[32];
    const auto end = std::to_chars(text, text + sizeof text, static_cast<double>(ms) / 1000.0, std::chars_format::fixed, 3).ptr;
    return std::string(text, end);
}

// How many bytes the UTF-8 character at this position takes, or 0 if it isn't valid.
std::size_t utf8Length(std::string_view text)
{
    const auto byte = [text](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char first = byte(0);
    std::size_t length = 0;
    unsigned char low = 0x80, high = 0xbf; // the range the second byte may be in
    if (first < 0x80)
        return 1;
    if (first >= 0xc2 && first <= 0xdf) {
        length = 2;
    } else if (first >= 0xe0 && first <= 0xef) {
        length = 3;
        low = first == 0xe0 ? 0xa0 : 0x80; // no overlong forms
        high = first == 0xed ? 0x9f : 0xbf; // no surrogates
    } else if (first >= 0xf0 && first <= 0xf4) {
        length = 4;
        low = first == 0xf0 ? 0x90 : 0x80;
        high = first == 0xf4 ? 0x8f : 0xbf;
    } else {
        return 0;
    }
    if (text.size() < length || byte(1) < low || byte(1) > high)
        return 0;
    for (std::size_t i = 2; i < length; ++i)
        if (byte(i) < 0x80 || byte(i) > 0xbf)
            return 0;
    return length;
}

// JSON must be valid UTF-8, and the kernel can cut a process name in the middle of a character.
std::string jsonEscaped(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const char c = text[i];
        const std::size_t length = utf8Length(std::string_view(text).substr(i));
        if (length == 0) {
            out += "\xef\xbf\xbd"; // the replacement character
            ++i;
            continue;
        }
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char escape[8];
            std::snprintf(escape, sizeof escape, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
            out += escape;
        } else {
            out.append(text, i, length);
        }
        i += length;
    }
    return out;
}

// Reports a full disk or similar, which a buffered write only shows when it is flushed.
bool finishWriting(File& file, const std::string& path, std::string& error)
{
    const bool wroteAll = std::ferror(file.get()) == 0;
    const int savedErrno = errno;
    std::FILE* raw = file.release();
    const bool closed = std::fclose(raw) == 0;
    if (wroteAll && closed)
        return true;
    if (!wroteAll)
        errno = savedErrno;
    error = errnoText(path);
    return false;
}

} // namespace

float channelValue(const TraceFrame& frame, TraceChannel channel)
{
    const float* slot = channelSlot(const_cast<TraceFrame&>(frame), static_cast<int>(channel));
    return slot ? *slot : kGap;
}

const char* channelKey(TraceChannel channel)
{
    switch (channel) {
    case TraceChannel::Cpu: return "cpu_percent";
    case TraceChannel::Memory: return "memory_used_bytes";
    case TraceChannel::DiskRead: return "disk_read_bytes_per_second";
    case TraceChannel::DiskWrite: return "disk_write_bytes_per_second";
    case TraceChannel::NetRx: return "network_receive_bytes_per_second";
    case TraceChannel::NetTx: return "network_send_bytes_per_second";
    case TraceChannel::Gpu: return "gpu_percent";
    case TraceChannel::Temp: return "cpu_temperature_celsius";
    case TraceChannel::Power: return "cpu_power_watts";
    case TraceChannel::PressureCpu: return "pressure_cpu_percent";
    case TraceChannel::PressureMemory: return "pressure_memory_percent";
    case TraceChannel::PressureIo: return "pressure_io_percent";
    case TraceChannel::Count: break;
    }
    return "";
}

TraceFrame frameFromSnapshot(const Snapshot& snapshot, std::size_t topCount)
{
    TraceFrame frame;
    frame.ms = snapshot.monotonicMs;
    frame.cpuPct = asFloat(snapshot.cpu.usagePct);
    frame.memoryUsedBytes = asFloat(snapshot.memory.used);
    frame.memoryTotalBytes = asFloat(snapshot.memory.total);

    const auto sum = [](const auto& items, auto reading) {
        float total = kGap;
        for (const auto& item : items) {
            const auto& value = reading(item);
            if (value.known())
                total = (std::isnan(total) ? 0.0f : total) + static_cast<float>(value.value);
        }
        return total;
    };
    frame.diskReadBps = sum(snapshot.disks, [](const DiskSample& d) -> const Reading<double>& { return d.readBps; });
    frame.diskWriteBps = sum(snapshot.disks, [](const DiskSample& d) -> const Reading<double>& { return d.writeBps; });
    std::vector<NetworkSample> external;
    frame.netRxBps = frame.netTxBps = kGap;
    for (const NetworkSample& net : snapshot.networks) {
        if (!countsTowardTotal(net, snapshot.networks))
            continue;
        if (net.rxBps.known())
            frame.netRxBps = (std::isnan(frame.netRxBps) ? 0.0f : frame.netRxBps) + static_cast<float>(net.rxBps.value);
        if (net.txBps.known())
            frame.netTxBps = (std::isnan(frame.netTxBps) ? 0.0f : frame.netTxBps) + static_cast<float>(net.txBps.value);
    }
    frame.gpuPct = snapshot.gpus.empty() ? kGap : asFloat(snapshot.gpus.front().busyPct);
    frame.cpuTempC = asFloat(snapshot.thermal.cpuHotspotC);
    frame.cpuPowerW = asFloat(snapshot.power.cpuPackageW);
    frame.pressureCpu = asFloat(snapshot.pressure.cpu.some10);
    frame.pressureMemory = asFloat(snapshot.pressure.memory.some10);
    frame.pressureIo = asFloat(snapshot.pressure.io.some10);

    std::vector<const ProcessSample*> busy;
    busy.reserve(snapshot.processes.size());
    for (const ProcessSample& process : snapshot.processes)
        if (process.cpuPct.known())
            busy.push_back(&process);
    const std::size_t count = std::min(topCount, busy.size());
    std::partial_sort(busy.begin(), busy.begin() + static_cast<std::ptrdiff_t>(count), busy.end(),
                      [](const ProcessSample* a, const ProcessSample* b) { return a->cpuPct.value > b->cpuPct.value; });
    frame.top.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        frame.top.push_back({busy[i]->info->name, busy[i]->info->id.pid, static_cast<float>(busy[i]->cpuPct.value), static_cast<float>(busy[i]->rssBytes.value)});
    return frame;
}

std::size_t TraceBuffer::costOf(const TraceFrame& frame)
{
    std::size_t cost = sizeof(TraceFrame) + frame.top.capacity() * sizeof(TraceProcess);
    for (const TraceProcess& process : frame.top)
        cost += process.name.capacity();
    return cost;
}

void TraceBuffer::trim()
{
    while (m_bytes > m_byteLimit && m_frames.size() > 1) {
        m_bytes -= costOf(m_frames.front());
        m_frames.pop_front();
    }
}

void TraceBuffer::setByteLimit(std::size_t byteLimit)
{
    m_byteLimit = byteLimit;
    trim();
}

void TraceBuffer::add(TraceFrame frame)
{
    m_bytes += costOf(frame);
    m_frames.push_back(std::move(frame));
    trim();
}

void TraceBuffer::clear()
{
    m_frames.clear();
    m_bytes = 0;
}

bool saveTrace(const std::string& path, const std::deque<TraceFrame>& frames, std::string& error)
{
    const std::string temporary = path + ".part";
    {
        File file(std::fopen(temporary.c_str(), "wb"));
        if (!file) {
            error = errnoText(path);
            return false;
        }
        std::string out;
        putBytes(out, kMagic, sizeof kMagic);
        putU32(out, kVersion);
        putU32(out, static_cast<std::uint32_t>(TraceChannel::Count));
        putU64(out, frames.size());
        for (const TraceFrame& frame : frames) {
            putU64(out, static_cast<std::uint64_t>(frame.ms));
            putFloat(out, frame.memoryTotalBytes);
            for (int channel = 0; channel < static_cast<int>(TraceChannel::Count); ++channel)
                putFloat(out, *channelSlot(const_cast<TraceFrame&>(frame), channel));
            putU32(out, static_cast<std::uint32_t>(frame.top.size()));
            for (const TraceProcess& process : frame.top) {
                putU32(out, static_cast<std::uint32_t>(process.name.size()));
                putBytes(out, process.name.data(), process.name.size());
                putU32(out, static_cast<std::uint32_t>(process.pid));
                putFloat(out, process.cpuPct);
                putFloat(out, process.rssBytes);
            }
            if (out.size() > 1024 * 1024) {
                if (std::fwrite(out.data(), 1, out.size(), file.get()) != out.size()) {
                    error = errnoText(path);
                    std::remove(temporary.c_str());
                    return false;
                }
                out.clear();
            }
        }
        std::fwrite(out.data(), 1, out.size(), file.get());
        if (!finishWriting(file, path, error)) {
            std::remove(temporary.c_str());
            return false;
        }
    }
    // Only a complete file ever gets the real name.
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        error = errnoText(path);
        std::remove(temporary.c_str());
        return false;
    }
    return true;
}

bool loadTrace(const std::string& path, std::deque<TraceFrame>& frames, std::string& error)
{
    File file(std::fopen(path.c_str(), "rb"));
    if (!file) {
        error = errnoText(path);
        return false;
    }
    Reader in(file.get());
    const auto damaged = [&error] {
        error = "The file is damaged or is not a recording.";
        return false;
    };

    char magic[sizeof kMagic];
    std::uint32_t version = 0, channels = 0;
    std::uint64_t count = 0;
    if (!in.bytes(magic, sizeof magic) || std::memcmp(magic, kMagic, sizeof kMagic) != 0 || !in.u32(version))
        return damaged();
    if (version != kVersion) {
        error = "This recording was made by a newer version of the app.";
        return false;
    }
    if (!in.u32(channels) || channels > 1024 || !in.u64(count) || count > kMaxFrames)
        return damaged();

    std::deque<TraceFrame> loaded;
    for (std::uint64_t i = 0; i < count; ++i) {
        TraceFrame frame;
        std::uint64_t ms = 0;
        if (!in.u64(ms) || ms > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) || !in.f32(frame.memoryTotalBytes) || !plausibleChannel(frame.memoryTotalBytes, false))
            return damaged();
        frame.ms = static_cast<std::int64_t>(ms);
        // Channels a later version adds are read past; ones this file lacks stay gaps.
        for (int channel = 0; channel < static_cast<int>(TraceChannel::Count); ++channel)
            *channelSlot(frame, channel) = kGap;
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            float value = 0;
            if (!in.f32(value) || !plausibleChannel(value, channel == static_cast<std::uint32_t>(TraceChannel::Temp)))
                return damaged();
            if (channel < static_cast<std::uint32_t>(TraceChannel::Count))
                *channelSlot(frame, static_cast<int>(channel)) = value;
        }
        std::uint32_t top = 0;
        if (!in.u32(top) || top > kMaxTopProcesses)
            return damaged();
        frame.top.resize(top);
        for (TraceProcess& process : frame.top) {
            std::uint32_t size = 0, pid = 0;
            if (!in.u32(size) || size > kMaxNameBytes)
                return damaged();
            process.name.resize(size);
            if ((size && !in.bytes(process.name.data(), size)) || !in.u32(pid) || !in.f32(process.cpuPct) || !in.f32(process.rssBytes)
                || std::isnan(process.cpuPct) || std::isnan(process.rssBytes) || !plausibleChannel(process.cpuPct, false) || !plausibleChannel(process.rssBytes, false))
                return damaged();
            process.pid = static_cast<int>(pid);
        }
        loaded.push_back(std::move(frame));
    }
    frames = std::move(loaded);
    return true;
}

bool exportCsv(const std::string& path, const std::deque<TraceFrame>& frames, std::string& error)
{
    File file(std::fopen(path.c_str(), "w"));
    if (!file) {
        error = errnoText(path);
        return false;
    }
    std::string line = "seconds";
    for (int channel = 0; channel < static_cast<int>(TraceChannel::Count); ++channel) {
        line += ',';
        line += channelKey(static_cast<TraceChannel>(channel));
    }
    line += '\n';
    std::fputs(line.c_str(), file.get());
    const std::int64_t start = frames.empty() ? 0 : frames.front().ms;
    for (const TraceFrame& frame : frames) {
        line = secondsText(frame.ms - start);
        for (int channel = 0; channel < static_cast<int>(TraceChannel::Count); ++channel) {
            line += ',';
            line += numberOr(channelValue(frame, static_cast<TraceChannel>(channel)), "");
        }
        line += '\n';
        std::fputs(line.c_str(), file.get());
    }
    return finishWriting(file, path, error);
}

bool exportJson(const std::string& path, const std::deque<TraceFrame>& frames, std::string& error)
{
    File file(std::fopen(path.c_str(), "w"));
    if (!file) {
        error = errnoText(path);
        return false;
    }
    std::fputs("{\"frames\":[\n", file.get());
    const std::int64_t start = frames.empty() ? 0 : frames.front().ms;
    bool firstFrame = true;
    for (const TraceFrame& frame : frames) {
        std::string line = firstFrame ? "{" : ",\n{";
        firstFrame = false;
        line += "\"seconds\":";
        line += secondsText(frame.ms - start);
        for (int channel = 0; channel < static_cast<int>(TraceChannel::Count); ++channel) {
            line += ",\"";
            line += channelKey(static_cast<TraceChannel>(channel));
            line += "\":";
            line += numberOr(channelValue(frame, static_cast<TraceChannel>(channel)), "null");
        }
        line += ",\"top\":[";
        for (std::size_t i = 0; i < frame.top.size(); ++i) {
            if (i)
                line += ',';
            line += "{\"name\":\"" + jsonEscaped(frame.top[i].name) + "\",\"pid\":" + std::to_string(frame.top[i].pid) + ",\"cpu_percent\":" + numberOr(frame.top[i].cpuPct, "null") + ",\"rss_bytes\":" + numberOr(frame.top[i].rssBytes, "null") + "}";
        }
        line += "]}";
        std::fputs(line.c_str(), file.get());
    }
    std::fputs("\n]}\n", file.get());
    return finishWriting(file, path, error);
}

TraceAnalysis analyseTrace(const std::deque<TraceFrame>& frames, std::size_t first, std::size_t last, std::size_t limit)
{
    TraceAnalysis out;
    if (frames.empty())
        return out;
    last = std::min(last, frames.size() - 1);
    first = std::min(first, last);
    out.frames = last - first + 1;
    out.seconds = static_cast<double>(frames[last].ms - frames[first].ms) / 1000.0;

    const auto peak = [](double& best, double value) {
        if (!std::isnan(value))
            best = std::isnan(best) ? value : std::max(best, value);
    };
    // One direction may be missing, and then the sum is what is known. Both missing stays a gap.
    const auto sumOf = [](float a, float b) {
        if (std::isnan(a))
            return static_cast<double>(b);
        return static_cast<double>(a) + (std::isnan(b) ? 0.0 : static_cast<double>(b));
    };
    struct Totals {
        double cpu = 0;
        double rss = 0;
    };
    std::unordered_map<std::string, Totals> totals;
    for (std::size_t i = first; i <= last; ++i) {
        const TraceFrame& frame = frames[i];
        if (std::isnan(frame.cpuPct))
            ++out.gaps;
        peak(out.peakCpu, frame.cpuPct);
        peak(out.peakMemoryBytes, frame.memoryUsedBytes);
        peak(out.peakDiskBps, sumOf(frame.diskReadBps, frame.diskWriteBps));
        peak(out.peakNetBps, sumOf(frame.netRxBps, frame.netTxBps));
        peak(out.peakTempC, frame.cpuTempC);
        peak(out.peakPowerW, frame.cpuPowerW);
        peak(out.peakPressure, frame.pressureCpu);
        peak(out.peakPressure, frame.pressureMemory);
        peak(out.peakPressure, frame.pressureIo);
        for (const TraceProcess& process : frame.top) {
            Totals& t = totals[process.name];
            t.cpu += static_cast<double>(process.cpuPct);
            t.rss = std::max(t.rss, static_cast<double>(process.rssBytes));
        }
    }
    out.contributors.reserve(totals.size());
    for (const auto& [name, t] : totals)
        out.contributors.push_back({name, t.cpu / static_cast<double>(out.frames), t.rss});
    std::sort(out.contributors.begin(), out.contributors.end(), [](const TraceContributor& a, const TraceContributor& b) { return a.averageCpuPct > b.averageCpuPct; });
    if (out.contributors.size() > limit)
        out.contributors.resize(limit);
    return out;
}

} // namespace fg
