#include "mainwindow.h"

#include "../format.h"
#include "../settings.h"
#include "../style/appearance.h"
#include "../style/style.h"
#include "../widgets/icons.h"
#include "../popout/popout.h"
#include "menubar.h"
#include "options.h"
#include "screensaver.h"
#include "sheet.h"
#include "titlebar.h"
#include "tray.h"

#include <QActionGroup>
#include <QCoreApplication>
#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QScreen>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWindow>
#include <qdrawutil.h>
#include <sys/resource.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace fg {

namespace {
constexpr int kFrameWidth = 4;
constexpr int kBaseWidth = 1000;
constexpr int kBaseHeight = 720;
}

MainWindow::MainWindow(SamplerHub& hub) : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint), m_hub(hub), m_defs(allViews())
{
    setWindowTitle(QStringLiteral("Freberg.guru - Task manager"));
    setWindowIcon(Icons::app());
    setMouseTracking(true);

    m_title = new TitleBar(this);
    m_menus = new HoverMenuBar(this);
    m_tools = new QToolBar(this);
    m_sheet = new Sheet(this);
    m_status = new QStatusBar(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kFrameWidth, kFrameWidth, kFrameWidth, kFrameWidth);
    layout->setSpacing(1);
    layout->addWidget(m_title);
    layout->addWidget(m_menus);
    layout->addWidget(m_tools);
    layout->addWidget(m_sheet, 1);
    layout->addWidget(m_status);

    buildMenus();
    buildToolBar();
    buildStatusBar();

    // Views are not created until opened, only their tabs exist.
    m_views.fill(nullptr, m_defs.size());
    for (const ViewDef& def : std::as_const(m_defs))
        m_sheet->addPage(def.title, new QWidget);

    connect(m_sheet, &Sheet::currentChanged, this, &MainWindow::onCurrentChanged);
    connect(&m_hub, &SamplerHub::sampled, this, &MainWindow::onSampled);
    connect(&Settings::instance(), &Settings::changed, this, [this](const QString& key) {
        if (key.startsWith(QLatin1String("appearance/")))
            applyAppearance();
        else if (key == QLatin1String("app/updateMs"))
            m_hub.setIntervalMs(Settings::instance().updateMs());
    });

    m_hub.setIntervalMs(Settings::instance().updateMs());
    if (Settings::instance().closeToTray() || Settings::instance().value("app/trayMeter", false).toBool() || Settings::instance().value("alerts/cpuPercent", 0).toInt() > 0
        || Settings::instance().value("alerts/memoryPercent", 0).toInt() > 0 || Settings::instance().value("alerts/temperatureC", 0).toInt() > 0)
        m_tray = new TrayAndAlerts(m_hub, this);
    connect(&Settings::instance(), &Settings::changed, this, [this](const QString& key) {
        if (!m_tray && (key.startsWith(QLatin1String("alerts/")) || key == QLatin1String("app/closeToTray") || key == QLatin1String("app/trayMeter")))
            m_tray = new TrayAndAlerts(m_hub, this);
    });
    resize(kBaseWidth, kBaseHeight);
    applyAppearance();
    m_sheet->setCurrentIndex(Settings::instance().value("app/lastView", 0).toInt());
}

MainWindow::~MainWindow()
{
    m_hub.clearWanted(this);
}

void MainWindow::openView(int index)
{
    m_sheet->setCurrentIndex(index);
}

void MainWindow::applyAppearance()
{
    fg::applyAppearance();
    const double scale = Settings::instance().textPercent() / 100.0;
    const int iconSize = qRound(22 * scale);
    m_tools->setIconSize(QSize(iconSize, iconSize));
    m_title->updateGeometry();
    m_title->update();

    // Larger text needs a larger window, as far as the screen allows.
    layout()->activate();
    if (!isMaximized()) {
        QSize wanted = QSize(qRound(kBaseWidth * scale), qRound(kBaseHeight * scale)).expandedTo(minimumSizeHint());
        if (const QScreen* where = screen())
            wanted = wanted.boundedTo(where->availableSize());
        if (wanted.width() > width() || wanted.height() > height())
            resize(wanted.expandedTo(size()));
    }
    update();
}

