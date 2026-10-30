// Drives the real window without showing anything. Settings and data go to a folder
// next to the test, so the user's own are never touched.
#include "app/hub.h"
#include "app/recorder.h"
#include "app/views/startup.h"
#include "core/benchmark.h"
#include "app/popout/popout.h"
#include "app/popout/skin.h"
#include "app/settings.h"
#include "app/shell/accessible.h"
#include "app/shell/mainwindow.h"
#include "app/shell/menubar.h"
#include "app/shell/sheet.h"
#include "app/style/appearance.h"
#include "app/style/style.h"
#include "app/views/processes.h"

#include <QAbstractItemModel>
#include <QAccessible>
#include <QApplication>
#include <QSettings>
#include <QAction>
#include <QCursor>
#include <QDir>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>
#include <QTreeWidget>
#include <cmath>

using namespace fg;

namespace {
// Runs before anything reads a path, so that the user's own settings are never touched.
const bool g_isolated = [] {
    static QTemporaryDir home(QDir::currentPath() + "/fixture-home-XXXXXX");
    qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
    return home.isValid();
}();
} // namespace

class TestUi : public QObject {
    Q_OBJECT
    SamplerHub* m_hub = nullptr;

private slots:
    void initTestCase()
    {
        QVERIFY(g_isolated);
        QVERIFY(Settings::instance().fileName().contains(QLatin1String("fixture-home")));
        installAccessibility();
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
        window.openView(5); // System Info
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

    void everyControlOnEveryPageHasARoleAndAName()
    {
        MainWindow window(*m_hub);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QStringList unnamed;
        int checked = 0;
        for (int view = 0; view < window.viewCount(); ++view) {
            window.openView(view);
            QTest::qWait(30);
            const std::function<void(QAccessibleInterface*)> walk = [&](QAccessibleInterface* item) {
                if (!item || !item->isValid())
                    return;
                const QAccessible::Role role = item->role();
                const bool interactive = role == QAccessible::PushButton || role == QAccessible::Button || role == QAccessible::PageTab || role == QAccessible::ComboBox
                    || role == QAccessible::EditableText || role == QAccessible::CheckBox || role == QAccessible::RadioButton || role == QAccessible::Slider
                    || role == QAccessible::SpinBox || role == QAccessible::Tree || role == QAccessible::List || role == QAccessible::Table || role == QAccessible::Chart;
                if (interactive && !item->state().invisible) {
                    ++checked;
                    if (item->text(QAccessible::Name).trimmed().isEmpty()) {
                        const QObject* object = item->object();
                        unnamed << QStringLiteral("%1: %2 (%3)").arg(window.viewTitle(view), object ? QString::fromLatin1(object->metaObject()->className()) : QStringLiteral("?")).arg(int(role));
                    }
                }
                // Rows of a list are many and are named by their cells; the list itself is what counts.
                if (role == QAccessible::Tree || role == QAccessible::List || role == QAccessible::Table)
                    return;
                for (int child = 0; child < item->childCount(); ++child)
                    walk(item->child(child));
            };
            walk(QAccessible::queryAccessibleInterface(&window));
        }
        QVERIFY(checked > 50);
        unnamed.removeDuplicates();
        QVERIFY2(unnamed.isEmpty(), qPrintable(unnamed.join(QStringLiteral("; "))));
    }

    void settingsReallyReachTheFile()
    {
        // Every group the app writes must read back from a separate object, as after a restart.
        Settings& settings = Settings::instance();
        const QString file = QSettings(QSettings::IniFormat, QSettings::UserScope, "Freberg.guru", "TaskManager").fileName();
        for (const char* key : {"app/language", "app/updateMs", "appearance/textPercent", "popout/skin", "processes/sortColumn", "alerts/cpuPercent", "appHistory/enabled", "performance/selected"}) {
            const QVariant before = settings.value(QLatin1String(key));
            settings.setValue(QLatin1String(key), QStringLiteral("marker"));
            settings.sync();
            QSettings reader(file, QSettings::IniFormat);
            QCOMPARE(reader.value(QLatin1String(key)).toString(), QStringLiteral("marker"));
            settings.setValue(QLatin1String(key), before);
        }
        settings.sync();
    }

    void flightRecorderRecordsSavesAndReopens()
    {
        FlightRecorder& recorder = FlightRecorder::shared(*m_hub);
        recorder.clear();
        recorder.start();
        QCOMPARE(m_hub->wanted() & SubAll, Subsystems(SubAll));
        m_hub->refreshNow();
        QTRY_VERIFY_WITH_TIMEOUT(recorder.buffer().frames().size() >= 2, 5000);
        recorder.stop();
        QCOMPARE(m_hub->wanted(), Subsystems(SubNone));

        const QString path = QDir::currentPath() + "/fixture-home-recording.fgtrace";
        std::string error;
        QVERIFY2(saveTrace(path.toStdString(), recorder.buffer().frames(), error), error.c_str());
        std::deque<TraceFrame> loaded;
        QVERIFY2(loadTrace(path.toStdString(), loaded, error), error.c_str());
        QFile::remove(path);
        QCOMPARE(loaded.size(), recorder.buffer().frames().size());
        QVERIFY(!std::isnan(loaded.back().cpuPct));
        QVERIFY(!loaded.back().top.empty());
    }

    void benchmarksGiveNumbersAndCanBeCancelled()
    {
        std::atomic<bool> go{false};
        QVERIFY(benchCpu(1, 0.05, go) > 0);
        QVERIFY(benchMemoryBandwidth(1024 * 1024, 0.05, go) > 0);
        QVERIFY(benchMemoryLatency(1024 * 1024, 0.05, go) > 0);
        const auto disk = benchDisk(QDir::currentPath().toStdString(), 4 * 1024 * 1024, go);
        QVERIFY(disk.has_value());
        QVERIFY(disk->readMBs > 0 && disk->writeMBs > 0);
        QVERIFY(!benchDisk("/nonexistent-folder", 1024 * 1024, go).has_value());
        std::atomic<bool> stop{true};
        QCOMPARE(benchCpu(1, 10.0, stop), 0.0); // stops at once
    }

    void startupToggleWritesAUserOverrideOnly()
    {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/autostart";
        QVERIFY(dir.contains(QLatin1String("fixture-home")));
        QDir().mkpath(dir);
        QFile file(dir + "/fixture-thing.desktop");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[Desktop Entry]\nName=Fixture Thing\nExec=/bin/true\n");
        file.close();

        StartupView view;
        view.opened();
        const auto lists = view.findChildren<QTreeWidget*>();
        QCOMPARE(lists.size(), 1);
        QTreeWidgetItem* found = nullptr;
        for (int i = 0; i < lists[0]->topLevelItemCount(); ++i)
            if (lists[0]->topLevelItem(i)->text(0) == QLatin1String("Fixture Thing"))
                found = lists[0]->topLevelItem(i);
        QVERIFY(found);
        lists[0]->setCurrentItem(found);
        const auto buttons = view.findChildren<QPushButton*>();
        const auto disable = std::find_if(buttons.begin(), buttons.end(), [](QPushButton* b) { return b->text().contains(QLatin1String("Disable")); });
        QVERIFY(disable != buttons.end());
        (*disable)->click();

        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("Hidden=true"));
    }

