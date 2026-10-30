// What the app asks of an operating system. The UI talks only to this, so another
// system's backend can be added without touching the views.
#pragma once

#include "../core/smbios.h"
#include "../core/snapshot.h"

#include <memory>
#include <string>
#include <vector>

namespace fg {

enum class ProcessSignal { End, Kill, Suspend, Resume };

struct ActionResult {
    enum Code { Ok, ProcessGone, AccessDenied, Failed } code = Ok;
    std::string detail;
    bool ok() const { return code == Ok; }
};

enum class IoPriority { Idle, BestEffort, Realtime };

// Everything the inspector shows. Read only when asked, never on a timer.
struct ProcessDetails {
    std::string status;
    std::string limits;
    std::string environment;
    std::string memoryMaps;
    std::string cgroup;
    std::vector<std::string> openFiles;
    std::vector<std::string> sockets;
    std::vector<std::string> threads;
};

// The facts on the System Info page. Read when the page opens.
struct SystemFacts {
    std::string hostName, osName, kernel, architecture;
    std::string boardVendor, boardName, biosVersion, productName;
    std::string runningAs;
    double uptimeSeconds = 0;
    std::vector<std::string> gpus;
    Reading<std::vector<MemoryModule>> memoryModules;
};

struct NetworkConnection {
    std::string protocol; // tcp, tcp6, udp, udp6, unix
    std::string local, remote;
    std::string state;
    unsigned uid = 0;
    ProcessId owner;          // pid 0 when the owner is hidden from us
    std::string ownerName;
    Reading<std::uint64_t> sent, received; // TCP only; other protocols do not count
};

struct IdleState {
    std::string name;
    std::uint64_t timeUs = 0;
};

// Slow-changing power facts for the Power & Freq page.
struct PowerDetails {
    std::vector<IdleState> idleStates; // summed over all logical processors
    Reading<std::vector<std::string>> availableGovernors;
    Reading<double> minFreqMHz, maxFreqMHz;
    Reading<std::string> boost;
};

struct KernelModule {
    std::string name;
    std::uint64_t sizeBytes = 0;
    int useCount = 0;
    std::string usedBy;
    std::string state;
    std::vector<std::string> devices;
};

class ISystemBackend {
public:
    virtual ~ISystemBackend() = default;

    // Reads only the requested subsystems. Costs nothing for the others.
    virtual SnapshotPtr sample(Subsystems wanted) = 0;

    virtual const CpuTopology& topology() = 0;
    virtual SystemFacts systemFacts() = 0;
    virtual PowerDetails powerDetails() = 0;
    virtual std::vector<NetworkConnection> connections() = 0;
    virtual std::vector<KernelModule> kernelModules() = 0;

    // Refuses when the PID now belongs to a different process than the one given.
    virtual ActionResult signalProcess(const ProcessId& id, ProcessSignal signal) = 0;
    virtual ActionResult setPriority(const ProcessId& id, int nice) = 0;
    virtual ActionResult setIoPriority(const ProcessId& id, IoPriority priority, int level) = 0;
    virtual ActionResult setAffinity(const ProcessId& id, const std::vector<int>& cpus) = 0;
    virtual ActionResult currentAffinity(const ProcessId& id, std::vector<int>& cpus) = 0;
    virtual ActionResult processDetails(const ProcessId& id, ProcessDetails& out) = 0;
};

// root is prepended to every /proc and /sys path. Tests pass a fake tree.
std::unique_ptr<ISystemBackend> createLinuxBackend(const std::string& root = {});

} // namespace fg