void MainWindow::buildMenus()
{
    QMenu* file = m_menus->addMenu(tr("&File"));
    file->addAction(tr("&New Task (Run...)"), this, &MainWindow::runNewTask);
    file->addSeparator();
    file->addAction(tr("E&xit"), QKeySequence(Qt::ALT | Qt::Key_F4), this, &QWidget::close);

    QMenu* options = m_menus->addMenu(tr("&Options"));
    QAction* onTop = options->addAction(tr("&Always On Top"));
    onTop->setCheckable(true);
    onTop->setChecked(Settings::instance().value("app/alwaysOnTop", false).toBool());
    connect(onTop, &QAction::toggled, this, [this](bool on) {
        Settings::instance().setValue("app/alwaysOnTop", on);
        setWindowFlag(Qt::WindowStaysOnTopHint, on);
        show();
    });
    options->addSeparator();
    options->addAction(tr("&Settings..."), this, &MainWindow::showOptions);

    QMenu* view = m_menus->addMenu(tr("&View"));
    view->addAction(tr("&Refresh Now"), QKeySequence(Qt::Key_F5), this, [this] {
        m_hub.refreshNow();
        if (m_currentIndex >= 0 && m_views[m_currentIndex])
            m_views[m_currentIndex]->refresh();
    });
    view->addAction(tr("&Popout Readout"), QKeySequence(Qt::Key_F11), this, &MainWindow::togglePopout);
    view->addAction(tr("&Full-Screen Load"), QKeySequence(Qt::Key_F12), this, &MainWindow::showScreensaver);
    view->addSeparator();
    QMenu* themes = view->addMenu(tr("&Theme"));
    auto* familyGroup = new QActionGroup(this);
    auto* modeGroup = new QActionGroup(this);
    const auto refreshThemeMenu = [familyGroup, modeGroup] {
        const Scheme& current = schemeById(Settings::instance().scheme());
        for (QAction* action : familyGroup->actions())
            action->setChecked(action->data().toString() == current.family);
        for (QAction* action : modeGroup->actions())
            action->setChecked(action->data().toBool() == current.isDark);
    };
    for (const QString& family : schemeFamilies()) {
        QAction* action = themes->addAction(QCoreApplication::translate("fg::Scheme", schemeFor(family, false).name));
        action->setCheckable(true);
        action->setData(family);
        familyGroup->addAction(action);
        connect(action, &QAction::triggered, this, [family] {
            Settings::instance().setValue("appearance/scheme", schemeFor(family, schemeById(Settings::instance().scheme()).isDark).id);
        });
    }
    themes->addSeparator();
    for (const bool dark : {false, true}) {
        QAction* action = themes->addAction(dark ? tr("&Dark") : tr("&Light"));
        action->setCheckable(true);
        action->setData(dark);
        modeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [dark] {
            Settings::instance().setValue("appearance/scheme", schemeFor(schemeById(Settings::instance().scheme()).family, dark).id);
        });
    }
    refreshThemeMenu();
    connect(&Settings::instance(), &Settings::changed, themes, [refreshThemeMenu](const QString& key) {
        if (key == QLatin1String("appearance/scheme"))
            refreshThemeMenu();
    });
    QMenu* speed = view->addMenu(tr("&Update Speed"));
    auto* speeds = new QActionGroup(this);
    const struct {
        const char* label;
        int ms;
    } choices[] = {{QT_TR_NOOP("&High (every 1/2 second)"), 500}, {QT_TR_NOOP("&Normal (every second)"), 1000}, {QT_TR_NOOP("&Low (every 4 seconds)"), 4000}, {QT_TR_NOOP("&Paused"), 0}};
    for (const auto& choice : choices) {
        QAction* action = speed->addAction(tr(choice.label));
        action->setCheckable(true);
        action->setChecked(Settings::instance().updateMs() == choice.ms);
        speeds->addAction(action);
        connect(action, &QAction::triggered, this, [this, ms = choice.ms] { setUpdateSpeed(ms); });
    }

    QMenu* help = m_menus->addMenu(tr("&Help"));
    help->addAction(tr("&Accessibility Statement..."), this, &MainWindow::showAccessibility);
    help->addAction(tr("Check for &Updates..."), this, &MainWindow::checkForUpdates);
    help->addSeparator();
    help->addAction(tr("&About..."), this, &MainWindow::showAbout);
}

