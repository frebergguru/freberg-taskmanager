// The notification area icon with a small live meter, and threshold alerts. Each
// exists only when the user has turned it on, so by default neither costs anything.
#pragma once

#include "../hub.h"

#include <QElapsedTimer>
#include <QObject>

class QSystemTrayIcon;
class QWidget;

namespace fg {

struct AlertRule {
    const char* settingsKey;
    const char* title;
    int level = 0; // 0 is off
    qint64 overSinceMs = -1;
    bool fired = false;
};

class TrayAndAlerts final : public QObject {
    Q_OBJECT
public:
    TrayAndAlerts(SamplerHub& hub, QWidget* window);
    ~TrayAndAlerts() override;

    bool iconShown() const;
    void reconfigure();

signals:
    void exitRequested();

private:
    void onSampled(const SnapshotPtr& snapshot);
    void paintIcon(double cpu);
    void checkRule(AlertRule& rule, bool known, double value, const QString& message, qint64 nowMs);
    void notify(const QString& title, const QString& message);

    SamplerHub& m_hub;
    QWidget* m_window;
    QSystemTrayIcon* m_icon = nullptr;
    AlertRule m_cpu{"alerts/cpuPercent", QT_TR_NOOP("High CPU use")};
    AlertRule m_memory{"alerts/memoryPercent", QT_TR_NOOP("High memory use")};
    AlertRule m_temperature{"alerts/temperatureC", QT_TR_NOOP("High temperature")};
    int m_holdMs = 10000;
    int m_lastIconLevel = -1;
};

} // namespace fg
