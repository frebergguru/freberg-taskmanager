#include "startup.h"

#include "../format.h"
#include "../system/dbus.h"

#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTextStream>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace fg {

namespace {

constexpr int kKindRole = Qt::UserRole;
constexpr int kKeyRole = Qt::UserRole + 1;

enum Kind { AutostartFile, UserUnit };

struct DesktopEntry {
    QString name, exec;
    bool enabled = true;
    QStringList onlyShowIn, notShowIn;
};

// A plain reader: QSettings would rewrite keys and lose localized names.
DesktopEntry readDesktop(const QString& path, bool* found)
{
    DesktopEntry entry;
    QFile file(path);
    *found = file.open(QIODevice::ReadOnly | QIODevice::Text);
    if (!*found)
        return entry;
    bool inEntry = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (!inEntry)
            continue;
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;
        const QStringView key = QStringView(line).left(equals);
        const QString value = line.mid(equals + 1);
        if (key == QLatin1String("Name"))
            entry.name = value;
        else if (key == QLatin1String("Exec"))
            entry.exec = value;
        else if (key == QLatin1String("OnlyShowIn"))
            entry.onlyShowIn = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        else if (key == QLatin1String("NotShowIn"))
            entry.notShowIn = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        else if (key == QLatin1String("Hidden") && value == QLatin1String("true"))
            entry.enabled = false;
        else if (key == QLatin1String("X-GNOME-Autostart-enabled") && value == QLatin1String("false"))
            entry.enabled = false;
    }
    return entry;
}

// A desktop skips an entry that names other desktops, however it is otherwise set.
bool runsOnThisDesktop(const DesktopEntry& entry)
{
    const QStringList desktops = qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (desktops.isEmpty())
        return true; // nothing to go by
    const auto names = [&desktops](const QStringList& list) { return std::any_of(desktops.begin(), desktops.end(), [&list](const QString& desktop) { return list.contains(desktop); }); };
    return (entry.onlyShowIn.isEmpty() || names(entry.onlyShowIn)) && !names(entry.notShowIn);
}

QString userAutostartDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/autostart");
}

// Disabling never touches the system file. A user copy with Hidden=true overrides it.
QString writeOverride(const QString& fileName, bool enabled)
{
    QDir().mkpath(userAutostartDir());
    const QString target = userAutostartDir() + QLatin1Char('/') + fileName;
    QString source = target;
    if (!QFile::exists(source))
        source = QStringLiteral("/etc/xdg/autostart/") + fileName;

    QFile in(source);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
        return in.errorString();
    QStringList lines;
    bool inEntry = false, written = false;
    while (!in.atEnd()) {
        const QString line = QString::fromUtf8(in.readLine()).chopped(0).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            if (inEntry && !written) {
                lines << QStringLiteral("Hidden=%1").arg(enabled ? "false" : "true");
                written = true;
            }
            inEntry = line == QLatin1String("[Desktop Entry]");
        }
        if (inEntry && line.startsWith(QLatin1String("Hidden="))) {
            lines << QStringLiteral("Hidden=%1").arg(enabled ? "false" : "true");
            written = true;
            continue;
        }
        if (inEntry && enabled && line == QLatin1String("X-GNOME-Autostart-enabled=false"))
            continue;
        lines << line;
    }
    in.close();
    if (inEntry && !written)
        lines << QStringLiteral("Hidden=%1").arg(enabled ? "false" : "true");

    QFile out(target);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return out.errorString();
    out.write(lines.join(QLatin1Char('\n')).toUtf8());
    out.write("\n");
    return {};
}

} // namespace

StartupView::StartupView()
    : m_list(new QTreeWidget), m_message(new QLabel), m_enable(new QPushButton(tr("&Enable"))), m_disable(new QPushButton(tr("&Disable"))),
      m_measure(new QPushButton(tr("&Measure Times")))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_list->setRootIsDecorated(false);
    m_list->setSortingEnabled(true);
    m_list->setHeaderLabels({tr("Name"), tr("Status"), tr("Source"), tr("Start time"), tr("Command")});
    m_list->setAccessibleName(tr("Startup items"));
    m_list->setColumnWidth(0, fontMetrics().averageCharWidth() * 32);
    m_list->sortByColumn(0, Qt::AscendingOrder);
    layout->addWidget(m_list, 1);

    auto* bottom = new QHBoxLayout;
    m_message->setWordWrap(true);
    bottom->addWidget(m_message, 1);
    bottom->addWidget(m_measure);
    bottom->addWidget(m_enable);
    bottom->addWidget(m_disable);
    layout->addLayout(bottom);

    m_measure->setToolTip(tr("Asks systemd how long user services took at the last start."));
    connect(m_enable, &QPushButton::clicked, this, [this] { toggleSelected(true); });
    connect(m_disable, &QPushButton::clicked, this, [this] { toggleSelected(false); });
    connect(m_measure, &QPushButton::clicked, this, &StartupView::measureTimes);
}

