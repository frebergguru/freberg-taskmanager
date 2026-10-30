// Records to a file with no window and no toolkit, for servers and scripts.
#pragma once

#include <string>

namespace fg {

// seconds <= 0 records until interrupted. Returns the process exit code.
int recordHeadless(const std::string& path, int seconds, int intervalMs);

} // namespace fg
