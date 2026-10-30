#include "installedapps.h"

#include "../format.h"

#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace fg {

namespace {

constexpr int kKeyRole = Qt::UserRole;
constexpr int kSizeRole = Qt::UserRole + 1;

struct App {
    QString name, version, source, description, key;
    qint64 size = -1; // -1 means this source does not tell us
};

// Reads pacman's database directly, which is far cheaper than starting pacman.
void readPacman(QList<App>& out)
{
    const QString base = QStringLiteral("/var/lib/pacman/local/");
    for (const QString& folder : QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile file(base + folder + QStringLiteral("/desc"));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        App app;
        app.source = QStringLiteral("pacman");
        QString section;
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.startsWith(QLatin1Char('%'))) {
                section = line;
            } else if (!line.isEmpty()) {
                if (section == QLatin1String("%NAME%"))
                    app.name = line;
                else if (section == QLatin1String("%VERSION%"))
                    app.version = line;
                else if (section == QLatin1String("%DESC%"))
                    app.description = line;
                else if (section == QLatin1String("%SIZE%"))
                    app.size = line.toLongLong();
            }
        }
        app.key = app.name;
        if (!app.name.isEmpty())
            out << app;
    }
}

void readDpkg(QList<App>& out)
{
    QFile file(QStringLiteral("/var/lib/dpkg/status"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    App app;
    bool installed = false;
    const auto finish = [&] {
        if (installed && !app.name.isEmpty()) {
            app.source = QStringLiteral("dpkg");
            app.key = app.name;
            out << app;
        }
        app = {};
        installed = false;
    };
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty())
            finish();
        else if (line.startsWith(QLatin1String("Package: ")))
            app.name = line.mid(9);
        else if (line.startsWith(QLatin1String("Version: ")))
            app.version = line.mid(9);
        else if (line.startsWith(QLatin1String("Description: ")))
            app.description = line.mid(13);
        else if (line.startsWith(QLatin1String("Installed-Size: ")))
            app.size = line.mid(16).toLongLong() * 1024;
        else if (line == QLatin1String("Status: install ok installed"))
            installed = true;
    }
    finish();
}

QString runTool(const QString& program, const QStringList& arguments)
{
    if (QStandardPaths::findExecutable(program).isEmpty())
        return {};
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(60000))
        return {};
    return QString::fromLocal8Bit(process.readAllStandardOutput());
}

void readRpm(QList<App>& out)
{
    const QString text = runTool(QStringLiteral("rpm"), {QStringLiteral("-qa"), QStringLiteral("--qf"), QStringLiteral("%{NAME}\\t%{VERSION}\\t%{SIZE}\\t%{SUMMARY}\\n")});
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QStringList f = line.split(QLatin1Char('\t'));
        if (f.size() == 4)
            out << App{f[0], f[1], QStringLiteral("rpm"), f[3], f[0], f[2].toLongLong()};
    }
}

void readFlatpak(QList<App>& out)
{
    const QString text = runTool(QStringLiteral("flatpak"), {QStringLiteral("list"), QStringLiteral("--app"), QStringLiteral("--columns=name,application,version,description")});
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QStringList f = line.split(QLatin1Char('\t'));
        if (f.size() >= 2)
            out << App{f[0], f.value(2), QStringLiteral("flatpak"), f.value(3), f[1], -1};
    }
}

void readSnap(QList<App>& out)
{
    const QString text = runTool(QStringLiteral("snap"), {QStringLiteral("list")});
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QStringList f = lines[i].split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (f.size() >= 2)
            out << App{f[0], f[1], QStringLiteral("snap"), QString(), f[0], -1};
    }
}

void readAppImages(QList<App>& out)
{
    for (const QString& place : {QDir::homePath() + QStringLiteral("/Applications"), QDir::homePath() + QStringLiteral("/.local/bin")}) {
        const QFileInfoList files = QDir(place).entryInfoList({QStringLiteral("*.AppImage"), QStringLiteral("*.appimage")}, QDir::Files);
        for (const QFileInfo& file : files)
            out << App{file.completeBaseName(), QString(), QStringLiteral("AppImage"), file.absoluteFilePath(), file.absoluteFilePath(), file.size()};
    }
}

// What removes an app. Empty when it must be done by hand.
QStringList uninstallCommand(const QString& source, const QString& key)
{
    if (source == QLatin1String("pacman"))
        return {QStringLiteral("pkexec"), QStringLiteral("pacman"), QStringLiteral("-R"), QStringLiteral("--noconfirm"), key};
    if (source == QLatin1String("dpkg"))
        return {QStringLiteral("pkexec"), QStringLiteral("apt-get"), QStringLiteral("remove"), QStringLiteral("-y"), key};
    if (source == QLatin1String("rpm"))
        return {QStringLiteral("pkexec"), QStringLiteral("rpm"), QStringLiteral("-e"), key};
    if (source == QLatin1String("flatpak"))
        return {QStringLiteral("flatpak"), QStringLiteral("uninstall"), QStringLiteral("-y"), key};
    if (source == QLatin1String("snap"))
        return {QStringLiteral("pkexec"), QStringLiteral("snap"), QStringLiteral("remove"), key};
    return {};
}

