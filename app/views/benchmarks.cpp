#include "benchmarks.h"

#include "../widgets/resultsdb.h"
#include "core/benchmark.h"

#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtSql/QSqlQuery>
#include <thread>

namespace fg {

namespace {

constexpr double kCpuSeconds = 3.0;
constexpr double kMemorySeconds = 2.0;
constexpr std::size_t kMemoryBytes = 256u * 1024 * 1024;
constexpr std::size_t kLatencyBytes = 64u * 1024 * 1024;
constexpr std::size_t kDiskBytes = 256u * 1024 * 1024;

struct Results {
    double cpuSingle = 0, cpuMulti = 0, memoryMBs = 0, latencyNs = 0, diskReadMBs = -1, diskWriteMBs = -1;
};

} // namespace

BenchmarksView::BenchmarksView()
    : m_folder(new QLineEdit(QDir::homePath())), m_run(new QPushButton(tr("&Run"))), m_cancel(new QPushButton(tr("&Cancel"))), m_bar(new QProgressBar), m_message(new QLabel), m_results(new QTreeWidget)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* info = new QLabel(tr("Runs short tests of the processor, memory and disk. It takes about 15 seconds, and the computer will be busy meanwhile. Scores can be compared between runs of this app only."));
    info->setWordWrap(true);
    layout->addWidget(info);

    auto* top = new QHBoxLayout;
    auto* label = new QLabel(tr("&Disk test folder:"));
    label->setBuddy(m_folder);
    m_folder->setAccessibleName(tr("Disk test folder"));
    top->addWidget(label);
    top->addWidget(m_folder, 1);
    top->addWidget(m_run);
    top->addWidget(m_cancel);
    layout->addLayout(top);

    m_bar->setRange(0, 5);
    m_bar->setValue(0);
    m_bar->setTextVisible(false);
    m_bar->setAccessibleName(tr("Benchmark progress"));
    layout->addWidget(m_bar);
    m_message->setWordWrap(true);
    layout->addWidget(m_message);

    m_results->setRootIsDecorated(false);
    m_results->setHeaderLabels({tr("Finished"), tr("CPU single"), tr("CPU all cores"), tr("Memory copy"), tr("Memory latency"), tr("Disk read"), tr("Disk write")});
    m_results->setAccessibleName(tr("Saved results"));
    layout->addWidget(m_results, 1);

    m_cancel->setEnabled(false);
    connect(m_run, &QPushButton::clicked, this, &BenchmarksView::start);
    connect(m_cancel, &QPushButton::clicked, this, [this] { m_cancelFlag = true; });
}

BenchmarksView::~BenchmarksView()
{
    m_cancelFlag = true;
    if (m_thread)
        m_thread->wait();
}

void BenchmarksView::closed()
{
    m_cancelFlag = true;
    m_results->clear();
    if (!m_thread)
        closeResultsDb();
}

void BenchmarksView::start()
{
    if (m_thread)
        return;
    const QString folder = QDir(m_folder->text()).absolutePath();
    m_cancelFlag = false;
    m_run->setEnabled(false);
    m_cancel->setEnabled(true);
    m_bar->setValue(0);
    m_message->setText(tr("Testing the processor, one core..."));

    auto results = std::make_shared<Results>();
    const std::string path = folder.toStdString();
    m_thread = QThread::create([this, results, path] {
        const auto step = [this](int value, const QString& text) {
            QMetaObject::invokeMethod(this, [this, value, text] {
                m_bar->setValue(value);
                m_message->setText(text);
            });
        };
        results->cpuSingle = benchCpu(1, kCpuSeconds, m_cancelFlag);
        step(1, tr("Testing the processor, all cores..."));
        results->cpuMulti = benchCpu(static_cast<int>(std::max(1u, std::thread::hardware_concurrency())), kCpuSeconds, m_cancelFlag);
        step(2, tr("Testing memory speed..."));
        results->memoryMBs = benchMemoryBandwidth(kMemoryBytes, kMemorySeconds, m_cancelFlag);
        step(3, tr("Testing memory latency..."));
        results->latencyNs = benchMemoryLatency(kLatencyBytes, kMemorySeconds, m_cancelFlag);
        step(4, tr("Testing the disk..."));
        if (const auto disk = benchDisk(path, kDiskBytes, m_cancelFlag)) {
            results->diskReadMBs = disk->readMBs;
            results->diskWriteMBs = disk->writeMBs;
        }
    });
    connect(m_thread, &QThread::finished, this, [this, results] {
        m_thread->deleteLater();
        m_thread = nullptr;
        m_run->setEnabled(true);
        m_cancel->setEnabled(false);
        if (m_cancelFlag) {
            m_bar->setValue(0);
            m_message->setText(tr("Cancelled. Nothing was saved."));
            return;
        }
        m_bar->setValue(5);
        m_message->setText(results->diskReadMBs < 0 ? tr("Finished, but the disk test could not write to that folder.") : tr("Finished."));

        QSqlQuery query = resultsQuery();
        query.prepare(QStringLiteral("INSERT INTO benchmarks VALUES (?, ?, ?, ?, ?, ?, ?)"));
        query.addBindValue(QDateTime::currentSecsSinceEpoch());
        query.addBindValue(results->cpuSingle);
        query.addBindValue(results->cpuMulti);
        query.addBindValue(results->memoryMBs);
        query.addBindValue(results->latencyNs);
        query.addBindValue(results->diskReadMBs);
        query.addBindValue(results->diskWriteMBs);
        query.exec();
        loadResults();
    });
    m_thread->start(QThread::NormalPriority);
}

void BenchmarksView::loadResults()
{
    m_results->clear();
    QSqlQuery query = resultsQuery();
    query.exec(QStringLiteral("SELECT * FROM benchmarks ORDER BY finished DESC LIMIT 50"));
    const QLocale locale;
    const auto disk = [&](double value) { return value < 0 ? tr("Not measured") : tr("%1 MB/s").arg(locale.toString(value, 'f', 0)); };
    while (query.next())
        new QTreeWidgetItem(m_results, {locale.toString(QDateTime::fromSecsSinceEpoch(query.value(0).toLongLong()), QLocale::ShortFormat), locale.toString(query.value(1).toDouble(), 'f', 0), locale.toString(query.value(2).toDouble(), 'f', 0),
                                        tr("%1 MB/s").arg(locale.toString(query.value(3).toDouble(), 'f', 0)), tr("%1 ns").arg(locale.toString(query.value(4).toDouble(), 'f', 1)), disk(query.value(5).toDouble()), disk(query.value(6).toDouble())});
}

} // namespace fg
