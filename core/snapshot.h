// One immutable sample of the system. Produced on the sampler thread and handed
// to the UI as a shared pointer, so nothing here is ever changed after publishing.
#pragma once

#include "reading.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fg {

enum Subsystem : unsigned {
    SubNone = 0,
    SubCpu = 1u << 0,
    SubMemory = 1u << 1,
    SubDisk = 1u << 2,
    SubNetwork = 1u << 3,
    SubGpu = 1u << 4,
    SubThermal = 1u << 5,
    SubPower = 1u << 6,
    SubPressure = 1u << 7,
    SubProcesses = 1u << 8,
    SubAll = (1u << 9) - 1,
    // Costly extras, kept out of SubAll. They are read only while their column is shown.
    SubProcessGpu = 1u << 9,
    SubProcessNetwork = 1u << 10,
};
using Subsystems = unsigned;

// A PID alone can be reused. With the kernel's start time it names one process.
struct ProcessId {
    int pid = 0;
    std::uint64_t startTicks = 0;
    bool operator==(const ProcessId&) const = default;
};

struct CpuCoreSample {
    Reading<double> usagePct;
    Reading<double> freqMHz;
};

struct CpuSample {
    Reading<double> usagePct;
    Reading<double> freqMHz; // average across logical processors
    std::vector<CpuCoreSample> cores;
};

struct MemorySample {
    Reading<std::uint64_t> total, used, available, free, cached, buffers, shared;
    Reading<std::uint64_t> swapTotal, swapUsed;
    Reading<std::uint64_t> zswapStored, zswapCompressed;
    Reading<std::uint64_t> zramOriginal, zramCompressed;
    Reading<std::uint64_t> oomKills; // since boot
};

struct DiskSample {
    std::string name;
    std::string model;
    bool rotational = false;
    bool removable = false;
    Reading<std::uint64_t> sizeBytes;
    Reading<double> activePct;
    Reading<double> readBps, writeBps;
    Reading<std::uint64_t> fsTotal, fsFree; // summed over its mounted filesystems
    std::vector<std::string> mounts;
};

struct NetworkSample {
    std::string name;
    std::string mac;
    std::string operState;
    bool wireless = false;
    bool loopback = false;
    Reading<double> linkMbps;
    Reading<double> rxBps, txBps;
    std::uint64_t rxTotal = 0, txTotal = 0;
    Reading<double> wifiSignalDbm;
    Reading<double> wifiQualityPct;
};

struct GpuSample {
    std::string card;
    std::string driver;
    Reading<double> busyPct;
    Reading<double> freqMHz;
    Reading<std::uint64_t> vramUsed, vramTotal;
    Reading<double> tempC;
    Reading<double> powerW;
};

struct ThermalSensor {
    std::string chip;
    std::string label;
    Reading<double> tempC;
};

struct ThermalSample {
    Reading<double> cpuHotspotC;
    Reading<std::uint64_t> throttleEvents; // thermal pressure, where the kernel counts it
    std::vector<ThermalSensor> sensors;
};

struct BatterySample {
    std::string name;
    std::string status;
    Reading<double> percent;
    Reading<double> powerW;
};

struct PowerSample {
    Reading<double> cpuPackageW;
    Reading<bool> onAc;
    Reading<std::string> governor;
    Reading<std::string> energyPreference;
    Reading<std::string> platformProfile;
    std::vector<BatterySample> batteries;
};

struct PressureLine {
    Reading<double> some10, full10;
};

struct PressureSample {
    PressureLine cpu, memory, io;
};

// Facts that do not change while a process lives. Shared between samples.
struct ProcessInfo {
    ProcessId id;
    std::string name;
    std::string commandLine;
    std::string exePath;
    unsigned uid = 0;
    Reading<std::string> cgroup;
};

struct ProcessSample {
    std::shared_ptr<const ProcessInfo> info;
    int ppid = 0;
    char state = '?';
    int threads = 0;
    int nice = 0;
    Reading<double> cpuPct; // share of the whole machine
    Reading<std::uint64_t> rssBytes;
    Reading<double> readBps, writeBps;
    Reading<double> gpuPct;     // only with SubProcessGpu
    Reading<double> networkBps; // only with SubProcessNetwork; TCP only
};

struct Snapshot {
    std::uint64_t sequence = 0;
    std::int64_t monotonicMs = 0;
    Subsystems collected = SubNone;

    CpuSample cpu;
    MemorySample memory;
    std::vector<DiskSample> disks;
    std::vector<NetworkSample> networks;
    std::vector<GpuSample> gpus;
    ThermalSample thermal;
    PowerSample power;
    PressureSample pressure;
    std::vector<ProcessSample> processes;
};

using SnapshotPtr = std::shared_ptr<const Snapshot>;

// Facts about the processor, read once.
struct CpuTopology {
    std::string model;
    int logical = 0;
    int physical = 0;
    int numaNodes = 0;
    std::vector<int> coreNode;     // logical processor -> NUMA node
    std::vector<bool> efficiency;  // logical processor -> efficiency core
    bool hybrid = false;
    Reading<double> maxFreqMHz;
};

} // namespace fg
