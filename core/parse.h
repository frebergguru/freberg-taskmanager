// Parsers for the text the kernel exposes. Pure functions over string_view, so
// they can be tested with plain strings and do not allocate per line.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace fg {

struct CpuTimes {
    std::uint64_t total = 0;
    std::uint64_t idle = 0; // idle + iowait
};

// Index 0 of the result is the "cpu" line, 1.. are cpu0, cpu1, ...
void parseCpuStat(std::string_view text, std::vector<CpuTimes>& out);

// Percent busy between two samples. Empty when no time has passed.
std::optional<double> cpuBusyPercent(const CpuTimes& before, const CpuTimes& after);

// Value of one "Key:   123 kB" line, converted to bytes.
std::optional<std::uint64_t> meminfoBytes(std::string_view text, std::string_view key);

struct DiskCounters {
    std::string_view name;
    std::uint64_t sectorsRead = 0;
    std::uint64_t sectorsWritten = 0;
    std::uint64_t ioTicksMs = 0;
};
void parseDiskstats(std::string_view text, std::vector<DiskCounters>& out);

struct NetCounters {
    std::string_view name;
    std::uint64_t rxBytes = 0;
    std::uint64_t txBytes = 0;
};
void parseNetDev(std::string_view text, std::vector<NetCounters>& out);

struct PressureValues {
    std::optional<double> some10;
    std::optional<double> full10;
};
PressureValues parsePressure(std::string_view text);

struct PidStat {
    std::string_view comm;
    char state = '?';
    int ppid = 0;
    std::uint64_t utime = 0;
    std::uint64_t stime = 0;
    int nice = 0;
    int threads = 0;
    std::uint64_t startTicks = 0;
    std::int64_t rssPages = 0;
};
// Handles names containing spaces and parentheses, such as "(sd-pam)" or "a) b".
bool parsePidStat(std::string_view text, PidStat& out);

struct PidIo {
    std::uint64_t readBytes = 0;
    std::uint64_t writeBytes = 0;
};
bool parsePidIo(std::string_view text, PidIo& out);

// "0-3,8,10-11" -> {0,1,2,3,8,10,11}
std::vector<int> parseCpuList(std::string_view text);

std::string_view trimmed(std::string_view text);
std::optional<std::uint64_t> parseU64(std::string_view text);
std::optional<std::int64_t> parseI64(std::string_view text);

} // namespace fg