void StartupView::closed()
{
    m_list->clear();
}

void StartupView::refresh()
{
    m_list->clear();

    // The user's file wins over the system file with the same name.
    QHash<QString, QString> files;
    for (const QString& dir : {QStringLiteral("/etc/xdg/autostart"), userAutostartDir()})
        for (const QString& name : QDir(dir).entryList({QStringLiteral("*.desktop")}, QDir::Files))
            files.insert(name, dir + QLatin1Char('/') + name);

    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        bool found = false;
        const DesktopEntry entry = readDesktop(it.value(), &found);
        if (!found)
            continue;
        auto* item = new QTreeWidgetItem(m_list, {entry.name.isEmpty() ? it.key() : entry.name, !entry.enabled ? tr("Disabled") : (runsOnThisDesktop(entry) ? tr("Enabled") : tr("Not on this desktop")), tr("Autostart file"), Format::why(Availability::NotYetSampled), entry.exec});
        item->setData(0, kKindRole, AutostartFile);
        item->setData(0, kKeyRole, it.key());
        item->setText(3, QString());
    }

    if (systemdAvailable()) {
        QString error;
        for (const DBusUnitFile& file : listUnitFiles(true, &error)) {
            if (!file.path.endsWith(QLatin1String(".service")) || file.path.contains(QLatin1Char('@')))
                continue;
            if (file.state != QLatin1String("enabled") && file.state != QLatin1String("disabled"))
                continue;
            const QString name = QFileInfo(file.path).fileName();
            auto* item = new QTreeWidgetItem(m_list, {name, file.state == QLatin1String("enabled") ? tr("Enabled") : tr("Disabled"), tr("User service"), QString(), file.path});
            item->setData(0, kKindRole, UserUnit);
            item->setData(0, kKeyRole, name);
        }
        if (!error.isEmpty())
            m_message->setText(tr("Could not list user services: %1").arg(error));
    }
}

void StartupView::toggleSelected(bool enabled)
{
    const QTreeWidgetItem* item = m_list->currentItem();
    if (!item) {
        m_message->setText(tr("Select an item first."));
        return;
    }
    const QString key = item->data(0, kKeyRole).toString();
    const QString name = item->text(0);
    const auto finish = [this, name, enabled](const QString& error) {
        m_message->setText(error.isEmpty() ? tr("%1 will %2 at the next login.").arg(name, enabled ? tr("start") : tr("not start")) : tr("Could not change %1: %2").arg(name, error));
        refresh();
    };
    if (item->data(0, kKindRole).toInt() == UserUnit)
        setUnitEnabled(true, key, enabled, this, finish);
    else
        finish(writeOverride(key, enabled));
}

void StartupView::measureTimes()
{
    auto* process = new QProcess(this);
    m_measure->setEnabled(false);
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_measure->setEnabled(true);
        m_message->setText(tr("Start times are not available on this system."));
        process->deleteLater();
    });
    connect(process, &QProcess::finished, this, [this, process](int code) {
        m_measure->setEnabled(true);
        if (code != 0) {
            m_message->setText(tr("Start times are not available on this system."));
            process->deleteLater();
            return;
        }
        QHash<QString, QString> times;
        const QStringList lines = QString::fromLocal8Bit(process->readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            const qsizetype cut = line.lastIndexOf(QLatin1Char(' '));
            if (cut > 0)
                times.insert(line.mid(cut + 1), line.left(cut).trimmed());
        }
        for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
            QTreeWidgetItem* item = m_list->topLevelItem(i);
            const QString time = times.value(item->data(0, kKeyRole).toString());
            item->setText(3, time.isEmpty() ? tr("Not measured") : time);
        }
        m_message->setText(tr("Start times are from the last login."));
        process->deleteLater();
    });
    process->start(QStringLiteral("systemd-analyze"), {QStringLiteral("--user"), QStringLiteral("blame")});
}

} // namespace fg