void MainWindow::buildToolBar()
{
    m_tools->setFloatable(false);
    m_tools->setMovable(false);
    m_tools->addAction(Icons::refresh(), tr("Refresh Now"), this, [this] {
        m_hub.refreshNow();
        if (m_currentIndex >= 0 && m_views[m_currentIndex])
            m_views[m_currentIndex]->refresh();
    });
    m_tools->addSeparator();
    m_tools->addAction(Icons::settings(), tr("Settings"), this, &MainWindow::showOptions);
    m_tools->addAction(Icons::popout(), tr("Popout Readout"), this, &MainWindow::togglePopout);
}

void MainWindow::buildStatusBar()
{
    m_status->setSizeGripEnabled(false);
    m_statusCpu = new QLabel(this);
    m_statusMemory = new QLabel(this);
    m_statusSelf = new QLabel(this);
    m_status->addWidget(m_statusCpu);
    m_status->addWidget(m_statusMemory);
    m_status->addWidget(m_statusSelf, 1);
}

void MainWindow::onCurrentChanged(int index)
{
    if (m_currentIndex >= 0 && m_views[m_currentIndex])
        m_views[m_currentIndex]->closed();
#ifdef __GLIBC__
    ::malloc_trim(0); // what the page just dropped goes back to the system
#endif
    m_currentIndex = index;
    Settings::instance().setValue("app/lastView", index);

    if (!m_views[index]) {
        m_views[index] = m_defs[index].create(m_hub);
        m_sheet->setPage(index, m_views[index]);
        connect(m_views[index], &View::wantsChanged, this, &MainWindow::updateWanted);
        connect(m_views[index], &View::openProcess, this, [this](const ProcessId& id) {
            openView(kProcessesView);
            m_views[kProcessesView]->goToProcess(id);
        });
    }
    m_views[index]->opened();
    updateWanted();
}

bool MainWindow::isOnScreen() const
{
    const QWindow* handle = windowHandle();
    return isVisible() && !isMinimized() && handle && handle->isExposed();
}

void MainWindow::updateWanted()
{
    Subsystems wanted = SubNone;
    if (isOnScreen()) {
        wanted = SubCpu | SubMemory;
        if (m_currentIndex >= 0 && m_views[m_currentIndex])
            wanted |= m_views[m_currentIndex]->wants();
    }
    m_hub.setWanted(this, wanted);
}

void MainWindow::onSampled(const SnapshotPtr& snapshot)
{
    if (!isOnScreen())
        return;
    if (snapshot->collected & SubCpu)
        m_statusCpu->setText(tr("CPU Usage: %1").arg(Format::percent(snapshot->cpu.usagePct)));
    if (snapshot->collected & SubMemory)
        m_statusMemory->setText(tr("Memory: %1 / %2").arg(Format::bytes(snapshot->memory.used), Format::bytes(snapshot->memory.total)));

    if (Settings::instance().selfMonitor()) {
        rusage usage{};
        ::getrusage(RUSAGE_SELF, &usage);
        const double cpuSeconds = static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) + static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1e6;
        m_statusSelf->setText(tr("This app: %1 s CPU, peak %2").arg(QLocale().toString(cpuSeconds, 'f', 1), Format::bytes(static_cast<std::uint64_t>(usage.ru_maxrss) * 1024)));
    } else if (!m_statusSelf->text().isEmpty()) {
        m_statusSelf->clear();
    }

    if (m_currentIndex >= 0 && m_views[m_currentIndex])
        m_views[m_currentIndex]->onSnapshotShared(snapshot);
}

void MainWindow::togglePopout()
{
    if (!m_popout)
        m_popout = new Popout(m_hub);
    m_popout->setVisible(!m_popout->isVisible());
}

void MainWindow::showScreensaver()
{
    if (!m_screensaver)
        m_screensaver = new Screensaver(m_hub);
    m_screensaver->showFullScreen();
}

void MainWindow::runNewTask()
{
    bool accepted = false;
    const QString command = QInputDialog::getText(this, tr("Create New Task"), tr("Type the name of a program to open:"), QLineEdit::Normal, QString(), &accepted);
    if (!accepted || command.trimmed().isEmpty())
        return;
    QStringList parts = QProcess::splitCommand(command);
    if (parts.isEmpty())
        return;
    const QString program = parts.takeFirst();
    if (!QProcess::startDetached(program, parts))
        QMessageBox::warning(this, tr("Create New Task"), tr("Could not start %1.").arg(program));
}

