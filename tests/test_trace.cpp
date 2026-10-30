#include "core/trace.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>

using namespace fg;

class TestTrace : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;

    static TraceFrame frame(std::int64_t ms, float cpu)
    {
        TraceFrame f;
        f.ms = ms;
        for (float* value : {&f.cpuPct, &f.memoryUsedBytes, &f.memoryTotalBytes, &f.diskReadBps, &f.diskWriteBps, &f.netRxBps, &f.netTxBps, &f.gpuPct, &f.cpuTempC, &f.cpuPowerW, &f.pressureCpu, &f.pressureMemory, &f.pressureIo})
            *value = std::nanf("");
        f.cpuPct = cpu;
        return f;
    }

    std::string path(const char* name) const { return (m_dir->path() + "/" + name).toStdString(); }

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>(QCoreApplication::applicationDirPath() + "/fixture-XXXXXX");
        QVERIFY(m_dir->isValid());
    }

    void roundTripKeepsValuesGapsAndProcesses()
    {
        std::deque<TraceFrame> frames;
        frames.push_back(frame(1000, 25.0f));
        frames.push_back(frame(2000, std::nanf(""))); // a gap
        frames.back().top.push_back({"Web Content (x)", 42, 12.5f, 1024.0f});
        frames.push_back(frame(3000, 75.0f));

        std::string error;
        QVERIFY2(saveTrace(path("a.fgtrace"), frames, error), error.c_str());
        std::deque<TraceFrame> loaded;
        QVERIFY2(loadTrace(path("a.fgtrace"), loaded, error), error.c_str());

        QCOMPARE(loaded.size(), std::size_t(3));
        QCOMPARE(loaded[0].ms, std::int64_t(1000));
        QCOMPARE(loaded[0].cpuPct, 25.0f);
        QVERIFY(std::isnan(loaded[1].cpuPct));      // the gap is still a gap, not 0
        QVERIFY(std::isnan(loaded[0].cpuPowerW));
        QCOMPARE(loaded[1].top.size(), std::size_t(1));
        QCOMPARE(loaded[1].top[0].name, std::string("Web Content (x)"));
        QCOMPARE(loaded[1].top[0].pid, 42);
        QCOMPARE(loaded[1].top[0].cpuPct, 12.5f);
        QVERIFY(!QFile::exists(QString::fromStdString(path("a.fgtrace.part"))));
    }

    void damagedFilesAreRefused_data()
    {
        QTest::addColumn<QByteArray>("content");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("not a recording") << QByteArray("hello there, this is no trace");
        QTest::newRow("newer version") << QByteArray("FGTRACE\n\x63\0\0\0", 12);
        QTest::newRow("impossible frame count") << (QByteArray("FGTRACE\n\x01\0\0\0\x0c\0\0\0", 16) + QByteArray(8, '\xff'));
        QTest::newRow("cut short") << (QByteArray("FGTRACE\n\x01\0\0\0\x0c\0\0\0", 16) + QByteArray("\x05\0\0\0\0\0\0\0", 8) + QByteArray(6, '\0'));
    }

    void damagedFilesAreRefused()
    {
        QFETCH(QByteArray, content);
        QFile file(QString::fromStdString(path("bad.fgtrace")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
        file.close();

        std::deque<TraceFrame> loaded;
        loaded.push_back(frame(1, 1.0f));
        std::string error;
        QVERIFY(!loadTrace(path("bad.fgtrace"), loaded, error));
        QVERIFY(!error.empty());
        QCOMPARE(loaded.size(), std::size_t(1)); // what was already open is left alone
    }

    void bufferDropsOldestAtItsLimit()
    {
        TraceBuffer buffer(sizeof(TraceFrame) * 3);
        for (int i = 0; i < 10; ++i)
            buffer.add(frame(i, 1.0f));
        QCOMPARE(buffer.frames().size(), std::size_t(3));
        QCOMPARE(buffer.frames().back().ms, std::int64_t(9));
        QCOMPARE(buffer.frames().front().ms, std::int64_t(7));
    }

    void analysisRanksContributorsAndCountsGaps()
    {
        std::deque<TraceFrame> frames;
        frames.push_back(frame(0, 50.0f));
        frames.back().top = {{"busy", 1, 40.0f, 100.0f}, {"quiet", 2, 1.0f, 900.0f}};
        frames.push_back(frame(1000, std::nanf("")));
        frames.push_back(frame(2000, 90.0f));
        frames.back().top = {{"busy", 1, 80.0f, 200.0f}};

        const TraceAnalysis a = analyseTrace(frames, 0, 99);
        QCOMPARE(a.frames, std::size_t(3));
        QCOMPARE(a.gaps, std::size_t(1));
        QCOMPARE(a.seconds, 2.0);
        QCOMPARE(a.peakCpu, 90.0);
        QCOMPARE(a.contributors.front().name, std::string("busy"));
        QCOMPARE(a.contributors.front().averageCpuPct, 40.0);
        QCOMPARE(a.contributors.front().peakRssBytes, 200.0);
        QCOMPARE(analyseTrace({}, 0, 0).frames, std::size_t(0));
    }

    void exportsWriteGapsAsEmpty()
    {
        std::deque<TraceFrame> frames;
        frames.push_back(frame(0, 50.0f));
        frames.push_back(frame(1000, std::nanf("")));
        frames.back().top = {{"a\"b", 1, 1.0f, 2.0f}};
        std::string error;
        QVERIFY(exportCsv(path("a.csv"), frames, error));
        QVERIFY(exportJson(path("a.json"), frames, error));

        QFile csv(QString::fromStdString(path("a.csv")));
        QVERIFY(csv.open(QIODevice::ReadOnly));
        const QList<QByteArray> lines = csv.readAll().split('\n');
        QVERIFY(lines[0].startsWith("seconds,cpu_percent"));
        QVERIFY(lines[1].startsWith("0.000,50,"));
        QVERIFY(lines[2].startsWith("1.000,,"));

        QFile json(QString::fromStdString(path("a.json")));
        QVERIFY(json.open(QIODevice::ReadOnly));
        const QByteArray text = json.readAll();
        QVERIFY(text.contains("\"cpu_percent\":null"));
        QVERIFY(text.contains("a\\\"b"));
    }
};

QTEST_GUILESS_MAIN(TestTrace)
#include "test_trace.moc"
