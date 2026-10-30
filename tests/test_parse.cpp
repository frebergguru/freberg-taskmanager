#include "core/history.h"
#include "core/parse.h"
#include "core/smbios.h"

#include <QTest>
#include <cmath>

using namespace fg;

class TestParse : public QObject {
    Q_OBJECT
private slots:
    void cpuStat()
    {
        std::vector<CpuTimes> out;
        parseCpuStat("cpu  100 0 100 700 100 0 0 0 0 0\ncpu0 50 0 50 350 50 0 0 0 0 0\nintr 1 2 3\n", out);
        QCOMPARE(out.size(), std::size_t(2));
        QCOMPARE(out[0].total, std::uint64_t(1000));
        QCOMPARE(out[0].idle, std::uint64_t(800));
    }

    void cpuBusy()
    {
        QCOMPARE(*cpuBusyPercent({1000, 800}, {1100, 850}), 50.0);
        QVERIFY(!cpuBusyPercent({1000, 800}, {1000, 800}));
    }

    void meminfo()
    {
        const std::string_view text = "MemTotal:       1000 kB\nCached:          20 kB\nSwapCached:       5 kB\nHugePages_Total: 7\n";
        QCOMPARE(*meminfoBytes(text, "MemTotal"), std::uint64_t(1024000));
        QCOMPARE(*meminfoBytes(text, "Cached"), std::uint64_t(20480));
        QCOMPARE(*meminfoBytes(text, "HugePages_Total"), std::uint64_t(7));
        QVERIFY(!meminfoBytes(text, "Missing"));
    }

    void diskstats()
    {
        std::vector<DiskCounters> out;
        parseDiskstats(" 259 0 nvme0n1 10 0 200 5 20 0 400 8 0 77 90 0 0 0 0 0 0\n", out);
        QCOMPARE(out.size(), std::size_t(1));
        QCOMPARE(out[0].name, std::string_view("nvme0n1"));
        QCOMPARE(out[0].sectorsRead, std::uint64_t(200));
        QCOMPARE(out[0].sectorsWritten, std::uint64_t(400));
        QCOMPARE(out[0].ioTicksMs, std::uint64_t(77));
    }

    void netdev()
    {
        std::vector<NetCounters> out;
        parseNetDev("Inter-|   Receive\n face |bytes\n  eth0: 100 1 0 0 0 0 0 0 200 2 0 0 0 0 0 0\n", out);
        QCOMPARE(out.size(), std::size_t(1));
        QCOMPARE(out[0].name, std::string_view("eth0"));
        QCOMPARE(out[0].rxBytes, std::uint64_t(100));
        QCOMPARE(out[0].txBytes, std::uint64_t(200));
    }

    void pressure()
    {
        const auto values = parsePressure("some avg10=1.50 avg60=0.10 avg300=0.00 total=1\nfull avg10=0.25 avg60=0.00 avg300=0.00 total=1\n");
        QCOMPARE(*values.some10, 1.5);
        QCOMPARE(*values.full10, 0.25);
        QVERIFY(!parsePressure("some avg60=1.0\n").some10);
    }

    void pidStat_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("plain") << "bash";
        QTest::newRow("spaces") << "Web Content";
        QTest::newRow("parens") << "a) (b";
    }

    void pidStat()
    {
        QFETCH(QString, name);
        const std::string text = "42 (" + name.toStdString() + ") S 7 42 42 0 -1 4194304 1 0 0 0 11 22 0 0 20 3 5 0 99999 1000 66 0 0";
        PidStat stat;
        QVERIFY(parsePidStat(text, stat));
        QCOMPARE(QString::fromUtf8(stat.comm.data(), qsizetype(stat.comm.size())), name);
        QCOMPARE(stat.state, 'S');
        QCOMPARE(stat.ppid, 7);
        QCOMPARE(stat.utime, std::uint64_t(11));
        QCOMPARE(stat.stime, std::uint64_t(22));
        QCOMPARE(stat.nice, 3);
        QCOMPARE(stat.threads, 5);
        QCOMPARE(stat.startTicks, std::uint64_t(99999));
        QCOMPARE(stat.rssPages, std::int64_t(66));
    }

    void pidStatTruncated()
    {
        PidStat stat;
        QVERIFY(!parsePidStat("42 (bash) S 7", stat));
        QVERIFY(!parsePidStat("garbage", stat));
    }

    void cpuList()
    {
        QCOMPARE(parseCpuList("0-2,8,10-11\n"), (std::vector<int>{0, 1, 2, 8, 10, 11}));
        QVERIFY(parseCpuList("").empty());
    }

    void memoryModules()
    {
        // One 8 GB DDR5 SODIMM at 5600 MT/s, then an empty slot, then the end marker.
        std::string table;
        const auto device = [&table](unsigned size, unsigned speed, const std::string& locator) {
            std::string s(0x17, '\0');
            s[0] = 17;
            s[1] = 0x17;
            s[0x0C] = static_cast<char>(size & 0xff);
            s[0x0D] = static_cast<char>(size >> 8);
            s[0x0E] = 0x0D;
            s[0x10] = 1;
            s[0x12] = 0x22;
            s[0x15] = static_cast<char>(speed & 0xff);
            s[0x16] = static_cast<char>(speed >> 8);
            table += s + locator + std::string(2, '\0');
        };
        device(8192, 5600, "DIMM A");
        device(0, 0, "DIMM B");
        table += std::string{char(127), char(4), 0, 0, 0, 0};

        const auto modules = parseMemoryModules(table);
        QCOMPARE(modules.size(), std::size_t(2));
        QCOMPARE(modules[0].sizeBytes, std::uint64_t(8192) * 1024 * 1024);
        QCOMPARE(modules[0].type, std::string("DDR5"));
        QCOMPARE(modules[0].formFactor, std::string("SODIMM"));
        QCOMPARE(modules[0].speedMTs, 5600u);
        QCOMPARE(modules[0].locator, std::string("DIMM A"));
        QCOMPARE(modules[1].sizeBytes, std::uint64_t(0));
        QCOMPARE(modules[1].locator, std::string("DIMM B"));
        QVERIFY(parseMemoryModules("").empty());
    }

    void historyKeepsGaps()
    {
        History history(3);
        history.push(1.0f);
        history.push(Reading<double>::missing(Availability::Unsupported));
        history.push(Reading<double>::measured(3.0));
        history.push(4.0f);
        QCOMPARE(history.size(), std::size_t(3));
        QVERIFY(std::isnan(history.at(0))); // not 0 and not interpolated
        QCOMPARE(history.at(1), 3.0f);
        QCOMPARE(history.latest(), 4.0f);
    }
};

QTEST_APPLESS_MAIN(TestParse)
#include "test_parse.moc"
