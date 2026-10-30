#include "dbus.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
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

QString manageUnit(bool userBus, const char* method, const QString& unit)
{
    QDBusMessage call = managerCall(QLatin1String(method));
    call.setInteractiveAuthorizationAllowed(true);
    call << unit << QStringLiteral("replace");
    const QDBusMessage reply = busFor(userBus).call(call, QDBus::Block, kActionTimeoutMs);
    return reply.type() == QDBusMessage::ErrorMessage ? reply.errorMessage() : QString();
}

QString setUnitEnabled(bool userBus, const QString& unit, bool enabled)
{
    QDBusMessage call = managerCall(enabled ? QStringLiteral("EnableUnitFiles") : QStringLiteral("DisableUnitFiles"));
    call.setInteractiveAuthorizationAllowed(true);
    if (enabled)
        call << QStringList{unit} << false << false;
    else
        call << QStringList{unit} << false;
    QDBusMessage reply = busFor(userBus).call(call, QDBus::Block, kActionTimeoutMs);
    if (reply.type() == QDBusMessage::ErrorMessage)
        return reply.errorMessage();
    reply = busFor(userBus).call(managerCall(QStringLiteral("Reload")), QDBus::Block, kActionTimeoutMs);
    return reply.type() == QDBusMessage::ErrorMessage ? reply.errorMessage() : QString();
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

QString sessionCall(const QDBusObjectPath& session, const char* method)
{
    QDBusMessage call = QDBusMessage::createMethodCall(kLogin, session.path(), QStringLiteral("org.freedesktop.login1.Session"), QLatin1String(method));
    call.setInteractiveAuthorizationAllowed(true);
    const QDBusMessage reply = QDBusConnection::systemBus().call(call, QDBus::Block, kActionTimeoutMs);
    return reply.type() == QDBusMessage::ErrorMessage ? reply.errorMessage() : QString();
}

} // namespace fg