void MainWindow::checkForUpdates()
{
    // No release address exists yet, so nothing is contacted.
    QMessageBox::information(this, tr("Check for Updates"), tr("This build has no release address set, so it can't check for updates. It sends nothing over the network."));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (Settings::instance().closeToTray() && m_tray && m_tray->iconShown()) {
        hide();
        event->ignore();
        return;
    }
    if (m_popout)
        m_popout->close();
    if (m_screensaver)
        m_screensaver->close();
    QApplication::quit();
    event->accept();
}

void MainWindow::setUpdateSpeed(int intervalMs)
{
    Settings::instance().setValue("app/updateMs", intervalMs);
}

void MainWindow::showOptions()
{
    OptionsDialog dialog(this);
    dialog.exec();
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About"), tr("<b>Freberg.guru - Task manager</b><br>Version %1<br><br>Sends no usage data, hardware details or process information anywhere.").arg(QApplication::applicationVersion()));
}

void MainWindow::showAccessibility()
{
    QMessageBox::information(this, tr("Accessibility Statement"),
                             tr("This app aims for full keyboard use, screen reader support through AT-SPI, WCAG AA contrast in every colour scheme, text size from 100% to 200%, and no information conveyed by colour or sound alone.\n\nKnown limits are listed in the README."));
}

Qt::Edges MainWindow::edgesAt(const QPoint& position) const
{
    if (isMaximized())
        return {};
    Qt::Edges edges;
    if (position.x() < kFrameWidth)
        edges |= Qt::LeftEdge;
    if (position.x() >= width() - kFrameWidth)
        edges |= Qt::RightEdge;
    if (position.y() < kFrameWidth)
        edges |= Qt::TopEdge;
    if (position.y() >= height() - kFrameWidth)
        edges |= Qt::BottomEdge;
    return edges;
}

void MainWindow::mousePressEvent(QMouseEvent* event)
{
    const Qt::Edges edges = edgesAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && edges && windowHandle())
        windowHandle()->startSystemResize(edges);
}

void MainWindow::mouseMoveEvent(QMouseEvent* event)
{
    const Qt::Edges edges = edgesAt(event->position().toPoint());
    if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge))
        setCursor(Qt::SizeFDiagCursor);
    else if (edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge))
        setCursor(Qt::SizeBDiagCursor);
    else if (edges & (Qt::LeftEdge | Qt::RightEdge))
        setCursor(Qt::SizeHorCursor);
    else if (edges & (Qt::TopEdge | Qt::BottomEdge))
        setCursor(Qt::SizeVerCursor);
    else
        unsetCursor();
}

void MainWindow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QBrush face = palette().brush(QPalette::Window);
    if (currentScheme().design == Design::Classic) {
        qDrawWinButton(&p, rect(), palette(), false, &face);
        return;
    }
    // Flat: one thin line, and the same width of edge to grab for resizing.
    p.fillRect(rect(), face);
    p.setPen(currentScheme().design == Design::Neon ? currentScheme().accent : currentScheme().border);
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange) {
        const int margin = isMaximized() ? 0 : kFrameWidth;
        layout()->setContentsMargins(margin, margin, margin, margin);
        m_title->syncState();
        updateWanted();
    } else if (event->type() == QEvent::ActivationChange) {
        m_title->update();
    }
    QWidget::changeEvent(event);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (QWindow* handle = windowHandle())
        handle->installEventFilter(this); // installing twice is harmless, Qt keeps one
    updateWanted();
}

void MainWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    updateWanted();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    // Covered or minimized: the compositor stops exposing us, so sampling stops too.
    if (watched == windowHandle() && event->type() == QEvent::Expose)
        QMetaObject::invokeMethod(this, &MainWindow::updateWanted, Qt::QueuedConnection);
    return QWidget::eventFilter(watched, event);
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space && event->modifiers() == Qt::AltModifier) {
        m_title->showSystemMenu(mapToGlobal(m_title->geometry().bottomLeft()));
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace fg
