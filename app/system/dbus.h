// Types and calls for systemd and logind over D-Bus. Connections open on first
// use, so nothing here costs anything until the Services or Users page is opened.
#pragma once

#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>

namespace fg {

struct DBusUnit {
    QString name, description, loadState, activeState, subState, following;
    QDBusObjectPath path;
    quint32 jobId = 0;
    QString jobType;
    QDBusObjectPath jobPath;
};

struct DBusUnitFile {
    QString path, state;
};

struct DBusSession {
    QString id;
    quint32 uid = 0;
    QString user, seat;
    QDBusObjectPath path;
};

QDBusArgument& operator<<(QDBusArgument& out, const DBusUnit& unit);
const QDBusArgument& operator>>(const QDBusArgument& in, DBusUnit& unit);
QDBusArgument& operator<<(QDBusArgument& out, const DBusUnitFile& file);
const QDBusArgument& operator>>(const QDBusArgument& in, DBusUnitFile& file);
QDBusArgument& operator<<(QDBusArgument& out, const DBusSession& session);
const QDBusArgument& operator>>(const QDBusArgument& in, DBusSession& session);

void registerDBusTypes();

// An empty string means it worked; otherwise the reason it did not. These can wait on a
// password prompt, so the answer comes later and the window keeps working meanwhile. It
// is not delivered if context has gone by then.
using DBusDone = std::function<void(const QString& error)>;
void manageUnit(bool userBus, const char* method, const QString& unit, QObject* context, DBusDone done);
void setUnitEnabled(bool userBus, const QString& unit, bool enabled, QObject* context, DBusDone done);
QList<DBusUnit> listUnits(bool userBus, QString* error);
QList<DBusUnitFile> listUnitFiles(bool userBus, QString* error);
bool systemdAvailable();

QList<DBusSession> listSessions(QString* error);
void sessionCall(const QDBusObjectPath& session, const char* method, QObject* context, DBusDone done);

} // namespace fg

Q_DECLARE_METATYPE(fg::DBusUnit)
Q_DECLARE_METATYPE(fg::DBusUnitFile)
Q_DECLARE_METATYPE(fg::DBusSession)
