// User settings, stored with QSettings. Every value is read when needed, and
// writing one emits changed() so the window can apply it at once.
#pragma once

#include <QObject>
#include <QSettings>
#include <QVariant>

namespace fg {

class Settings final : public QObject {
    Q_OBJECT
public:
    static Settings& instance();

    QVariant value(const QString& key, const QVariant& fallback = {}) const { return m_store.value(key, fallback); }
    void setValue(const QString& key, const QVariant& value);
    void sync() { m_store.sync(); }

    QString scheme() const { return value("appearance/scheme", "standard").toString(); }
    int saturation() const { return value("appearance/saturation", 10).toInt(); }
    int textPercent() const { return qBound(100, value("appearance/textPercent", 100).toInt(), 200); }
    bool followSystem() const { return value("appearance/followSystem", false).toBool(); }
    int updateMs() const { return value("app/updateMs", 1000).toInt(); }
    QString language() const { return value("app/language", "").toString(); }
    bool fahrenheit() const { return value("app/fahrenheit", false).toBool(); }
    bool networkBits() const { return value("app/networkBits", false).toBool(); }
    bool rowHighlight() const { return value("app/rowHighlight", true).toBool(); }
    bool closeToTray() const { return value("app/closeToTray", false).toBool(); }
    bool selfMonitor() const { return value("app/selfMonitor", false).toBool(); }

signals:
    void changed(const QString& key);

private:
    Settings();
    QSettings m_store;
};

} // namespace fg
