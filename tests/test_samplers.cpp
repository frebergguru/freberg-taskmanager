#include "core/diskscan.h"
#include "platform/backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <sys/stat.h>
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

        // Same PID, new start time: a new process, so no rate carries over.
        put("/proc/stat", "cpu  3000 0 0 0 0 0 0 0 0 0\n");
        put("/proc/42/stat", statLine("second", 900, 600));
        snapshot = system->sample(SubProcesses);
        QCOMPARE(snapshot->processes[0].info->name, std::string("second"));
        QCOMPARE(snapshot->processes[0].info->id.startTicks, std::uint64_t(600));
        QCOMPARE(snapshot->processes[0].cpuPct.state, Availability::NotYetSampled);
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
