// Reads memory module facts out of a raw SMBIOS table.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fg {

struct MemoryModule {
    std::string locator;
    std::string formFactor;
    std::string type;
    std::uint64_t sizeBytes = 0; // 0 means the slot is empty
    unsigned speedMTs = 0;       // 0 means not reported
};

std::vector<MemoryModule> parseMemoryModules(std::string_view table);

} // namespace fg
