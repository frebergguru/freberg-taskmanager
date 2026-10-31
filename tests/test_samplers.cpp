#include "core/diskscan.h"
#include "platform/backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <csignal>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace fg;

class TestSamplers : public QObject {
    Q_OBJECT

    // Fixtures live next to the test binary, not in the small shared /tmp.
    std::unique_ptr<QTemporaryDir> m_dir;

    void put(const QString& relative, const QByteArray& text)
    {
        const QString path = m_dir->path() + relative;
        QDir().mkpath(QFileInfo(path).path());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(text);
    }

    std::unique_ptr<ISystemBackend> backend() { return createLinuxBackend(m_dir->path().toStdString()); }

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>(QCoreApplication::applicationDirPath() + "/fixture-XXXXXX");
        QVERIFY(m_dir->isValid());
    }

    void emptySystemReportsNoZeros()
    {
        const auto snapshot = backend()->sample(SubAll);
        QCOMPARE(snapshot->cpu.usagePct.state, Availability::Unsupported);
        QCOMPARE(snapshot->memory.total.state, Availability::Unsupported);
        QCOMPARE(snapshot->power.cpuPackageW.state, Availability::Unsupported);
        QCOMPARE(snapshot->power.onAc.state, Availability::Unsupported);
        QCOMPARE(snapshot->thermal.cpuHotspotC.state, Availability::Unsupported);
        QCOMPARE(snapshot->pressure.cpu.some10.state, Availability::Unsupported);
        QVERIFY(snapshot->disks.empty());
    }

    void onlyRequestedSubsystemsAreRead()
    {
        put("/proc/meminfo", "MemTotal: 100 kB\nMemAvailable: 40 kB\n");
        const auto snapshot = backend()->sample(SubMemory);
        QCOMPARE(snapshot->collected, Subsystems(SubMemory));
        QVERIFY(snapshot->memory.total.known());
        QCOMPARE(snapshot->cpu.usagePct.state, Availability::NotYetSampled);
    }

    void memoryUsedIsTotalMinusAvailable()
    {
        put("/proc/meminfo", "MemTotal: 100 kB\nMemAvailable: 40 kB\nSwapTotal: 10 kB\nSwapFree: 4 kB\n");
        const auto snapshot = backend()->sample(SubMemory);
        QCOMPARE(snapshot->memory.used.value, std::uint64_t(60 * 1024));
        QCOMPARE(snapshot->memory.swapUsed.value, std::uint64_t(6 * 1024));
        QCOMPARE(snapshot->memory.zswapStored.state, Availability::Unsupported);
    }

    void cpuRateNeedsTwoSamples()
    {
        put("/proc/stat", "cpu  100 0 0 100 0 0 0 0 0 0\ncpu0 100 0 0 100 0 0 0 0 0 0\n");
        auto system = backend();
        QCOMPARE(system->sample(SubCpu)->cpu.usagePct.state, Availability::NotYetSampled);
        put("/proc/stat", "cpu  150 0 0 150 0 0 0 0 0 0\ncpu0 150 0 0 150 0 0 0 0 0 0\n");
        const auto second = system->sample(SubCpu);
        QVERIFY(second->cpu.usagePct.known());
        QCOMPARE(second->cpu.usagePct.value, 50.0);
        QCOMPARE(second->cpu.cores.size(), std::size_t(1));
        QCOMPARE(second->cpu.cores[0].freqMHz.state, Availability::Unsupported);
    }

    void deniedRaplIsAccessDeniedNotZero()
    {
        if (::geteuid() == 0)
            QSKIP("root can read everything");
        put("/sys/class/powercap/intel-rapl:0/energy_uj", "1000");
        put("/sys/class/powercap/intel-rapl:0:0/energy_uj", "1000");
        ::chmod((m_dir->path() + "/sys/class/powercap/intel-rapl:0/energy_uj").toUtf8().constData(), 0);
        const auto snapshot = backend()->sample(SubPower);
        QCOMPARE(snapshot->power.cpuPackageW.state, Availability::AccessDenied);
    }

    void raplCounterWrapIsHandled()
    {
        put("/sys/class/powercap/intel-rapl:0/max_energy_range_uj", "1000000");
        put("/sys/class/powercap/intel-rapl:0/energy_uj", "999000");
        auto system = backend();
        QCOMPARE(system->sample(SubPower)->power.cpuPackageW.state, Availability::NotYetSampled);
        put("/sys/class/powercap/intel-rapl:0/energy_uj", "1000");
        QTest::qWait(50);
        const auto second = system->sample(SubPower);
        QVERIFY(second->power.cpuPackageW.known());
        QVERIFY(second->power.cpuPackageW.value > 0);
    }

    void disksSkipVirtualDevices()
    {
        put("/proc/diskstats",
            " 7 0 loop0 1 0 2 0 1 0 2 0 0 1 1 0 0 0 0 0 0\n"
            " 259 0 nvme0n1 10 0 200 5 20 0 400 8 0 77 90 0 0 0 0 0 0\n");
        put("/sys/block/nvme0n1/size", "1000\n");
        put("/sys/block/nvme0n1/device/model", "TestDisk 1TB \n");
        put("/sys/block/loop0/size", "10\n");
        const auto snapshot = backend()->sample(SubDisk);
        QCOMPARE(snapshot->disks.size(), std::size_t(1));
        QCOMPARE(snapshot->disks[0].model, std::string("TestDisk 1TB"));
        QCOMPARE(snapshot->disks[0].sizeBytes.value, std::uint64_t(512000));
        QCOMPARE(snapshot->disks[0].readBps.state, Availability::NotYetSampled);
    }

    void wifiLinkSpeedIsNotZero()
    {
        put("/proc/net/dev", "h\nh\n  wlan0: 1 0 0 0 0 0 0 0 2 0 0 0 0 0 0 0\n");
        put("/sys/class/net/wlan0/speed", "-1\n");
        put("/sys/class/net/wlan0/wireless/x", "");
        const auto snapshot = backend()->sample(SubNetwork);
        QCOMPARE(snapshot->networks.size(), std::size_t(1));
        QVERIFY(snapshot->networks[0].wireless);
        QCOMPARE(snapshot->networks[0].linkMbps.state, Availability::Unsupported);
    }

    void processCpuShareAndPidReuse()
    {
        const auto statLine = [](const char* name, int ticks, int start) {
            return QStringLiteral("42 (%1) S 1 42 42 0 -1 0 0 0 0 0 %2 0 0 0 20 0 1 0 %3 1000 10 0 0").arg(name).arg(ticks).arg(start).toUtf8();
        };
        put("/proc/stat", "cpu  1000 0 0 0 0 0 0 0 0 0\n");
        put("/proc/42/stat", statLine("first", 100, 500));
        auto system = backend();
        system->sample(SubProcesses);

        put("/proc/stat", "cpu  2000 0 0 0 0 0 0 0 0 0\n");
        put("/proc/42/stat", statLine("first", 350, 500));
        auto snapshot = system->sample(SubProcesses);
        QCOMPARE(snapshot->processes.size(), std::size_t(1));
        QCOMPARE(snapshot->processes[0].cpuPct.value, 25.0);

        // Same PID, new start time: a new process, so nothing carries over. All it has
        // used so far falls in this interval, so that is its share.
        put("/proc/stat", "cpu  3000 0 0 0 0 0 0 0 0 0\n");
        put("/proc/42/stat", statLine("second", 900, 600));
        snapshot = system->sample(SubProcesses);
        QCOMPARE(snapshot->processes[0].info->name, std::string("second"));
        QCOMPARE(snapshot->processes[0].info->id.startTicks, std::uint64_t(600));
        QCOMPARE(snapshot->processes[0].cpuPct.value, 90.0);
    }

    void firstPassHasNoShareYet()
    {
        put("/proc/stat", "cpu  1000 0 0 0 0 0 0 0 0 0\n");
        put("/proc/42/stat", "42 (old) S 1 42 42 0 -1 0 0 0 0 0 100 0 0 0 20 0 1 0 500 1000 10 0 0");
        QCOMPARE(backend()->sample(SubProcesses)->processes[0].cpuPct.state, Availability::NotYetSampled);
    }

    void longNamesComeFromTheCommandLine()
    {
        const auto add = [this](int pid, const char* name, const QByteArray& command) {
            put(QStringLiteral("/proc/%1/stat").arg(pid), QByteArray::number(pid) + " (" + name + ") S 1 1 1 0 -1 0 0 0 0 0 1 1 0 0 20 0 1 0 77 0 5 0 0 0\n");
            put(QStringLiteral("/proc/%1/cmdline").arg(pid), command);
        };
        add(11, "systemd-userwor", QByteArray("/usr/lib/systemd/systemd-userwork\0x\0", 36));
        add(12, "a-long-script-n", QByteArray("python3\0/opt/a-long-script-name.py\0", 35));
        add(13, "fifteen-letters", QByteArray("something-else\0", 15));
        add(14, "systemd-userwor", QByteArray("systemd-userwork: waiting...\0", 29));
        auto list = backend()->sample(SubProcesses)->processes;
        std::sort(list.begin(), list.end(), [](const ProcessSample& a, const ProcessSample& b) { return a.info->id.pid < b.info->id.pid; });
        QCOMPARE(list.at(0).info->name, std::string("systemd-userwork"));
        QCOMPARE(list.at(1).info->name, std::string("a-long-script-name.py"));
        QCOMPARE(list.at(2).info->name, std::string("fifteen-letters"));
        QCOMPARE(list.at(3).info->name, std::string("systemd-userwork"));
    }

    void refusesToSignalReusedPid()
    {
        QProcess sleeper;
        sleeper.start("sleep", {"60"});
        QVERIFY(sleeper.waitForStarted());
        const int pid = static_cast<int>(sleeper.processId());
        auto system = createLinuxBackend();

        ProcessId id{pid, 0};
        const auto snapshot = system->sample(SubProcesses);
        for (const ProcessSample& process : snapshot->processes)
            if (process.info->id.pid == pid)
                id = process.info->id;
        QVERIFY(id.startTicks != 0);

        // An identity with the wrong start time stands for a process that is gone.
        const ProcessId stale{pid, id.startTicks + 1};
        QCOMPARE(system->signalProcess(stale, ProcessSignal::Kill).code, ActionResult::ProcessGone);
        QVERIFY(!sleeper.waitForFinished(100));

        QVERIFY(system->signalProcess(id, ProcessSignal::Kill).ok());
        QVERIFY(sleeper.waitForFinished(2000));
    }

    void diskScanAddsUpAndSorts()
    {
        put("/tree/small.txt", QByteArray(100, 'x'));
        put("/tree/big/large.bin", QByteArray(200000, 'y'));
        std::atomic<bool> cancel{false};
        const DiskScanResult result = scanFolder((m_dir->path() + "/tree").toStdString(), cancel);
        QVERIFY(!result.root->denied);
        QCOMPARE(result.root->files, std::uint64_t(2));
        QCOMPARE(result.root->children.size(), std::size_t(2));
        QCOMPARE(result.root->children[0]->name, std::string("big")); // largest first
        QVERIFY(result.root->bytes >= 200000);
        QCOMPARE(nodePath(*result.root->children[0]->children[0]), (m_dir->path() + "/tree/big/large.bin").toStdString());
    }

    void diskScanCountsHardLinksOnce()
    {
        put("/tree/one.bin", QByteArray(200000, 'y'));
        QVERIFY(::link((m_dir->path() + "/tree/one.bin").toUtf8().constData(), (m_dir->path() + "/tree/two.bin").toUtf8().constData()) == 0);
        std::atomic<bool> cancel{false};
        const DiskScanResult result = scanFolder((m_dir->path() + "/tree").toStdString(), cancel);
        QCOMPARE(result.root->files, std::uint64_t(2));
        QVERIFY(result.root->bytes < 300000);
    }

    void diskScanCancelsAndReportsMissing()
    {
        put("/tree/a.txt", "a");
        std::atomic<bool> cancel{true};
        QVERIFY(scanFolder((m_dir->path() + "/tree").toStdString(), cancel).cancelled);
        std::atomic<bool> go{false};
        QVERIFY(scanFolder((m_dir->path() + "/does-not-exist").toStdString(), go).root->denied);
    }

    void connectionsListFromFakeUnixTable()
    {
        put("/proc/net/unix", "Num RefCount Protocol Flags Type St Inode Path\n0000: 00000002 00000000 00010000 0001 01 1234 /run/test.sock\n");
        const auto list = backend()->connections();
        QCOMPARE(list.size(), std::size_t(1));
        QCOMPARE(list[0].protocol, std::string("unix"));
        QCOMPARE(list[0].local, std::string("/run/test.sock"));
        QCOMPARE(list[0].state, std::string("LISTEN"));
        QCOMPARE(list[0].sent.state, Availability::Unsupported);
    }

    void onlyAcceptingUnixSocketsAreListening()
    {
        put("/proc/net/unix", "Num RefCount Protocol Flags Type St Inode Path\n"
                              "0000: 00000002 00000000 00000000 0002 01 1 /run/datagram.sock\n"
                              "0001: 00000002 00000000 00000000 0001 03 2 /run/talking.sock\n");
        const auto list = backend()->connections();
        QCOMPARE(list.size(), std::size_t(2));
        QCOMPARE(list[0].state, std::string("UNCONNECTED"));
        QCOMPARE(list[1].state, std::string("CONNECTED"));
    }

    void encryptedVolumeCountsTowardItsDisk()
    {
        put("/proc/diskstats", "259 0 nvme0n1 1 0 2 0 3 0 4 0 0 5 0\n");
        put("/sys/block/nvme0n1/device/model", "Fixture\n");
        put("/sys/block/nvme0n1/nvme0n1p2/size", "1\n");
        put("/sys/block/dm-0/slaves/nvme0n1p2", "");
        put("/dev/dm-0", "");
        QDir().mkpath(m_dir->path() + "/dev/mapper");
        QVERIFY(QFile::link("../dm-0", m_dir->path() + "/dev/mapper/secret"));
        QDir().mkpath(m_dir->path() + "/mnt/my disk");
        QDir().mkpath(m_dir->path() + "/mnt/home");
        put("/proc/self/mounts", "/dev/mapper/secret /mnt/my\\040disk btrfs rw 0 0\n/dev/mapper/secret /mnt/home btrfs rw 0 0\n");

        const auto snapshot = backend()->sample(SubDisk);
        QCOMPARE(snapshot->disks.size(), std::size_t(1));
        const DiskSample& disk = snapshot->disks[0];
        QCOMPARE(disk.mounts, (std::vector<std::string>{"/mnt/my disk", "/mnt/home"}));
        QCOMPARE(disk.fsTotal.state, Availability::Measured);
        QVERIFY(disk.fsTotal.value > 0);
    }

    void aMountWeCannotAskAboutIsNotZeroBytes()
    {
        put("/proc/diskstats", "8 0 sda 1 0 2 0 3 0 4 0 0 5 0\n");
        put("/sys/block/sda/device/model", "Fixture\n");
        put("/sys/block/sda/sda1/size", "1\n");
        put("/proc/self/mounts", "/dev/sda1 /gone ext4 rw 0 0\n");
        const auto snapshot = backend()->sample(SubDisk);
        QCOMPARE(snapshot->disks.size(), std::size_t(1));
        QCOMPARE(snapshot->disks[0].mounts, (std::vector<std::string>{"/gone"}));
        QCOMPARE(snapshot->disks[0].fsTotal.state, Availability::Unsupported);
    }

    void execChangesNameAndCommandLine()
    {
        const auto stat = [](const char* name) { return QByteArray("4242 (") + name + ") S 1 1 1 0 -1 0 0 0 0 0 1 1 0 0 20 0 1 0 777 0 5 0 0 0\n"; };
        put("/proc/stat", "cpu 1 0 1 1 0 0 0 0 0 0\n");
        put("/proc/4242/stat", stat("bash"));
        put("/proc/4242/cmdline", QByteArray("bash\0-c\0x\0", 11));
        auto system = backend();
        QCOMPARE(system->sample(SubProcesses)->processes.at(0).info->name, std::string("bash"));

        put("/proc/4242/stat", stat("sleep"));
        put("/proc/4242/cmdline", QByteArray("sleep\09\0", 8));
        const auto renamed = system->sample(SubProcesses);
        QCOMPARE(renamed->processes.at(0).info->name, std::string("sleep"));
        QCOMPARE(renamed->processes.at(0).info->commandLine, std::string("sleep 9"));
        QCOMPARE(renamed->processes.at(0).info->id.startTicks, std::uint64_t(777));

        // The same name with new arguments shows up within the refresh period.
        put("/proc/4242/cmdline", QByteArray("sleep\0" "10\0", 9));
        std::string line;
        for (int pass = 0; pass < 30; ++pass)
            line = system->sample(SubProcesses)->processes.at(0).info->commandLine;
        QCOMPARE(line, std::string("sleep 10"));
    }

    void actionsReachEveryThread()
    {
        const pid_t child = ::fork();
        QVERIFY(child >= 0);
        if (child == 0) {
            ::prctl(PR_SET_PDEATHSIG, SIGKILL); // never outlives this test, even if it fails
            std::thread([] { ::pause(); }).detach();
            std::thread([] { ::pause(); }).detach();
            ::pause();
            ::_exit(0);
        }
        const auto stopChild = qScopeGuard([child] {
            ::kill(child, SIGKILL);
            ::waitpid(child, nullptr, 0);
        });
        const QString tasks = QStringLiteral("/proc/%1/task").arg(child);
        const auto threads = [&tasks] { return QDir(tasks).entryList(QDir::Dirs | QDir::NoDotAndDotDot); };
        QTRY_COMPARE(threads().size(), 3);

        auto system = createLinuxBackend();
        ProcessId id{child, 0};
        for (const ProcessSample& process : system->sample(SubProcesses)->processes)
            if (process.info->id.pid == child)
                id = process.info->id;
        QVERIFY(id.startTicks != 0);
        // Lower than now, since raising it is refused however the test was started.
        const int niceness = std::min(19, ::getpriority(PRIO_PROCESS, 0) + 3);
        QVERIFY(system->setPriority(id, niceness).ok());
        // The first processor this test may use itself, since a container may rule out CPU 0.
        cpu_set_t allowed;
        QCOMPARE(::sched_getaffinity(0, sizeof allowed, &allowed), 0);
        int first = 0;
        while (!CPU_ISSET(first, &allowed))
            ++first;
        QVERIFY(system->setAffinity(id, {first}).ok());
        for (const QString& thread : threads()) {
            QFile file(tasks + "/" + thread + "/stat");
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QByteArray text = file.readAll();
            QCOMPARE(text.mid(text.lastIndexOf(')') + 2).split(' ').value(16), QByteArray::number(niceness));
            cpu_set_t set;
            QCOMPARE(::sched_getaffinity(thread.toInt(), sizeof set, &set), 0);
            QCOMPARE(CPU_COUNT(&set), 1);
        }
    }

    void trafficTotalsLeaveOutLinksOverHardware()
    {
        NetworkSample wifi, bridge, loop;
        wifi.hasDevice = true;
        loop.loopback = true;
        const std::vector<NetworkSample> laptop{wifi, bridge, loop};
        QVERIFY(countsTowardTotal(wifi, laptop));
        QVERIFY(!countsTowardTotal(bridge, laptop));
        QVERIFY(!countsTowardTotal(loop, laptop));
        // Inside a container a virtual link is all there is.
        QVERIFY(countsTowardTotal(bridge, {bridge, loop}));
    }

    void kernelModulesParse()
    {
        put("/proc/modules", "amdgpu 123456 7 foo,bar, Live 0x0\nsnd 4096 0 - Live 0x0\n");
        const auto list = backend()->kernelModules();
        QCOMPARE(list.size(), std::size_t(2));
        QCOMPARE(list[0].name, std::string("amdgpu"));
        QCOMPARE(list[0].sizeBytes, std::uint64_t(123456));
        QCOMPARE(list[0].useCount, 7);
        QCOMPARE(list[0].usedBy, std::string("foo,bar"));
        QCOMPARE(list[1].usedBy, std::string());
    }

    void liveConnectionsAnswer()
    {
        // A real listening socket of ours must appear, with us as the owner.
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const std::string port = ":" + std::to_string(server.serverPort());
        bool found = false;
        for (const NetworkConnection& c : createLinuxBackend()->connections())
            if (c.protocol == "tcp" && c.local.ends_with(port) && c.owner.pid == ::getpid() && c.state == "LISTEN")
                found = true;
        QVERIFY(found);
    }

    void sensorLimitsAreTheLowerOneAndMissingIsNotZero()
    {
        put("/sys/class/hwmon/hwmon0/name", "nvme\n");
        put("/sys/class/hwmon/hwmon0/temp1_input", "40000\n");
        put("/sys/class/hwmon/hwmon0/temp1_max", "80000\n");
        put("/sys/class/hwmon/hwmon0/temp1_crit", "90000\n");
        put("/sys/class/hwmon/hwmon0/temp2_input", "41000\n");
        put("/sys/class/hwmon/hwmon0/temp2_max", "0\n"); // this chip's way of saying no limit
        put("/sys/class/hwmon/hwmon0/temp3_input", "42000\n");
        put("/sys/class/hwmon/hwmon0/temp3_max", "65261850\n"); // the placeholder NVMe drives really send
        const auto thermal = backend()->sample(SubThermal)->thermal;
        QCOMPARE(thermal.sensors.size(), std::size_t(3));
        QCOMPARE(thermal.sensors[0].limitC.value, 80.0);
        QCOMPARE(thermal.sensors[1].limitC.state, Availability::Unsupported);
        QCOMPARE(thermal.sensors[2].limitC.state, Availability::Unsupported);
    }

    void fansAreReadAndAMissingFanIsNotZero()
    {
        put("/sys/class/hwmon/hwmon0/name", "nct6775\n");
        put("/sys/class/hwmon/hwmon0/fan1_input", "1200\n");
        put("/sys/class/hwmon/hwmon0/fan1_label", "CPU Fan\n");
        put("/sys/class/hwmon/hwmon0/pwm1", "128\n");
        put("/sys/class/hwmon/hwmon0/fan2_input", "0\n");
        const auto thermal = backend()->sample(SubThermal)->thermal;
        QCOMPARE(thermal.fans.size(), std::size_t(2));
        QCOMPARE(thermal.fans[0].label, std::string("CPU Fan"));
        QCOMPARE(thermal.fans[0].rpm.value, 1200.0);
        QVERIFY(thermal.fans[0].dutyPct.value > 50 && thermal.fans[0].dutyPct.value < 51);
        QCOMPARE(thermal.fans[1].rpm.value, 0.0);                                // a stopped fan really is 0
        QCOMPARE(thermal.fans[1].dutyPct.state, Availability::Unsupported);      // but its power is unknown
        QVERIFY(createLinuxBackend((m_dir->path() + "/empty").toStdString())->sample(SubThermal)->thermal.fans.empty());
    }

    void thermalZonesAddOnlyWhatHwmonLacks()
    {
        put("/sys/class/hwmon/hwmon0/name", "acpitz\n");
        put("/sys/class/hwmon/hwmon0/temp1_input", "30000\n");
        put("/sys/class/thermal/thermal_zone0/type", "acpitz\n");
        put("/sys/class/thermal/thermal_zone0/temp", "30000\n");
        put("/sys/class/thermal/thermal_zone1/type", "cpu-thermal\n");
        put("/sys/class/thermal/thermal_zone1/temp", "55000\n");
        put("/sys/class/thermal/thermal_zone1/trip_point_0_type", "critical\n");
        put("/sys/class/thermal/thermal_zone1/trip_point_0_temp", "95000\n");
        const auto thermal = backend()->sample(SubThermal)->thermal;
        QCOMPARE(thermal.sensors.size(), std::size_t(2)); // the second acpitz is the same sensor
        QCOMPARE(thermal.sensors[1].label, std::string("cpu-thermal"));
        QCOMPARE(thermal.sensors[1].limitC.value, 95.0);
        QCOMPARE(thermal.cpuHotspotC.value, 55.0);
    }

    void wifiSignalIsRead()
    {
        put("/proc/net/dev", "h\nh\n  wlan0: 1 0 0 0 0 0 0 0 2 0 0 0 0 0 0 0\n");
        put("/sys/class/net/wlan0/wireless/x", "");
        put("/proc/net/wireless", "Inter-| sta-|\n face | tus |\nwlan0: 0000   35.  -65.  -256        0      0      0      0      0        0\n");
        const auto snapshot = backend()->sample(SubNetwork);
        QCOMPARE(snapshot->networks[0].wifiSignalDbm.value, -65.0);
        QCOMPARE(snapshot->networks[0].wifiQualityPct.value, 50.0);
        QCOMPARE(snapshot->networks[0].wifiQualityPct.state, Availability::Estimated); // assumes a scale
    }

    void oomKillsAreReadAndMissingIsNotZero()
    {
        put("/proc/meminfo", "MemTotal: 100 kB\n");
        QCOMPARE(backend()->sample(SubMemory)->memory.oomKills.state, Availability::Unsupported);
        put("/proc/vmstat", "pgfault 5\noom_kill 3\nnext 1\n");
        QCOMPARE(backend()->sample(SubMemory)->memory.oomKills.value, std::uint64_t(3));
    }

    void processGpuAddsEnginesAndCountsClientsOnce()
    {
        const QByteArray stat = "42 (game) S 1 42 42 0 -1 0 0 0 0 0 1 0 0 0 20 0 1 0 500 1000 10 0 0";
        put("/proc/stat", "cpu  1000 0 0 0 0 0 0 0 0 0\n");
        put("/proc/42/stat", stat);
        put("/proc/42/fdinfo/5", "drm-client-id:\t7\ndrm-engine-gfx:\t1000000000 ns\ndrm-engine-capacity-gfx:\t2\n");
        put("/proc/42/fdinfo/6", "drm-client-id:\t7\ndrm-engine-gfx:\t1000000000 ns\n");
        auto system = backend();
        auto first = system->sample(SubProcesses | SubProcessGpu);
        QCOMPARE(first->processes[0].gpuPct.state, Availability::NotYetSampled);
        QTest::qWait(1000);
        put("/proc/42/fdinfo/5", "drm-client-id:\t7\ndrm-engine-gfx:\t1500000000 ns\n");
        auto second = system->sample(SubProcesses | SubProcessGpu);
        QVERIFY(second->processes[0].gpuPct.known());
        QVERIFY(second->processes[0].gpuPct.value > 30 && second->processes[0].gpuPct.value <= 60); // about 50%
        QCOMPARE(system->sample(SubProcesses)->processes[0].gpuPct.state, Availability::NotYetSampled); // not asked, not read
    }

    void offlineProcessorsKeepTheirNumbers()
    {
        put("/proc/stat", "cpu  10 0 0 90 0 0 0 0 0 0\ncpu0 5 0 0 45 0 0 0 0 0 0\ncpu2 5 0 0 45 0 0 0 0 0 0\nintr 1\n");
        put("/sys/devices/system/cpu/online", "0,2\n");
        put("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "1000000\n");
        put("/sys/devices/system/cpu/cpu2/cpufreq/scaling_cur_freq", "3000000\n");
        put("/sys/devices/cpu_atom/cpus", "2\n");
        auto system = backend();
        const auto snapshot = system->sample(SubCpu);
        QCOMPARE(snapshot->cpu.cores.size(), std::size_t(2));
        QCOMPARE(snapshot->cpu.cores[0].freqMHz.value, 1000.0);
        QCOMPARE(snapshot->cpu.cores[1].freqMHz.value, 3000.0); // cpu2's, not the absent cpu1's
        const CpuTopology& topology = system->topology();
        QCOMPARE(topology.ids, std::vector<int>({0, 2}));
        QCOMPARE(topology.logical, 2);
        QCOMPARE(topology.physical, 0); // not told, and the UI doesn't show this as a count
        QVERIFY(!topology.efficiency[0]);
        QVERIFY(topology.efficiency[1]);
    }

    void aProcessorChangingStateIsNotARate()
    {
        put("/proc/stat", "cpu  10 0 0 90 0 0 0 0 0 0\ncpu0 5 0 0 45 0 0 0 0 0 0\ncpu1 5 0 0 45 0 0 0 0 0 0\n");
        auto system = backend();
        system->sample(SubCpu);
        put("/proc/stat", "cpu  20 0 0 180 0 0 0 0 0 0\ncpu0 10 0 0 90 0 0 0 0 0 0\ncpu2 5 0 0 45 0 0 0 0 0 0\n");
        const auto snapshot = system->sample(SubCpu);
        QCOMPARE(snapshot->cpu.cores[1].usagePct.state, Availability::NotYetSampled);
    }

    void throttleCountsEachPackageOnce()
    {
        const auto add = [this](int cpu, int package, int count) {
            const QString dir = QStringLiteral("/sys/devices/system/cpu/cpu%1/").arg(cpu);
            put(dir + "thermal_throttle/package_throttle_count", QByteArray::number(count) + "\n");
            put(dir + "topology/physical_package_id", QByteArray::number(package) + "\n");
        };
        add(0, 0, 5);
        add(1, 0, 5);
        add(2, 1, 3);
        const auto snapshot = backend()->sample(SubThermal);
        QCOMPARE(snapshot->thermal.throttleEvents.value, std::uint64_t(8));
    }

    void anUnreadableThrottleCountIsNotZero()
    {
        if (::geteuid() == 0)
            QSKIP("Root can read everything.");
        const QString file = "/sys/devices/system/cpu/cpu0/thermal_throttle/package_throttle_count";
        put(file, "5\n");
        ::chmod((m_dir->path() + file).toUtf8().constData(), 0);
        QCOMPARE(backend()->sample(SubThermal)->thermal.throttleEvents.state, Availability::AccessDenied);
    }

    void powerAddsEveryPackage()
    {
        put("/sys/class/powercap/intel-rapl:0/energy_uj", "1000000");
        put("/sys/class/powercap/intel-rapl:1/energy_uj", "1000000");
        auto system = backend();
        system->sample(SubPower);
        QTest::qWait(100);
        put("/sys/class/powercap/intel-rapl:0/energy_uj", "2000000");
        QTest::qWait(1);
        put("/sys/class/powercap/intel-rapl:1/energy_uj", "2000000");
        const auto second = system->sample(SubPower);
        QVERIFY(second->power.cpuPackageW.known());
        QVERIFY(second->power.cpuPackageW.value > 5); // 2 J in about 0.1 s, far more than one package's share
    }

    void aWrapOfUnknownSizeIsAGapNotZeroWatts()
    {
        put("/sys/class/powercap/intel-rapl:0/energy_uj", "999000");
        auto system = backend();
        system->sample(SubPower);
        put("/sys/class/powercap/intel-rapl:0/energy_uj", "1000");
        QTest::qWait(50);
        QCOMPARE(system->sample(SubPower)->power.cpuPackageW.state, Availability::NotYetSampled);
    }

    void batteryPowerWorkedOutFromTwoReadsIsAnEstimate()
    {
        put("/sys/class/power_supply/BAT0/type", "Battery\n");
        put("/sys/class/power_supply/BAT0/current_now", "1000000\n");
        put("/sys/class/power_supply/BAT0/voltage_now", "12000000\n");
        const auto snapshot = backend()->sample(SubPower);
        QCOMPARE(snapshot->power.batteries.size(), std::size_t(1));
        QCOMPARE(snapshot->power.batteries[0].powerW.state, Availability::Estimated);
        QCOMPARE(snapshot->power.batteries[0].powerW.value, 12.0);
    }

    void aDeviceThatReturnsStartsOver()
    {
        const auto stats = [this](const QByteArray& lines) { put("/proc/diskstats", lines); };
        put("/sys/block/sdb/device/model", "First\n");
        stats("8 16 sdb 1 0 2 0 3 0 4 0 0 5 0\n");
        put("/proc/net/dev", "h\nh\n  veth1: 100 0 0 0 0 0 0 0 200 0 0 0 0 0 0 0\n");
        auto system = backend();
        system->sample(SubDisk | SubNetwork);
        QTest::qWait(10);
        stats("");
        put("/proc/net/dev", "h\nh\n");
        QVERIFY(system->sample(SubDisk | SubNetwork)->disks.empty());
        QTest::qWait(10);
        put("/sys/block/sdb/device/model", "Second\n");
        stats("8 16 sdb 1 0 900000 0 3 0 4 0 0 5 0\n");
        put("/proc/net/dev", "h\nh\n  veth1: 900000 0 0 0 0 0 0 0 200 0 0 0 0 0 0 0\n");
        const auto back = system->sample(SubDisk | SubNetwork);
        QCOMPARE(back->disks.at(0).model, std::string("Second"));
        QCOMPARE(back->disks.at(0).readBps.state, Availability::NotYetSampled); // not a spike from the old counters
        QCOMPARE(back->networks.at(0).rxBps.state, Availability::NotYetSampled);
    }

    void everyInterfaceKeepsItsName()
    {
        put("/proc/net/dev", "h\nh\n  eth0: 1 0 0 0 0 0 0 0 2 0 0 0 0 0 0 0\n  wlan0: 3 0 0 0 0 0 0 0 4 0 0 0 0 0 0 0\n");
        put("/sys/class/net/eth0/address", "aa:bb:cc:dd:ee:ff\n");
        put("/sys/class/net/eth0/operstate", "up\n");
        put("/sys/class/net/eth0/speed", "1000\n");
        const auto snapshot = backend()->sample(SubNetwork);
        QCOMPARE(snapshot->networks.at(0).name, std::string("eth0"));
        QCOMPARE(snapshot->networks.at(0).mac, std::string("aa:bb:cc:dd:ee:ff"));
        QCOMPARE(snapshot->networks.at(1).name, std::string("wlan0"));
        QCOMPARE(snapshot->networks.at(1).rxTotal, std::uint64_t(3));
    }

    void aGpuClientWithoutEngineTimesIsNotZero()
    {
        put("/proc/stat", "cpu  1000 0 0 0 0 0 0 0 0 0\n");
        put("/proc/42/stat", "42 (game) S 1 42 42 0 -1 0 0 0 0 0 1 0 0 0 20 0 1 0 500 1000 10 0 0");
        put("/proc/42/fdinfo/5", "drm-client-id:\t7\ndrm-cycles-rcs:\t1000\n");
        put("/proc/43/stat", "43 (quiet) S 1 43 43 0 -1 0 0 0 0 0 1 0 0 0 20 0 1 0 500 1000 10 0 0");
        put("/proc/43/fdinfo/5", "pos: 0\n");
        auto list = backend()->sample(SubProcesses | SubProcessGpu)->processes;
        std::sort(list.begin(), list.end(), [](const ProcessSample& a, const ProcessSample& b) { return a.info->id.pid < b.info->id.pid; });
        QCOMPARE(list.at(0).gpuPct.state, Availability::Unsupported);
        QCOMPARE(list.at(1).gpuPct.state, Availability::Measured); // no GPU contexts at all really is 0
        QCOMPARE(list.at(1).gpuPct.value, 0.0);
    }

    void aRewrittenCommandLineIsNotAName()
    {
        put("/proc/15/stat", "15 (fifteen-letters) S 1 1 1 0 -1 0 0 0 0 0 1 1 0 0 20 0 1 0 77 0 5 0 0 0\n");
        put("/proc/15/cmdline", "fifteen-letters" + QByteArray(5000, 'x') + QByteArray(1, '\0'));
        const auto snapshot = backend()->sample(SubProcesses);
        QCOMPARE(snapshot->processes.at(0).info->name, std::string("fifteen-letters"));
    }

    void missingUptimeIsNotZero()
    {
        QCOMPARE(backend()->systemFacts().uptimeSeconds.state, Availability::Unsupported);
        put("/proc/uptime", "120.5 300.0\n");
        QCOMPARE(backend()->systemFacts().uptimeSeconds.value, 120.5);
    }

    void aFolderWeCantEnterIsCountedAsDenied()
    {
        if (::geteuid() == 0)
            QSKIP("Root can read everything.");
        put("/tree/closed/file", QByteArray(8192, 'x'));
        const QByteArray closed = (m_dir->path() + "/tree/closed").toUtf8();
        ::chmod(closed.constData(), 0444); // may be listed, but nothing in it can be weighed
        const auto reopen = qScopeGuard([&closed] { ::chmod(closed.constData(), 0755); });
        std::atomic<bool> cancel{false};
        const DiskScanResult result = scanFolder((m_dir->path() + "/tree").toStdString(), cancel);
        QCOMPARE(result.deniedFolders, std::uint64_t(1));
    }

    void liveSystemAnswers()
    {
        auto system = createLinuxBackend();
        system->sample(SubAll);
        QTest::qWait(200);
        const auto snapshot = system->sample(SubAll);
        QVERIFY(snapshot->cpu.usagePct.known());
        QVERIFY(snapshot->memory.total.known());
        QVERIFY(!snapshot->processes.empty());
        QVERIFY(system->topology().logical > 0);
    }
};

QTEST_GUILESS_MAIN(TestSamplers)
#include "test_samplers.moc"