    void everyThemeOpensEveryPageAndCanBeLeft()
    {
        MainWindow window(*m_hub);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto sheet = window.findChild<Sheet*>();
        QVERIFY(sheet);
        for (const Scheme& scheme : schemes()) {
            Settings::instance().setValue("appearance/scheme", scheme.id);
            window.applyAppearance();
            for (int view = 0; view < window.viewCount(); ++view) {
                window.openView(view);
                QVERIFY(!window.grab().isNull()); // draws every page in this design
            }
            // Every tab must stay inside the page's own area in every design.
            for (const SheetTab* tab : sheet->findChildren<SheetTab*>())
                QVERIFY2(sheet->rect().contains(tab->geometry()), qPrintable(scheme.id + QLatin1Char(' ') + tab->text()));
        }
        Settings::instance().setValue("appearance/scheme", QStringLiteral("standard"));
        window.applyAppearance();
    }

    void thermalsSitsBetweenPerformanceAndProcesses()
    {
        MainWindow window(*m_hub);
        QCOMPARE(window.viewTitle(1), QStringLiteral("Performance"));
        QCOMPARE(window.viewTitle(2), QStringLiteral("Thermals"));
        QCOMPARE(window.viewTitle(kProcessesView), QStringLiteral("Processes"));
    }

    void thermalsPageShowsEverySensorAndSamplesOnlyThat()
    {
        MainWindow window(*m_hub);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.openView(2);
        QTest::qWait(30);
        QVERIFY(m_hub->wanted() & SubThermal);
        QVERIFY(!(m_hub->wanted() & SubProcesses));
        const auto trees = window.findChildren<QTreeWidget*>();
        QTreeWidget* sensors = nullptr;
        for (QTreeWidget* tree : trees)
            if (tree->accessibleName().startsWith(QLatin1String("Temperature sensors")))
                sensors = tree;
        QVERIFY(sensors);
        m_hub->refreshNow();
        const int expected = static_cast<int>(m_hub->backend().sample(SubThermal)->thermal.sensors.size());
        QTRY_COMPARE_WITH_TIMEOUT(sensors->topLevelItemCount(), expected, 3000);
        if (expected > 0) {
            // Choosing a sensor keeps its row, and refreshing must not lose the choice.
            sensors->setCurrentItem(sensors->topLevelItem(expected - 1));
            m_hub->refreshNow();
            QTest::qWait(100);
            QCOMPARE(sensors->currentItem(), sensors->topLevelItem(expected - 1));
        }
    }

