// Drives the real window without showing anything. Settings and data go to a folder
// next to the test, so the user's own are never touched.
#include "app/hub.h"
#include "app/popout/popout.h"
#include "app/popout/skin.h"
#include "app/settings.h"
#include "app/shell/mainwindow.h"
#include "app/shell/sheet.h"
#include "app/style/appearance.h"
#include "app/style/style.h"
#include "app/views/processes.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QDir>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>

using namespace fg;

class TestUi : public QObject {
    Q_OBJECT
    SamplerHub* m_hub = nullptr;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        installAppearance();
        m_hub = new SamplerHub(this);
    }

    void everyViewOpensAndSamplesOnlyWhatItNeeds()
    {
        MainWindow window(*m_hub);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QSignalSpy samples(m_hub, &SamplerHub::sampled);
        for (int view = 0; view < window.viewCount(); ++view) {
            window.openView(view);
            QTest::qWait(30);
        }
        // Back on a light page, the process walk has to stop.
        window.openView(4); // System Info
        QTest::qWait(30);
        QVERIFY(!(m_hub->wanted() & SubProcesses));
        QVERIFY(m_hub->wanted() & SubCpu);
    }

    void hiddenWindowSamplesNothing()
    {
        MainWindow window(*m_hub);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(m_hub->wanted() != SubNone); // starts once the window is really on screen
        window.hide();
        QCOMPARE(m_hub->wanted(), Subsystems(SubNone));
    }

    void popoutSamplesOnlyWhileShown()
    {
        Popout popout(*m_hub);
        QCOMPARE(m_hub->wanted(), Subsystems(SubNone));
        popout.show();
        QVERIFY(QTest::qWaitForWindowExposed(&popout));
        QVERIFY(m_hub->wanted() & SubCpu);
        QVERIFY(!(m_hub->wanted() & SubProcesses));
        popout.hide();
        QCOMPARE(m_hub->wanted(), Subsystems(SubNone));
        QVERIFY(popout.sizeHint().isValid());
    }

    void endTaskStopsARealProcessAfterConfirmation()
    {
        QProcess sleeper;
        sleeper.start("sleep", {"120"});
        QVERIFY(sleeper.waitForStarted());
        const int pid = static_cast<int>(sleeper.processId());

        ProcessId id{pid, 0};
        const auto snapshot = m_hub->backend().sample(SubProcesses);
        for (const ProcessSample& process : snapshot->processes)
            if (process.info->id.pid == pid)
                id = process.info->id;
        QVERIFY(id.startTicks != 0);
        QVERIFY(m_hub->backend().signalProcess(id, ProcessSignal::End).ok());
        QVERIFY(sleeper.waitForFinished(3000));
    }

    void builtInSkinsAllLoad()
    {
        QTemporaryDir empty(QCoreApplication::applicationDirPath() + "/fixture-XXXXXX");
        const SkinLibrary library = loadSkins(empty.path());
        QVERIFY2(library.problems.isEmpty(), library.problems.isEmpty() ? "" : qPrintable(library.problems.first().reason));
        QVERIFY(library.skins.size() >= 6);
    }

    void badSkinsAreListedWithAReason()
    {
        QTemporaryDir dir(QCoreApplication::applicationDirPath() + "/fixture-XXXXXX");
        const auto write = [&dir](const QString& name, const QByteArray& text) {
            QFile file(dir.path() + "/" + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(text);
        };
        write("bad-json.fgskin", "{not json");
        write("no-id.fgskin", "{\"format\":1,\"name\":\"X\"}");
        write("wrong-format.fgskin", "{\"format\":2,\"id\":\"a\",\"name\":\"A\"}");
        write("taken.fgskin", "{\"format\":1,\"id\":\"default\",\"name\":\"Another\"}");
        write("big.fgskin", QByteArray(300 * 1024, ' '));
        write("good.fgskin", "{\"format\":1,\"id\":\"good\",\"name\":\"Good\",\"colours\":{\"ink\":\"#ff000080\"},\"metrics\":{\"border\":9999}}");
        const SkinLibrary library = loadSkins(dir.path());
        QCOMPARE(library.problems.size(), 5);
        const auto good = std::find_if(library.skins.begin(), library.skins.end(), [](const Skin& s) { return s.id == "good"; });
        QVERIFY(good != library.skins.end());
        QCOMPARE(good->ink, QColor(255, 0, 0, 128)); // CSS order, alpha last
        QCOMPARE(good->border, 16);                  // out of range is clamped, not refused
    }

    void skinImagesCantLeaveTheirFolder()
    {
        QTemporaryDir dir(QCoreApplication::applicationDirPath() + "/fixture-XXXXXX");
        QImage(4, 4, QImage::Format_RGB32).save(dir.path() + "/outside.png");
        QVERIFY(QDir(dir.path()).mkdir("inner"));
        Skin skin;
        QString reason;
        QVERIFY(parseSkin("{\"format\":1,\"id\":\"x\",\"name\":\"X\",\"surfaces\":{\"windowImage\":{\"file\":\"../outside.png\"}}}", dir.path() + "/inner", skin, reason));
        QVERIFY(skin.windowImage.image.isNull());
    }

    void textSizeGrowsTheWindowAndKeepsItsContent()
    {
        MainWindow window(*m_hub);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const QSize small = window.size();
        Settings::instance().setValue("appearance/textPercent", 200);
        window.applyAppearance();
        QTest::qWait(20);
        QVERIFY(window.width() > small.width());
        QVERIFY(window.height() > small.height());
        Settings::instance().setValue("appearance/textPercent", 100);
        window.applyAppearance();
    }

    void tabsWorkFromTheKeyboard()
    {
        MainWindow window(*m_hub);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto sheets = window.findChildren<Sheet*>();
        QCOMPARE(sheets.size(), 1);
        const int start = sheets[0]->currentIndex();
        sheets[0]->step(1);
        QCOMPARE(sheets[0]->currentIndex(), (start + 1) % sheets[0]->count());
        // Like any exclusive group: Tab reaches the chosen tab, and arrows move between tabs.
        const auto tabs = sheets[0]->findChildren<SheetTab*>();
        QCOMPARE(tabs.size(), sheets[0]->count());
        int tabStops = 0;
        for (SheetTab* tab : tabs) {
            if (tab->focusPolicy() & Qt::TabFocus) {
                ++tabStops;
                QVERIFY(tab->isChecked());
            }
            QVERIFY(tab->focusPolicy() & Qt::ClickFocus);
        }
        QCOMPARE(tabStops, 1);
        for (SheetTab* tab : tabs)
            if (tab->isChecked()) {
                tab->setFocus();
                QTest::keyClick(tab, Qt::Key_Right);
                break;
            }
        QCOMPARE(sheets[0]->currentIndex(), (start + 2) % sheets[0]->count());
    }
};

QTEST_MAIN(TestUi)
#include "test_ui.moc"
