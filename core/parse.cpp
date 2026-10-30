#include "parse.h"

#include <charconv>

namespace fg {

namespace {

// Splits off the next whitespace-separated field.
std::string_view nextField(std::string_view& text)
{
    std::size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t'))
        ++start;
    std::size_t end = start;
    while (end < text.size() && text[end] != ' ' && text[end] != '\t' && text[end] != '\n')
        ++end;
    const std::string_view field = text.substr(start, end - start);
    text.remove_prefix(end);
    return field;
}

std::string_view nextLine(std::string_view& text)
{
    const std::size_t end = text.find('\n');
    if (end == std::string_view::npos) {
        const std::string_view line = text;
        text = {};
        return line;
    }
    const std::string_view line = text.substr(0, end);
    text.remove_prefix(end + 1);
    return line;
}

std::uint64_t u64Or0(std::string_view field)
{
    return parseU64(field).value_or(0);
}

} // namespace

std::string_view trimmed(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\n'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\n'))
        text.remove_suffix(1);
    return text;
}

std::optional<std::uint64_t> parseU64(std::string_view text)
{
    text = trimmed(text);
    std::uint64_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size())
        return std::nullopt;
    return value;
}

std::optional<std::int64_t> parseI64(std::string_view text)
{
    text = trimmed(text);
    std::int64_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size())
        return std::nullopt;
    return value;
}

void parseCpuStat(std::string_view text, std::vector<CpuTimes>& out)
{
    out.clear();
    while (!text.empty()) {
        std::string_view line = nextLine(text);
        if (!line.starts_with("cpu"))
            break; // the cpu lines come first, and the rest of the file is long
        nextField(line);
        CpuTimes times;
        for (int column = 0; column < 8; ++column) { // guest time is already in user
            const std::string_view field = nextField(line);
            if (field.empty())
                break;
            const std::uint64_t value = u64Or0(field);
            times.total += value;
            if (column == 3 || column == 4)
                times.idle += value;
        }
        out.push_back(times);
    }
}

std::optional<double> cpuBusyPercent(const CpuTimes& before, const CpuTimes& after)
{
    if (after.total <= before.total)
        return std::nullopt;
    const std::uint64_t total = after.total - before.total;
    const std::uint64_t idle = after.idle >= before.idle ? after.idle - before.idle : 0;
    if (idle >= total)
        return 0.0;
    return 100.0 * static_cast<double>(total - idle) / static_cast<double>(total);
}

std::optional<std::uint64_t> meminfoBytes(std::string_view text, std::string_view key)
{
    while (!text.empty()) {
        std::string_view line = nextLine(text);
        if (line.size() <= key.size() || !line.starts_with(key) || line[key.size()] != ':')
            continue;
        line.remove_prefix(key.size() + 1);
        const auto value = parseU64(nextField(line));
        if (!value)
            return std::nullopt;
        return nextField(line) == "kB" ? *value * 1024 : *value;
    }
    return std::nullopt;
}

void parseDiskstats(std::string_view text, std::vector<DiskCounters>& out)
{
    out.clear();
    while (!text.empty()) {
        std::string_view line = nextLine(text);
        std::string_view fields[14];
        int count = 0;
        while (count < 14) {
            fields[count] = nextField(line);
            if (fields[count].empty())
                break;
            ++count;
        }
        if (count < 14)
            continue;
        out.push_back({fields[2], u64Or0(fields[5]), u64Or0(fields[9]), u64Or0(fields[12])});
    }
}

void parseNetDev(std::string_view text, std::vector<NetCounters>& out)
{
    out.clear();
    while (!text.empty()) {
        std::string_view line = nextLine(text);
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos)
            continue; // the two header lines
        const std::string_view name = trimmed(line.substr(0, colon));
        line.remove_prefix(colon + 1);
        std::string_view fields[9];
        int count = 0;
        while (count < 9) {
            fields[count] = nextField(line);
            if (fields[count].empty())
                break;
            ++count;
        }
        if (count < 9)
            continue;
        out.push_back({name, u64Or0(fields[0]), u64Or0(fields[8])});
    }
}

PressureValues parsePressure(std::string_view text)
{
    PressureValues out;
    while (!text.empty()) {
        std::string_view line = nextLine(text);
        const std::string_view kind = nextField(line);
        const std::string_view avg10 = nextField(line);
        if (!avg10.starts_with("avg10="))
            continue;
        double value = 0;
        const std::string_view number = avg10.substr(6);
        const auto [end, ec] = std::from_chars(number.data(), number.data() + number.size(), value);
        if (ec != std::errc{} || end != number.data() + number.size())
            continue;
        if (kind == "some")
            out.some10 = value;
        else if (kind == "full")
            out.full10 = value;
    }
    return out;
}

bool parsePidStat(std::string_view text, PidStat& out)
{
    // The name can hold anything, so it ends at the last ')'.
    const std::size_t open = text.find('(');
    const std::size_t close = text.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close < open)
        return false;
    out.comm = text.substr(open + 1, close - open - 1);

    std::string_view rest = text.substr(close + 1);
    // Field numbers from proc_pid_stat(5); the state is field 3.
    for (int field = 3; field <= 24; ++field) {
        const std::string_view value = nextField(rest);
        if (value.empty())
            return false;
        switch (field) {
        case 3: out.state = value[0]; break;
        case 4: out.ppid = static_cast<int>(parseI64(value).value_or(0)); break;
        case 14: out.utime = u64Or0(value); break;
        case 15: out.stime = u64Or0(value); break;
        case 19: out.nice = static_cast<int>(parseI64(value).value_or(0)); break;
        case 20: out.threads = static_cast<int>(parseI64(value).value_or(0)); break;
        case 22: out.startTicks = u64Or0(value); break;
        case 24: out.rssPages = parseI64(value).value_or(0); break;
        default: break;
        }
    }
    return true;
}

bool parsePidIo(std::string_view text, PidIo& out)
{
    bool haveRead = false;
    bool haveWrite = false;
    while (!text.empty()) {
        std::string_view line = nextLine(text);
        const std::string_view key = nextField(line);
        if (key == "read_bytes:") {
            out.readBytes = u64Or0(nextField(line));
            haveRead = true;
        } else if (key == "write_bytes:") {
            out.writeBytes = u64Or0(nextField(line));
            haveWrite = true;
        }
    }
    return haveRead && haveWrite;
}

std::vector<int> parseCpuList(std::string_view text)
{
    std::vector<int> out;
    text = trimmed(text);
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        const std::string_view part = text.substr(0, comma);
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
        const std::size_t dash = part.find('-');
        const auto first = parseI64(part.substr(0, dash));
        if (!first)
            continue;
        const auto last = dash == std::string_view::npos ? first : parseI64(part.substr(dash + 1));
        if (!last)
            continue;
        for (std::int64_t cpu = *first; cpu <= *last; ++cpu)
            out.push_back(static_cast<int>(cpu));
    }
    return out;
}

} // namespace fg