    void menusFollowThePointerWithNoClicks()
    {
        MainWindow window(*m_hub);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* bar = window.findChild<HoverMenuBar*>();
        QVERIFY(bar);
        const QList<QAction*> top = bar->actions();
        QVERIFY(top.size() >= 4);

        // Pointing opens the menu.
        QAction* viewAction = top[2];
        QTest::mouseMove(bar, bar->actionGeometry(viewAction).center());
        QTRY_VERIFY_WITH_TIMEOUT(viewAction->menu()->isVisible(), 2000);

        // A submenu opens from pointing at it as well.
        QMenu* view = viewAction->menu();
        QAction* theme = nullptr;
        for (QAction* action : view->actions())
            if (action->menu() && action->text().contains(QLatin1String("Theme")))
                theme = action;
        QVERIFY(theme);
        QTest::mouseMove(view, view->actionGeometry(theme).center());
        QTRY_VERIFY_WITH_TIMEOUT(theme->menu()->isVisible(), 2000);
        QTest::mouseMove(view, view->actionGeometry(view->actions().first()).center());
        QTRY_VERIFY_WITH_TIMEOUT(!theme->menu()->isVisible(), 3000);

        // Moving well away closes everything.
        QCursor::setPos(window.mapToGlobal(QPoint(window.width() / 2, window.height() - 20)));
        QTRY_VERIFY_WITH_TIMEOUT(!view->isVisible(), 3000);
        QVERIFY(!QApplication::activePopupWidget());
    }

    void passingOverTheBarOpensNothing()
    {
        MainWindow window(*m_hub);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* bar = window.findChild<HoverMenuBar*>();
        QTest::mouseMove(bar, bar->actionGeometry(bar->actions()[0]).center());
        QCursor::setPos(window.mapToGlobal(QPoint(window.width() / 2, window.height() - 20))); // gone again at once
        QTest::qWait(300);
        QVERIFY(!QApplication::activePopupWidget());
    }

    void aMenuOpenedByKeyboardStaysOpenWherever()
    {
        MainWindow window(*m_hub);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* bar = window.findChild<HoverMenuBar*>();
        QCursor::setPos(window.mapToGlobal(QPoint(window.width() / 2, window.height() - 20)));
        bar->setActiveAction(bar->actions()[1]);
        QTRY_VERIFY(bar->actions()[1]->menu()->isVisible());
        QTest::qWait(700); // past the close delay
        QVERIFY(bar->actions()[1]->menu()->isVisible());
        bar->actions()[1]->menu()->close();
    }

    void hoverCanBeTurnedOff()
    {
        Settings::instance().setValue("app/hoverMenus", false);
        MainWindow window(*m_hub);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* bar = window.findChild<HoverMenuBar*>();
        QTest::mouseMove(bar, bar->actionGeometry(bar->actions()[0]).center());
        QTest::qWait(300);
        QVERIFY(!QApplication::activePopupWidget());
        Settings::instance().setValue("app/hoverMenus", true);
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
        const QSize small = window.minimumSizeHint();
        Settings::instance().setValue("appearance/textPercent", 200);
        window.applyAppearance();
        QTest::qWait(20);
        // How far the window itself grows depends on the screen. What it needs must grow, and it must get that.
        QVERIFY(window.minimumSizeHint().height() > small.height());
        QVERIFY(window.width() >= window.minimumSizeHint().width());
        QVERIFY(window.height() >= window.minimumSizeHint().height());
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
