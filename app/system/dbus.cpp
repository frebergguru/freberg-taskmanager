#include "dbus.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QFileInfo>

namespace fg {

namespace {

const QString kSystemd = QStringLiteral("org.freedesktop.systemd1");
const QString kManagerPath = QStringLiteral("/org/freedesktop/systemd1");
const QString kManager = QStringLiteral("org.freedesktop.systemd1.Manager");
const QString kLogin = QStringLiteral("org.freedesktop.login1");

QDBusConnection busFor(bool userBus)
{
    return userBus ? QDBusConnection::sessionBus() : QDBusConnection::systemBus();
}

QDBusMessage managerCall(const QString& method)
{
    return QDBusMessage::createMethodCall(kSystemd, kManagerPath, kManager, method);
}

// Starting or stopping can wait on a password prompt, so allow it plenty of time.
constexpr int kActionTimeoutMs = 120000;

void whenAnswered(const QDBusConnection& bus, const QDBusMessage& call, QObject* context, DBusDone done)
{
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(call, kActionTimeoutMs), context);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context, [watcher, done = std::move(done)] {
        watcher->deleteLater();
        done(watcher->isError() ? watcher->error().message() : QString());
    });
}

} // namespace

QDBusArgument& operator<<(QDBusArgument& out, const DBusUnit& u)
{
    out.beginStructure();
    out << u.name << u.description << u.loadState << u.activeState << u.subState << u.following << u.path << u.jobId << u.jobType << u.jobPath;
    out.endStructure();
    return out;
}

const QDBusArgument& operator>>(const QDBusArgument& in, DBusUnit& u)
{
    in.beginStructure();
    in >> u.name >> u.description >> u.loadState >> u.activeState >> u.subState >> u.following >> u.path >> u.jobId >> u.jobType >> u.jobPath;
    in.endStructure();
    return in;
}

QDBusArgument& operator<<(QDBusArgument& out, const DBusUnitFile& f)
{
    out.beginStructure();
    out << f.path << f.state;
    out.endStructure();
    return out;
}

const QDBusArgument& operator>>(const QDBusArgument& in, DBusUnitFile& f)
{
    in.beginStructure();
    in >> f.path >> f.state;
    in.endStructure();
    return in;
}

QDBusArgument& operator<<(QDBusArgument& out, const DBusSession& s)
{
    out.beginStructure();
    out << s.id << s.uid << s.user << s.seat << s.path;
    out.endStructure();
    return out;
}

const QDBusArgument& operator>>(const QDBusArgument& in, DBusSession& s)
{
    in.beginStructure();
    in >> s.id >> s.uid >> s.user >> s.seat >> s.path;
    in.endStructure();
    return in;
}

void registerDBusTypes()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    qDBusRegisterMetaType<DBusUnit>();
    qDBusRegisterMetaType<QList<DBusUnit>>();
    qDBusRegisterMetaType<DBusUnitFile>();
    qDBusRegisterMetaType<QList<DBusUnitFile>>();
    qDBusRegisterMetaType<DBusSession>();
    qDBusRegisterMetaType<QList<DBusSession>>();
}

bool systemdAvailable()
{
    return QFileInfo::exists(QStringLiteral("/run/systemd/system"));
}

QList<DBusUnit> listUnits(bool userBus, QString* error)
{
    registerDBusTypes();
    const QDBusReply<QList<DBusUnit>> reply = busFor(userBus).call(managerCall(QStringLiteral("ListUnits")));
    if (!reply.isValid()) {
        *error = reply.error().message();
        return {};
    }
    return reply.value();
}

QList<DBusUnitFile> listUnitFiles(bool userBus, QString* error)
{
    registerDBusTypes();
    const QDBusReply<QList<DBusUnitFile>> reply = busFor(userBus).call(managerCall(QStringLiteral("ListUnitFiles")));
    if (!reply.isValid()) {
        *error = reply.error().message();
        return {};
    }
    return reply.value();
}

void manageUnit(bool userBus, const char* method, const QString& unit, QObject* context, DBusDone done)
{
    QDBusMessage call = managerCall(QLatin1String(method));
    call.setInteractiveAuthorizationAllowed(true);
    call << unit << QStringLiteral("replace");
    whenAnswered(busFor(userBus), call, context, std::move(done));
}

void setUnitEnabled(bool userBus, const QString& unit, bool enabled, QObject* context, DBusDone done)
{
    QDBusMessage call = managerCall(enabled ? QStringLiteral("EnableUnitFiles") : QStringLiteral("DisableUnitFiles"));
    call.setInteractiveAuthorizationAllowed(true);
    if (enabled)
        call << QStringList{unit} << false << false;
    else
        call << QStringList{unit} << false;
    whenAnswered(busFor(userBus), call, context, [userBus, context, done = std::move(done)](const QString& error) mutable {
        if (!error.isEmpty()) {
            done(error);
            return;
        }
        // This has its own permission, so it may ask too.
        QDBusMessage reload = managerCall(QStringLiteral("Reload"));
        reload.setInteractiveAuthorizationAllowed(true);
        whenAnswered(busFor(userBus), reload, context, std::move(done));
    });
}

QList<DBusSession> listSessions(QString* error)
{
    registerDBusTypes();
    QDBusMessage call = QDBusMessage::createMethodCall(kLogin, QStringLiteral("/org/freedesktop/login1"), QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("ListSessions"));
    const QDBusReply<QList<DBusSession>> reply = QDBusConnection::systemBus().call(call);
    if (!reply.isValid()) {
        *error = reply.error().message();
        return {};
    }
    return reply.value();
}

void sessionCall(const QDBusObjectPath& session, const char* method, QObject* context, DBusDone done)
{
    QDBusMessage call = QDBusMessage::createMethodCall(kLogin, session.path(), QStringLiteral("org.freedesktop.login1.Session"), QLatin1String(method));
    call.setInteractiveAuthorizationAllowed(true);
    whenAnswered(QDBusConnection::systemBus(), call, context, std::move(done));
}

} // namespace fg
