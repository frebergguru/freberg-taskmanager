#include "apphistory.h"

#include "../format.h"
#include "../hub.h"
#include "../settings.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace fg {

namespace {

const QString kConnection = QStringLiteral("apphistory");
constexpr int kFlushMs = 60000;

AppHistoryLog* g_log = nullptr; // one per process, shared by the view and the launch check

AppHistoryLog& sharedLog(SamplerHub& hub)
{
    if (!g_log)
        g_log = new AppHistoryLog(hub, &hub);
    return *g_log;
}

} // namespace

AppHistoryLog::AppHistoryLog(SamplerHub& hub, QObject* parent) : QObject(parent), m_hub(hub)
{
    m_logicalCpus = std::max(1, m_hub.backend().topology().logical);
}

AppHistoryLog::~AppHistoryLog()
{
    flush();
}

bool AppHistoryLog::openDatabase()
{
    if (m_opened)
        return true;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConnection);
    db.setDatabaseName(dir + QStringLiteral("/apphistory.sqlite"));
    if (!db.open())
        return false;
    QSqlQuery(db).exec(QStringLiteral("CREATE TABLE IF NOT EXISTS apps (app TEXT PRIMARY KEY, cpu REAL NOT NULL, disk REAL NOT NULL, first INTEGER NOT NULL, last INTEGER NOT NULL)"));
    m_opened = true;
    return true;
}

void AppHistoryLog::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    Settings::instance().setValue("appHistory/enabled", enabled);
    if (enabled) {
        openDatabase();
        connect(&m_hub, &SamplerHub::sampled, this, &AppHistoryLog::onSampled, Qt::UniqueConnection);
        m_hub.setWanted(this, SubProcesses);
        if (!m_flushTimer) {
            m_flushTimer = new QTimer(this);
            m_flushTimer->setTimerType(Qt::VeryCoarseTimer);
            connect(m_flushTimer, &QTimer::timeout, this, &AppHistoryLog::flush);
        }
        m_flushTimer->start(kFlushMs);
    } else {
        flush();
        disconnect(&m_hub, &SamplerHub::sampled, this, &AppHistoryLog::onSampled);
        m_hub.clearWanted(this);
        if (m_flushTimer)
            m_flushTimer->stop();
        m_previousMs = 0;
    }
}

void AppHistoryLog::onSampled(const SnapshotPtr& snapshot)
{
    if (!(snapshot->collected & SubProcesses))
        return;
    const std::int64_t now = snapshot->monotonicMs;
    const double seconds = m_previousMs ? static_cast<double>(now - m_previousMs) / 1000.0 : 0;
    m_previousMs = now;
    if (seconds <= 0)
        return;
    for (const ProcessSample& process : snapshot->processes) {
        Pending& pending = m_pending[QString::fromStdString(process.info->name)];
        // cpuPct is a share of the whole machine, so it scales with the processor count.
        if (process.cpuPct.known())
            pending.cpuSeconds += process.cpuPct.value / 100.0 * m_logicalCpus * seconds;
        if (process.readBps.known())
            pending.diskBytes += process.readBps.value * seconds;
        if (process.writeBps.known())
            pending.diskBytes += process.writeBps.value * seconds;
    }
}

void AppHistoryLog::flush()
{
    if (m_pending.isEmpty() || !openDatabase())
        return;
    QSqlDatabase db = QSqlDatabase::database(kConnection);
    db.transaction();
    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT INTO apps (app, cpu, disk, first, last) VALUES (?, ?, ?, ?, ?) "
                                 "ON CONFLICT(app) DO UPDATE SET cpu = cpu + excluded.cpu, disk = disk + excluded.disk, last = excluded.last"));
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (auto it = m_pending.constBegin(); it != m_pending.constEnd(); ++it) {
        if (it.value().cpuSeconds <= 0 && it.value().diskBytes <= 0)
            continue;
        query.addBindValue(it.key());
        query.addBindValue(it.value().cpuSeconds);
        query.addBindValue(it.value().diskBytes);
        query.addBindValue(now);
        query.addBindValue(now);
        query.exec();
    }
    db.commit();
    m_pending.clear();
}

void AppHistoryLog::clearAll()
{
    m_pending.clear();
    if (openDatabase())
        QSqlQuery(QSqlDatabase::database(kConnection)).exec(QStringLiteral("DELETE FROM apps"));
}

QList<AppHistoryLog::Row> AppHistoryLog::rows()
{
    flush();
    QList<Row> out;
    if (!openDatabase())
        return out;
    QSqlQuery query(QSqlDatabase::database(kConnection));
    query.exec(QStringLiteral("SELECT app, cpu, disk, first, last FROM apps ORDER BY cpu DESC"));
    while (query.next())
        out.append({query.value(0).toString(), query.value(1).toDouble(), query.value(2).toDouble(), query.value(3).toLongLong(), query.value(4).toLongLong()});
    return out;
}

void startAppHistoryIfEnabled(SamplerHub& hub)
{
    if (Settings::instance().value("appHistory/enabled", false).toBool())
        sharedLog(hub).setEnabled(true);
}

AppHistoryView::AppHistoryView(SamplerHub& hub)
    : m_log(&sharedLog(hub)), m_enabled(new QCheckBox(tr("&Keep app history (walks all processes on each update, even when this window is hidden)"))), m_list(new QTreeWidget), m_message(new QLabel)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_enabled->setChecked(m_log->isEnabled());
    layout->addWidget(m_enabled);

    m_list->setRootIsDecorated(false);
    m_list->setSortingEnabled(true);
    m_list->setHeaderLabels({tr("App"), tr("CPU time"), tr("Disk"), tr("Network"), tr("First seen"), tr("Last seen")});
    m_list->setAccessibleName(tr("App history"));
    m_list->setColumnWidth(0, fontMetrics().averageCharWidth() * 28);
    layout->addWidget(m_list, 1);

    auto* bottom = new QHBoxLayout;
    m_message->setWordWrap(true);
    bottom->addWidget(m_message, 1);
    auto* clear = new QPushButton(tr("&Delete History"));
    bottom->addWidget(clear);
    layout->addLayout(bottom);

    connect(m_enabled, &QCheckBox::toggled, this, [this](bool on) {
        m_log->setEnabled(on);
        refresh();
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        if (QMessageBox::warning(this, tr("Delete History"), tr("Delete all app history?"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
            m_log->clearAll();
            refresh();
        }
    });
}

void AppHistoryView::closed()
{
    m_list->clear();
}

void AppHistoryView::refresh()
{
    m_list->clear();
    const QList<AppHistoryLog::Row> rows = m_log->rows();
    const QLocale locale;
    for (const AppHistoryLog::Row& row : rows) {
        const int minutes = static_cast<int>(row.cpuSeconds / 60);
        const QString cpu = minutes > 0 ? tr("%1 h %2 min").arg(minutes / 60).arg(minutes % 60) : tr("%1 s").arg(locale.toString(row.cpuSeconds, 'f', 1));
        new QTreeWidgetItem(m_list, {row.app, cpu, Format::bytes(static_cast<std::uint64_t>(row.diskBytes)), tr("Not measured"),
                                     locale.toString(QDateTime::fromSecsSinceEpoch(row.firstSeen), QLocale::ShortFormat),
                                     locale.toString(QDateTime::fromSecsSinceEpoch(row.lastSeen), QLocale::ShortFormat)});
    }
    if (rows.isEmpty())
        m_message->setText(m_log->isEnabled() ? tr("Collecting. Figures appear within a minute.") : tr("App history is off, so nothing is being collected."));
    else
        m_message->clear();
}

} // namespace fg
