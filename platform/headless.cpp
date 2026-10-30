#include "headless.h"

#include "../core/trace.h"
#include "backend.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <ctime>

namespace fg {

namespace {

std::atomic<bool> g_stop{false};

void requestStop(int)
{
    g_stop = true;
}

} // namespace

int recordHeadless(const std::string& path, int seconds, int intervalMs)
{
    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);

    const auto backend = createLinuxBackend();
    TraceBuffer buffer;
    const long long wanted = seconds > 0 ? static_cast<long long>(seconds) * 1000 / intervalMs : -1;
    for (long long taken = 0; !g_stop && (wanted < 0 || taken <= wanted); ++taken) {
        buffer.add(frameFromSnapshot(*backend->sample(SubAll)));
        timespec pause{intervalMs / 1000, (intervalMs % 1000) * 1000000L};
        ::nanosleep(&pause, nullptr); // a signal cuts this short, and the loop then ends
    }

    std::string error;
    if (!saveTrace(path, buffer.frames(), error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    return 0;
}

} // namespace fg
