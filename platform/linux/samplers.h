// One sampler per subsystem. Each one owns its previous counters and open files,
// and is constructed the first time it is asked for, so unused ones cost nothing.
#pragma once

#include "../../core/parse.h"
#include "../backend.h"
#include "../../core/snapshot.h"
#include "file.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace fg {

struct WifiLink {
    Reading<std::string> network;
    Reading<double> sendMbps;
};
WifiLink readWifiLink(const std::string& interfaceName);

PowerDetails readPowerDetails(const std::string& root);
std::vector<NetworkConnection> readConnections(const std::string& root);
std::vector<KernelModule> readKernelModules(const std::string& root);

class CpuSampler {
public:
    explicit CpuSampler(const std::string& root);
    void sample(CpuSample& out);
    CpuTopology readTopology() const;

private:
    std::string m_root;
    CachedFile m_stat;
    std::vector<CachedFile> m_freq;
    std::vector<CpuTimes> m_previous, m_current;
    std::string m_buffer;
};

class MemorySampler {
public:
    explicit MemorySampler(const std::string& root);
    void sample(MemorySample& out);

private:
    std::string m_root;
    CachedFile m_meminfo;
    CachedFile m_vmstat;
    std::vector<CachedFile> m_zram;
    std::uint64_t m_passes = 0;
    std::string m_buffer;
};

class DiskSampler {
public:
    explicit DiskSampler(const std::string& root);
    void sample(std::int64_t nowMs, std::vector<DiskSample>& out);

private:
    struct Previous {
        std::uint64_t sectorsRead, sectorsWritten, ioTicksMs;
    };
    struct Identity {
        std::string model;
        bool physical = false, rotational = false, removable = false;
        CachedFile size;
    };
    Identity& identity(const std::string& name);
    void refreshFilesystems();

    std::string m_root;
    CachedFile m_diskstats;
    std::string m_buffer, m_sizeText;
    std::vector<DiskCounters> m_counters;
    std::unordered_map<std::string, Previous> m_previous;
    std::unordered_map<std::string, Identity> m_identity;
    std::int64_t m_previousMs = 0;
    std::int64_t m_filesystemsAtMs = 0;
    struct Filesystems {
        std::uint64_t total = 0, free = 0;
        bool measured = false; // a mount we could not ask about is not 0 bytes
        std::vector<std::string> mounts;
    };
    std::unordered_map<std::string, Filesystems> m_filesystems;
};

class NetworkSampler {
public:
    explicit NetworkSampler(const std::string& root);
    void sample(std::int64_t nowMs, std::vector<NetworkSample>& out);

private:
    struct Previous {
        std::uint64_t rx, tx;
    };
    // What stays open per interface. The type and the kind of device never change.
    struct Face {
        CachedFile address, operState, speed;
        bool loopback = false, wireless = false, hasDevice = false;
    };
    std::string m_root;
    CachedFile m_netdev;
    CachedFile m_wireless;
    std::string m_buffer, m_wirelessText, m_faceText; // m_counters' names point into m_buffer
    std::vector<NetCounters> m_counters;
    std::unordered_map<std::string, Previous> m_previous;
    std::unordered_map<std::string, Face> m_faces;
    std::int64_t m_previousMs = 0;
    struct KnownWifi {
        WifiLink link;
        std::int64_t atMs = 0;
    };
    std::unordered_map<std::string, KnownWifi> m_wifi;
};

class GpuSampler {
public:
    explicit GpuSampler(const std::string& root);
    void sample(std::vector<GpuSample>& out);

private:
    struct Card {
        std::string name, driver;
        bool hasHwmon = false;
        CachedFile busy, vramUsed, vramTotal, gtFreq, temp, powerAverage, powerInput, hwmonFreq;
    };
    std::string m_root;
    std::vector<Card> m_cards;
    std::string m_buffer;
};

class ThermalSampler {
public:
    explicit ThermalSampler(const std::string& root);
    void sample(ThermalSample& out);

private:
    struct Sensor {
        std::string chip, label;
        CachedFile input;
        bool cpu = false;
        Reading<double> limitC;
    };
    struct Fan {
        std::string chip, label;
        CachedFile input, duty;
        bool hasDuty = false;
    };
    std::string m_root;
    std::vector<Sensor> m_sensors;
    std::vector<Fan> m_fans;
    std::vector<CachedFile> m_throttle; // one per CPU package
    std::string m_buffer;
};

class PowerSampler {
public:
    explicit PowerSampler(const std::string& root);
    void sample(std::int64_t nowMs, PowerSample& out);

private:
    // One per CPU package.
    struct RaplDomain {
        CachedFile energy;
        std::uint64_t range = 0;
        std::uint64_t previous = 0;
        bool havePrevious = false;
    };
    struct Supply {
        std::string name;
        bool isMains = false;
        CachedFile online, status, capacity, powerNow, currentNow, voltageNow;
    };
    void findSupplies();
    std::string readText(CachedFile& file);

    std::string m_root;
    std::string m_buffer;
    std::vector<RaplDomain> m_rapl;
    std::int64_t m_previousMs = 0;
    std::vector<Supply> m_supplies;
    std::uint64_t m_passes = 0;
    CachedFile m_governor, m_energyPreference, m_platformProfile;
};

class PressureSampler {
public:
    explicit PressureSampler(const std::string& root);
    void sample(PressureSample& out);

private:
    CachedFile m_cpu, m_memory, m_io;
    std::string m_buffer;
};

class ProcessSampler {
public:
    explicit ProcessSampler(const std::string& root);
    void sample(std::int64_t nowMs, std::uint64_t totalCpuTicksDelta, Subsystems wanted, std::vector<ProcessSample>& out);

    // Start time of a PID right now, or 0 when it is gone.
    std::uint64_t currentStartTicks(int pid);

private:
    struct Known {
        std::shared_ptr<const ProcessInfo> info;
        std::string kernelName; // as the kernel has it, which info->name may extend
        std::uint64_t cpuTicks = 0;
        std::uint64_t readBytes = 0, writeBytes = 0;
        bool haveIo = false;
        std::uint64_t gpuNs = 0;
        bool haveGpu = false;
        std::uint64_t networkBytes = 0;
        bool haveNetwork = false;
        std::uint64_t seenAt = 0;
        std::int64_t atMs = 0;
    };
    std::shared_ptr<const ProcessInfo> readInfo(int pid, const PidStat& stat);
    Reading<double> gpuShare(int pid, Known& known, double seconds);

    std::string m_root;
    std::string m_buffer;
    std::unordered_map<int, Known> m_known;
    std::uint64_t m_pass = 0;
    std::int64_t m_previousPassMs = 0;
    long m_pageSize;
};

} // namespace fg
