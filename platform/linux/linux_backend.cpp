#include "../backend.h"
#include "samplers.h"

#include <cerrno>
#include <charconv>
#include <climits>
#include <csignal>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <cstdlib>
#include <memory>
#include <pwd.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace fg {

namespace {

std::int64_t monotonicMs()
{
    timespec now{};
    ::clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

// Opens the sampler the first time it is needed, so unused subsystems cost nothing.
template <class T>
T& lazily(std::unique_ptr<T>& sampler, const std::string& root)
{
    if (!sampler)
        sampler = std::make_unique<T>(root);
    return *sampler;
}

class FdGuard {
public:
    explicit FdGuard(int fd) : m_fd(fd) {}
    ~FdGuard()
    {
        if (m_fd >= 0)
            ::close(m_fd);
    }
    int get() const { return m_fd; }

private:
    int m_fd;
};

class LinuxBackend final : public ISystemBackend {
public:
    explicit LinuxBackend(std::string root) : m_root(std::move(root)), m_stat(m_root + "/proc/stat") {}

    SnapshotPtr sample(Subsystems wanted) override
    {
        auto snapshot = std::make_shared<Snapshot>();
        const std::int64_t now = monotonicMs();
        snapshot->sequence = ++m_sequence;
        snapshot->monotonicMs = now;
        snapshot->collected = wanted;

        if (wanted & SubCpu)
            lazily(m_cpu, m_root).sample(snapshot->cpu);
        if (wanted & SubMemory)
            lazily(m_memory, m_root).sample(snapshot->memory);
        if (wanted & SubDisk)
            lazily(m_disk, m_root).sample(now, snapshot->disks);
        if (wanted & SubNetwork)
            lazily(m_network, m_root).sample(now, snapshot->networks);
        if (wanted & SubGpu)
            lazily(m_gpu, m_root).sample(snapshot->gpus);
        if (wanted & SubThermal)
            lazily(m_thermal, m_root).sample(snapshot->thermal);
        if (wanted & SubPower)
            lazily(m_power, m_root).sample(now, snapshot->power);
        if (wanted & SubPressure)
            lazily(m_pressure, m_root).sample(snapshot->pressure);
        if (wanted & SubProcesses)
            lazily(m_processes, m_root).sample(now, totalTicksDelta(), wanted, snapshot->processes);
        return snapshot;
    }

    const CpuTopology& topology() override
    {
        if (!m_topology) {
            CpuSampler sampler(m_root);
            m_topology = std::make_unique<CpuTopology>(sampler.readTopology());
        }
        return *m_topology;
    }

    SystemFacts systemFacts() override
    {
        SystemFacts facts;
        utsname names{};
        if (::uname(&names) == 0) {
            facts.hostName = names.nodename;
            facts.kernel = names.release;
            facts.architecture = names.machine;
        }

        const auto trimmedFile = [this](const std::string& path) {
            std::string text;
            return readFile(m_root + path, text) == ReadStatus::Ok ? std::string(trimmed(text)) : std::string();
        };
        const std::string dmi = "/sys/class/dmi/id/";
        facts.boardVendor = trimmedFile(dmi + "board_vendor");
        facts.boardName = trimmedFile(dmi + "board_name");
        facts.biosVersion = trimmedFile(dmi + "bios_version");
        facts.productName = trimmedFile(dmi + "product_name");

        std::string release;
        if (readFile(m_root + "/etc/os-release", release) == ReadStatus::Ok) {
            const std::size_t key = release.find("PRETTY_NAME=");
            if (key != std::string::npos) {
                const std::size_t end = release.find('\n', key);
                std::string value = release.substr(key + 12, end == std::string::npos ? end : end - key - 12);
                std::erase(value, '"');
                facts.osName = value;
            }
        }

        std::string uptime;
        const ReadStatus uptimeStatus = readFile(m_root + "/proc/uptime", uptime);
        // from_chars always reads a point. strtod follows the locale, and in Norwegian stops at it.
        double seconds = 0;
        const char* const uptimeEnd = uptime.data() + uptime.size();
        if (uptimeStatus != ReadStatus::Ok)
            facts.uptimeSeconds = Reading<double>::missing(toAvailability(uptimeStatus));
        else if (std::from_chars(uptime.data(), uptimeEnd, seconds).ec == std::errc{})
            facts.uptimeSeconds = Reading<double>::measured(seconds);
        else
            facts.uptimeSeconds = Reading<double>::missing(Availability::Unsupported);

        // Launched through sudo, pkexec or doas: name both the account and who asked.
        const uid_t uid = ::geteuid();
        const passwd* me = ::getpwuid(uid);
        facts.runningAs = me ? me->pw_name : std::to_string(uid);
        if (const char* sudo = std::getenv("SUDO_USER"); sudo && uid == 0)
            facts.runningAs += std::string(" (sudo: ") + sudo + ")";
        else if (const char* doas = std::getenv("DOAS_USER"); doas && uid == 0)
            facts.runningAs += std::string(" (doas: ") + doas + ")";
        else if (const char* pkexec = std::getenv("PKEXEC_UID"); pkexec && uid == 0) {
            const passwd* who = ::getpwuid(static_cast<uid_t>(std::strtoul(pkexec, nullptr, 10)));
            facts.runningAs += std::string(" (pkexec: ") + (who ? who->pw_name : pkexec) + ")";
        }

        const std::string drm = m_root + "/sys/class/drm/";
        if (DIR* cards = ::opendir(drm.c_str())) {
            while (const dirent* entry = ::readdir(cards)) {
                const std::string_view name = entry->d_name;
                if (name.starts_with("card") && name.find('-') == std::string_view::npos) {
                    char target[PATH_MAX];
                    const std::string link = drm + entry->d_name + "/device/driver";
                    const ssize_t size = ::readlink(link.c_str(), target, sizeof target);
                    std::string driver = size > 0 ? std::string(target, static_cast<std::size_t>(size)) : std::string();
                    if (const std::size_t slash = driver.rfind('/'); slash != std::string::npos)
                        driver.erase(0, slash + 1);
                    facts.gpus.push_back(std::string(name) + " (" + driver + ")");
                }
            }
            ::closedir(cards);
        }

        std::string table;
        const ReadStatus tableStatus = readFile(m_root + "/sys/firmware/dmi/tables/DMI", table);
        facts.memoryModules = tableStatus == ReadStatus::Ok ? Reading<std::vector<MemoryModule>>::measured(parseMemoryModules(table))
                                                            : Reading<std::vector<MemoryModule>>::missing(toAvailability(tableStatus));
        return facts;
    }

    PowerDetails powerDetails() override { return readPowerDetails(m_root); }
    std::vector<NetworkConnection> connections() override { return readConnections(m_root); }
    std::vector<KernelModule> kernelModules() override { return readKernelModules(m_root); }

    ActionResult signalProcess(const ProcessId& id, ProcessSignal signal) override
    {
        int number = SIGTERM;
        switch (signal) {
        case ProcessSignal::End: number = SIGTERM; break;
        case ProcessSignal::Kill: number = SIGKILL; break;
        case ProcessSignal::Suspend: number = SIGSTOP; break;
        case ProcessSignal::Resume: number = SIGCONT; break;
        }

        const FdGuard pidfd(static_cast<int>(::syscall(SYS_pidfd_open, id.pid, 0)));
        if (const ActionResult check = verified(pidfd, id); !check.ok())
            return check;
        if (::syscall(SYS_pidfd_send_signal, pidfd.get(), number, nullptr, 0) != 0)
            return failure();
        return {};
    }

    ActionResult setPriority(const ProcessId& id, int nice) override
    {
        const FdGuard pidfd(static_cast<int>(::syscall(SYS_pidfd_open, id.pid, 0)));
        if (const ActionResult check = verified(pidfd, id); !check.ok())
            return check;
        return forEachThread(id.pid, [nice](int thread) { return ::setpriority(PRIO_PROCESS, static_cast<id_t>(thread), nice); });
    }

    ActionResult setIoPriority(const ProcessId& id, IoPriority priority, int level) override
    {
        const FdGuard pidfd(static_cast<int>(::syscall(SYS_pidfd_open, id.pid, 0)));
        if (const ActionResult check = verified(pidfd, id); !check.ok())
            return check;
        constexpr int kWhoProcess = 1;
        constexpr int kClassShift = 13;
        const int ioClass = priority == IoPriority::Realtime ? 1 : (priority == IoPriority::BestEffort ? 2 : 3);
        return forEachThread(id.pid, [ioClass, level](int thread) { return static_cast<int>(::syscall(SYS_ioprio_set, kWhoProcess, thread, (ioClass << kClassShift) | level)); });
    }

    ActionResult setAffinity(const ProcessId& id, const std::vector<int>& cpus) override
    {
        const FdGuard pidfd(static_cast<int>(::syscall(SYS_pidfd_open, id.pid, 0)));
        if (const ActionResult check = verified(pidfd, id); !check.ok())
            return check;
        cpu_set_t set;
        CPU_ZERO(&set);
        for (const int cpu : cpus)
            if (cpu >= 0 && cpu < CPU_SETSIZE)
                CPU_SET(cpu, &set);
        return forEachThread(id.pid, [&set](int thread) { return ::sched_setaffinity(thread, sizeof set, &set); });
    }

    ActionResult currentAffinity(const ProcessId& id, std::vector<int>& cpus) override
    {
        const FdGuard pidfd(static_cast<int>(::syscall(SYS_pidfd_open, id.pid, 0)));
        if (const ActionResult check = verified(pidfd, id); !check.ok())
            return check;
        cpu_set_t set;
        if (::sched_getaffinity(id.pid, sizeof set, &set) != 0)
            return failure();
        cpus.clear();
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
            if (CPU_ISSET(cpu, &set))
                cpus.push_back(cpu);
        return {};
    }

    ActionResult processDetails(const ProcessId& id, ProcessDetails& out) override
    {
        const FdGuard pidfd(static_cast<int>(::syscall(SYS_pidfd_open, id.pid, 0)));
        if (const ActionResult check = verified(pidfd, id); !check.ok())
            return check;

        const std::string dir = m_root + "/proc/" + std::to_string(id.pid);
        constexpr std::size_t kMapsLimit = 256 * 1024;
        const auto stateOf = [](ReadStatus status) { return status == ReadStatus::Ok ? Availability::Measured : toAvailability(status); };
        out.statusState = stateOf(readFile(dir + "/status", out.status));
        out.limitsState = stateOf(readFile(dir + "/limits", out.limits));
        readFile(dir + "/cgroup", out.cgroup);
        out.memoryMapsState = stateOf(readFile(dir + "/maps", out.memoryMaps));
        if (out.memoryMaps.size() > kMapsLimit)
            out.memoryMaps.resize(kMapsLimit);
        out.environmentState = stateOf(readFile(dir + "/environ", out.environment));
        for (char& c : out.environment)
            if (c == '\0')
                c = '\n';

        if (DIR* fds = ::opendir((dir + "/fd").c_str())) {
            while (const dirent* entry = ::readdir(fds)) {
                if (entry->d_name[0] == '.')
                    continue;
                char target[PATH_MAX];
                const ssize_t size = ::readlink((dir + "/fd/" + entry->d_name).c_str(), target, sizeof target);
                if (size <= 0)
                    continue;
                std::string line(entry->d_name);
                line += ": ";
                line.append(target, static_cast<std::size_t>(size));
                (std::string_view(target, static_cast<std::size_t>(size)).starts_with("socket:") ? out.sockets : out.openFiles).push_back(std::move(line));
            }
            ::closedir(fds);
        }
        if (DIR* tasks = ::opendir((dir + "/task").c_str())) {
            std::string stat;
            while (const dirent* entry = ::readdir(tasks)) {
                if (entry->d_name[0] == '.')
                    continue;
                PidStat parsed;
                if (readFile(dir + "/task/" + entry->d_name + "/stat", stat) == ReadStatus::Ok && parsePidStat(stat, parsed)) {
                    std::string line(entry->d_name);
                    line += " ";
                    line += parsed.comm;
                    line += " [";
                    line += parsed.state;
                    line += "]";
                    out.threads.push_back(std::move(line));
                }
            }
            ::closedir(tasks);
        }
        return {};
    }

private:
    // Linux keeps priority and affinity per thread, and these calls change only the one
    // named. A thread that ended meanwhile is not a failure.
    template <class Apply>
    ActionResult forEachThread(int pid, Apply apply)
    {
        std::vector<int> threads;
        const std::string taskDir = m_root + "/proc/" + std::to_string(pid) + "/task/";
        if (DIR* tasks = ::opendir(taskDir.c_str())) {
            while (const dirent* entry = ::readdir(tasks))
                if (const auto thread = parseI64(entry->d_name))
                    threads.push_back(static_cast<int>(*thread));
            ::closedir(tasks);
        }
        if (threads.empty())
            return {ActionResult::ProcessGone, {}};
        std::size_t applied = 0;
        for (const int thread : threads) {
            // A thread number can only be reused once that thread ended. Skip one that no longer belongs here.
            if (::access((taskDir + std::to_string(thread)).c_str(), F_OK) != 0)
                continue;
            if (apply(thread) != 0 && errno != ESRCH) {
                ActionResult result = failure();
                if (applied > 0)
                    result.detail += " (" + std::to_string(applied) + " of " + std::to_string(threads.size()) + " threads were already changed)";
                return result;
            }
            ++applied;
        }
        return {};
    }

    // Taking the pidfd first pins the process, so the start time check cannot go stale.
    ActionResult verified(const FdGuard& pidfd, const ProcessId& id)
    {
        if (pidfd.get() < 0)
            return errno == ESRCH ? ActionResult{ActionResult::ProcessGone, {}} : failure();
        if (lazily(m_processes, m_root).currentStartTicks(id.pid) != id.startTicks)
            return {ActionResult::ProcessGone, "this process ID now belongs to another process"};
        return {};
    }

    static ActionResult failure()
    {
        const int error = errno;
        const ActionResult::Code code = error == EPERM || error == EACCES ? ActionResult::AccessDenied
            : error == ESRCH                                              ? ActionResult::ProcessGone
                                                                          : ActionResult::Failed;
        return {code, std::strerror(error)};
    }

    // Process CPU shares need the whole machine's ticks over the same interval.
    std::uint64_t totalTicksDelta()
    {
        if (m_stat.read(m_buffer) != ReadStatus::Ok)
            return 0;
        parseCpuStat(m_buffer, m_ticks);
        if (m_ticks.empty())
            return 0;
        const std::uint64_t total = m_ticks[0].total;
        const std::uint64_t delta = total > m_previousTotalTicks && m_previousTotalTicks != 0 ? total - m_previousTotalTicks : 0;
        m_previousTotalTicks = total;
        return delta;
    }

    std::string m_root;
    CachedFile m_stat;
    std::uint64_t m_sequence = 0;
    std::unique_ptr<CpuSampler> m_cpu;
    std::unique_ptr<MemorySampler> m_memory;
    std::unique_ptr<DiskSampler> m_disk;
    std::unique_ptr<NetworkSampler> m_network;
    std::unique_ptr<GpuSampler> m_gpu;
    std::unique_ptr<ThermalSampler> m_thermal;
    std::unique_ptr<PowerSampler> m_power;
    std::unique_ptr<PressureSampler> m_pressure;
    std::unique_ptr<ProcessSampler> m_processes;
    std::unique_ptr<CpuTopology> m_topology;
    std::string m_buffer;
    std::vector<CpuTimes> m_ticks;
    std::uint64_t m_previousTotalTicks = 0;
};

} // namespace

std::unique_ptr<ISystemBackend> createLinuxBackend(const std::string& root)
{
    return std::make_unique<LinuxBackend>(root);
}

} // namespace fg
