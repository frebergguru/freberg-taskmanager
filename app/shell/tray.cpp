#include "tray.h"

#include "../format.h"
#include "../settings.h"
#include "../widgets/icons.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QMenu>
#include <QPainter>
#include <QSystemTrayIcon>
#include <QWidget>

namespace fg {

TrayAndAlerts::TrayAndAlerts(SamplerHub& hub, QWidget* window) : QObject(window), m_hub(hub), m_window(window)
{
    connect(&m_hub, &SamplerHub::sampled, this, &TrayAndAlerts::onSampled);
    connect(&Settings::instance(), &Settings::changed, this, [this](const QString& key) {
        if (key.startsWith(QLatin1String("alerts/")) || key == QLatin1String("app/closeToTray") || key == QLatin1String("app/trayMeter"))
            reconfigure();
    });
    reconfigure();
}

TrayAndAlerts::~TrayAndAlerts()
{
    m_hub.clearWanted(this);
}

bool TrayAndAlerts::iconShown() const
{
    return m_icon && m_icon->isVisible();
}

void TrayAndAlerts::reconfigure()
{
    const Settings& settings = Settings::instance();
    for (AlertRule* rule : {&m_cpu, &m_memory, &m_temperature}) {
        rule->level = settings.value(QLatin1String(rule->settingsKey), 0).toInt();
        rule->overSinceMs = -1;
        rule->fired = false;
    }
    m_holdMs = settings.value("alerts/holdSeconds", 10).toInt() * 1000;

    const bool meter = settings.value("app/trayMeter", false).toBool();
    const bool wantIcon = settings.closeToTray() || meter;
    if (wantIcon && !m_icon && QSystemTrayIcon::isSystemTrayAvailable()) {
        m_icon = new QSystemTrayIcon(Icons::app(), this);
        m_icon->setToolTip(QStringLiteral("Freberg.guru - Task manager"));
        auto* menu = new QMenu(m_window);
        menu->addAction(tr("&Show"), m_window, [this] {
            m_window->showNormal();
            m_window->raise();
            m_window->activateWindow();
        });
        menu->addSeparator();
        menu->addAction(tr("E&xit"), qApp, &QApplication::quit);
        m_icon->setContextMenu(menu);
        connect(m_icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason != QSystemTrayIcon::Trigger)
                return;
            m_window->setVisible(!m_window->isVisible());
            if (m_window->isVisible()) {
                m_window->raise();
                m_window->activateWindow();
            }
        });
        m_icon->show();
    } else if (!wantIcon && m_icon) {
        delete m_icon;
        m_icon = nullptr;
    }

    Subsystems wanted = SubNone;
    if (meter && m_icon)
        wanted |= SubCpu;
    if (m_cpu.level > 0)
        wanted |= SubCpu;
    if (m_memory.level > 0)
        wanted |= SubMemory;
    if (m_temperature.level > 0)
        wanted |= SubThermal;
    m_hub.setWanted(this, wanted);
}

void TrayAndAlerts::paintIcon(double cpu)
{
    // A new icon goes over D-Bus, so only send one when the bar really moves.
    const int level = static_cast<int>(cpu / 100.0 * 14.0 + 0.5);
    if (level == m_lastIconLevel)
        return;
    m_lastIconLevel = level;
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::black);
    QPainter p(&pixmap);
    p.fillRect(1, 15 - level, 14, level, QColor(0, 255, 0));
    p.end();
    m_icon->setIcon(QIcon(pixmap));
    m_icon->setToolTip(tr("CPU Usage: %1").arg(Format::percent(cpu)));
}

void TrayAndAlerts::checkRule(AlertRule& rule, bool known, double value, const QString& message, qint64 nowMs)
{
    if (rule.level <= 0 || !known)
        return;
    if (value < rule.level) {
        rule.overSinceMs = -1;
        rule.fired = false;
        return;
    }
    if (rule.overSinceMs < 0)
        rule.overSinceMs = nowMs;
    if (!rule.fired && nowMs - rule.overSinceMs >= m_holdMs) {
        rule.fired = true; // once per episode, not once per update
        notify(tr(rule.title), message);
    }
}

void TrayAndAlerts::onSampled(const SnapshotPtr& snapshot)
{
    const Subsystems got = snapshot->collected;
    const qint64 now = snapshot->monotonicMs;
    if (got & SubCpu) {
        if (m_icon && snapshot->cpu.usagePct.known() && Settings::instance().value("app/trayMeter", false).toBool())
            paintIcon(snapshot->cpu.usagePct.value);
        checkRule(m_cpu, snapshot->cpu.usagePct.known(), snapshot->cpu.usagePct.value, tr("CPU use is at %1.").arg(Format::percent(snapshot->cpu.usagePct)), now);
    }
    if (got & SubMemory) {
        const MemorySample& m = snapshot->memory;
        const bool known = m.used.known() && m.total.known() && m.total.value > 0;
        const double share = known ? 100.0 * static_cast<double>(m.used.value) / static_cast<double>(m.total.value) : 0;
        checkRule(m_memory, known, share, tr("Memory use is at %1.").arg(Format::percent(share)), now);
    }
    if (got & SubThermal)
        checkRule(m_temperature, snapshot->thermal.cpuHotspotC.known(), snapshot->thermal.cpuHotspotC.value, tr("The processor is at %1.").arg(Format::temperature(snapshot->thermal.cpuHotspotC)), now);
}

void TrayAndAlerts::notify(const QString& title, const QString& message)
{
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"), QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    call << QStringLiteral("Freberg.guru - Task manager") << quint32(0) << QStringLiteral("utilities-system-monitor") << title << message << QStringList() << QVariantMap() << -1;
    if (!QDBusConnection::sessionBus().send(call) && m_icon)
        m_icon->showMessage(title, message);
}

} // namespace fg