// Sorts sizes as numbers, though they are shown as text.
class AppItem final : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem& other) const override
    {
        const int column = treeWidget()->sortColumn();
        if (column == 3)
            return data(0, kSizeRole).toLongLong() < other.data(0, kSizeRole).toLongLong();
        return text(column).compare(other.text(column), Qt::CaseInsensitive) < 0;
    }
};

} // namespace

InstalledAppsView::InstalledAppsView() : m_list(new QTreeWidget), m_filter(new QLineEdit), m_message(new QLabel), m_uninstall(new QPushButton(tr("&Uninstall...")))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* top = new QHBoxLayout;
    auto* label = new QLabel(tr("&Filter:"));
    label->setBuddy(m_filter);
    m_filter->setAccessibleName(tr("Filter installed apps"));
    top->addWidget(label);
    top->addWidget(m_filter, 1);
    layout->addLayout(top);

    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setHeaderLabels({tr("Name"), tr("Version"), tr("Source"), tr("Size"), tr("Description")});
    m_list->setAccessibleName(tr("Installed apps"));
    m_list->setColumnWidth(0, fontMetrics().averageCharWidth() * 28);
    layout->addWidget(m_list, 1);

    auto* bottom = new QHBoxLayout;
    m_message->setWordWrap(true);
    bottom->addWidget(m_message, 1);
    bottom->addWidget(m_uninstall);
    layout->addLayout(bottom);

    connect(m_filter, &QLineEdit::textChanged, this, &InstalledAppsView::applyFilter);
    connect(m_uninstall, &QPushButton::clicked, this, &InstalledAppsView::uninstall);
}

void InstalledAppsView::closed()
{
    ++m_generation;
    m_list->clear();
}

void InstalledAppsView::refresh()
{
    const int generation = ++m_generation;
    m_list->clear();
    m_message->setText(tr("Reading the list of installed apps..."));

    // Package tools can take seconds, so this never runs on the window's thread.
    auto results = std::make_shared<QList<App>>();
    QThread* thread = QThread::create([results] {
        readPacman(*results);
        readDpkg(*results);
        readRpm(*results);
        readFlatpak(*results);
        readSnap(*results);
        readAppImages(*results);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, results, generation] {
        if (generation != m_generation)
            return;
        m_list->setSortingEnabled(false);
        for (const App& app : std::as_const(*results)) {
            auto* item = new AppItem(m_list, {app.name, app.version, app.source, app.size >= 0 ? Format::bytes(static_cast<std::uint64_t>(app.size)) : Format::why(Availability::Unsupported), app.description});
            item->setData(0, kKeyRole, app.key);
            item->setData(0, kSizeRole, app.size);
        }
        m_list->setSortingEnabled(true);
        m_list->sortByColumn(0, Qt::AscendingOrder);
        applyFilter();
        m_message->setText(tr("%n app(s) installed.", nullptr, static_cast<int>(results->size())));
    });
    thread->start(QThread::LowPriority);
}

void InstalledAppsView::applyFilter()
{
    const QString filter = m_filter->text();
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_list->topLevelItem(i);
        item->setHidden(!filter.isEmpty() && !item->text(0).contains(filter, Qt::CaseInsensitive) && !item->text(4).contains(filter, Qt::CaseInsensitive));
    }
}

void InstalledAppsView::uninstall()
{
    const QTreeWidgetItem* item = m_list->currentItem();
    if (!item) {
        m_message->setText(tr("Select an app first."));
        return;
    }
    const QString name = item->text(0);
    const QString source = item->text(2);
    const QStringList command = uninstallCommand(source, item->data(0, kKeyRole).toString());
    if (command.isEmpty()) {
        m_message->setText(tr("This kind of app is removed by deleting its file: %1").arg(item->text(4)));
        return;
    }
    if (QMessageBox::warning(this, tr("Uninstall"), tr("Uninstall %1 (%2)?\n\nThis will run:\n%3").arg(name, source, command.join(QLatin1Char(' '))), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    auto* process = new QProcess(this);
    m_uninstall->setEnabled(false);
    m_message->setText(tr("Uninstalling %1...").arg(name));
    connect(process, &QProcess::finished, this, [this, process, name](int code) {
        m_uninstall->setEnabled(true);
        m_message->setText(code == 0 ? tr("%1 was uninstalled.").arg(name) : tr("Could not uninstall %1: %2").arg(name, QString::fromLocal8Bit(process->readAllStandardError()).trimmed()));
        process->deleteLater();
        if (code == 0)
            refresh();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, name](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_uninstall->setEnabled(true);
            m_message->setText(tr("Could not uninstall %1: the tool it needs is not installed.").arg(name));
            process->deleteLater();
        }
    });
    process->start(command.first(), command.mid(1));
}

} // namespace fg
