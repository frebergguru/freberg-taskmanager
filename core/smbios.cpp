#include "smbios.h"

#include <algorithm>

namespace fg {

namespace {

constexpr unsigned char kMemoryDevice = 17;
constexpr unsigned char kEndOfTable = 127;

unsigned word(std::string_view s, std::size_t at)
{
    if (at + 2 > s.size())
        return 0;
    return static_cast<unsigned char>(s[at]) | (static_cast<unsigned>(static_cast<unsigned char>(s[at + 1])) << 8);
}

std::uint32_t dword(std::string_view s, std::size_t at)
{
    if (at + 4 > s.size())
        return 0;
    return word(s, at) | (static_cast<std::uint32_t>(word(s, at + 2)) << 16);
}

std::string formFactorName(unsigned value)
{
    switch (value) {
    case 0x09: return "DIMM";
    case 0x0D: return "SODIMM";
    case 0x0B: return "Row of chips";
    case 0x0F: return "FB-DIMM";
    default: return {};
    }
}

std::string typeName(unsigned value)
{
    switch (value) {
    case 0x12: return "DDR";
    case 0x13: return "DDR2";
    case 0x18: return "DDR3";
    case 0x1A: return "DDR4";
    case 0x1D: return "LPDDR3";
    case 0x1E: return "LPDDR4";
    case 0x22: return "DDR5";
    case 0x23: return "LPDDR5";
    default: return {};
    }
}

// Text fields hold a one-based index into the text after the fixed part. Zero means none.
std::string textAt(std::string_view strings, unsigned wanted)
{
    for (unsigned index = 1; wanted != 0 && !strings.empty(); ++index) {
        const std::size_t end = strings.find('\0');
        if (index == wanted)
            return std::string(strings.substr(0, end));
        if (end == std::string_view::npos)
            break;
        strings.remove_prefix(end + 1);
    }
    return {};
}

// Some firmware gives every module the same locator, and only the bank tells them apart.
// A bank shared as well, such as "Not Specified", would add nothing, so it is left out.
void nameModules(std::vector<MemoryModule>& modules)
{
    for (MemoryModule& module : modules) {
        const auto sameLocator = [&module](const MemoryModule& other) { return other.locator == module.locator; };
        const auto sameBank = [&module](const MemoryModule& other) { return other.locator == module.locator && other.bank == module.bank; };
        const bool needsBank = !module.bank.empty() && std::ranges::count_if(modules, sameLocator) > 1 && std::ranges::count_if(modules, sameBank) == 1;
        module.name = !needsBank ? module.locator : module.locator.empty() ? module.bank : module.bank + ", " + module.locator;
    }
}

} // namespace

std::vector<MemoryModule> parseMemoryModules(std::string_view table)
{
    std::vector<MemoryModule> out;
    std::size_t at = 0;
    while (at + 4 <= table.size()) {
        const auto type = static_cast<unsigned char>(table[at]);
        const std::size_t length = static_cast<unsigned char>(table[at + 1]);
        if (length < 4 || at + length > table.size())
            break;

        // Text follows the fixed part, and a double zero ends it.
        std::size_t stringsEnd = at + length;
        while (stringsEnd + 1 < table.size() && (table[stringsEnd] != '\0' || table[stringsEnd + 1] != '\0'))
            ++stringsEnd;
        const std::string_view strings = table.substr(at + length, stringsEnd - (at + length));

        if (type == kEndOfTable)
            break;
        if (type == kMemoryDevice && length >= 0x15) {
            const std::string_view s = table.substr(at, length);
            MemoryModule module;
            const unsigned size = word(s, 0x0C);
            if (size == 0x7FFF)
                module.sizeBytes = static_cast<std::uint64_t>(dword(s, 0x1C)) * 1024 * 1024;
            else if (size == 0xFFFF)
                module.sizeKnown = false;
            else if (size & 0x8000)
                module.sizeBytes = static_cast<std::uint64_t>(size & 0x7FFF) * 1024;
            else
                module.sizeBytes = static_cast<std::uint64_t>(size) * 1024 * 1024;
            module.formFactor = formFactorName(static_cast<unsigned char>(s[0x0E]));
            module.type = typeName(static_cast<unsigned char>(s[0x12]));
            if (length > 0x16)
                module.speedMTs = word(s, 0x15);
            module.locator = textAt(strings, static_cast<unsigned char>(s[0x10]));
            module.bank = textAt(strings, static_cast<unsigned char>(s[0x11]));
            out.push_back(std::move(module));
        }
        at = stringsEnd + 2;
    }
    nameModules(out);
    return out;
}

} // namespace fg
