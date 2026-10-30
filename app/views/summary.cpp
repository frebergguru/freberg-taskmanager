#include "summary.h"

#include "../format.h"
#include "../settings.h"

#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <unistd.h>

namespace fg {

namespace {
constexpr int kTopCount = 8;
constexpr int kPidRole = Qt::UserRole;
constexpr int kStartRole = Qt::UserRole + 1;
}

SummaryView::SummaryView() : m_top(new QTreeWidget(this))
{
    auto* grid = new QGridLayout;
    grid->setSpacing(6);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(grid);

    const auto tile = [&](const QString& name, int row, int column) {
        auto* box = new QGroupBox(name, this);
        auto* boxLayout = new QVBoxLayout(box);
        Tile out;
        out.value = new QLabel(box);
        out.value->setAccessibleName(name);
        out.value->setWordWrap(true);
        out.value->setMinimumHeight(fontMetrics().lineSpacing() * 2 + 2); // read and write fill two lines
        out.value->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        out.bar = new QProgressBar(box);
        out.bar->setTextVisible(false);
        out.bar->setRange(0, 100);
        out.bar->setAccessibleName(name);
        boxLayout->addWidget(out.value);
        boxLayout->addWidget(out.bar);
        grid->addWidget(box, row, column);
        return out;
    };
    m_cpu = tile(tr("CPU"), 0, 0);
    m_memory = tile(tr("Memory"), 0, 1);
    m_disk = tile(tr("Disk"), 0, 2);
    m_network = tile(tr("Network"), 0, 3);
    m_gpu = tile(tr("GPU"), 1, 0);
    m_thermal = tile(tr("Thermals"), 1, 1);
    m_energy = tile(tr("Energy"), 1, 2);
    m_pressure = tile(tr("System Pressure"), 1, 3);

    auto* label = new QLabel(tr("&Busiest processes (double-click for details):"), this);
    label->setBuddy(m_top);
    layout->addWidget(label);
    layout->addWidget(m_top, 1);
    m_top->setRootIsDecorated(false);
    m_top->setHeaderLabels({tr("Name"), tr("PID"), tr("CPU"), tr("Memory")});
    m_top->header()->setStretchLastSection(false);
    m_top->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_top->setAccessibleName(tr("Busiest processes"));
    connect(m_top, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        emit openProcess({item->data(0, kPidRole).toInt(), item->data(0, kStartRole).toULongLong()});
    });
}

void SummaryView::onSnapshot(const Snapshot& snapshot)
{
    const auto setBar = [](const Tile& tile, double value, bool known) {
        tile.bar->setEnabled(known);
        tile.bar->setValue(known ? qBound(0, static_cast<int>(value), 100) : 0);
    };

    m_cpu.value->setText(tr("%1 at %2").arg(Format::percent(snapshot.cpu.usagePct), Format::megahertz(snapshot.cpu.freqMHz)));
    setBar(m_cpu, snapshot.cpu.usagePct.value, snapshot.cpu.usagePct.known());

    const MemorySample& memory = snapshot.memory;
    m_memory.value->setText(tr("%1 of %2").arg(Format::bytes(memory.used), Format::bytes(memory.total)));
    const bool memoryKnown = memory.used.known() && memory.total.known() && memory.total.value > 0;
    setBar(m_memory, memoryKnown ? 100.0 * static_cast<double>(memory.used.value) / static_cast<double>(memory.total.value) : 0, memoryKnown);

    double read = 0, write = 0, active = 0;
    bool diskKnown = false;
    for (const DiskSample& disk : snapshot.disks)
        if (disk.readBps.known() && disk.writeBps.known() && disk.activePct.known()) {
            read += disk.readBps.value;
            write += disk.writeBps.value;
            active = std::max(active, disk.activePct.value);
            diskKnown = true;
        }
    m_disk.value->setText(diskKnown ? tr("Read %1\nWrite %2").arg(Format::diskRate(read), Format::diskRate(write)) : Format::why(Availability::NotYetSampled));
    setBar(m_disk, active, diskKnown);

    double rx = 0, tx = 0;
    double linkUse = 0;
    bool netKnown = false;
    for (const NetworkSample& net : snapshot.networks) {
        if (net.loopback || !net.rxBps.known() || !net.txBps.known())
            continue;
        rx += net.rxBps.value;
        tx += net.txBps.value;
        netKnown = true;
        if (net.linkMbps.known() && net.linkMbps.value > 0)
            linkUse = std::max(linkUse, 100.0 * (net.rxBps.value + net.txBps.value) * 8.0 / (net.linkMbps.value * 1e6));
    }
    m_network.value->setText(netKnown ? tr("Receive %1\nSend %2").arg(Format::rate(rx), Format::rate(tx)) : Format::why(Availability::NotYetSampled));
    setBar(m_network, linkUse, netKnown);

    if (!snapshot.gpus.empty()) {
        const GpuSample& gpu = snapshot.gpus.front();
        m_gpu.value->setText(Format::percent(gpu.busyPct));
        setBar(m_gpu, gpu.busyPct.value, gpu.busyPct.known());
    } else {
        m_gpu.value->setText(Format::why(Availability::Unsupported));
        setBar(m_gpu, 0, false);
    }

    m_thermal.value->setText(tr("CPU hotspot: %1").arg(Format::temperature(snapshot.thermal.cpuHotspotC)));
    setBar(m_thermal, snapshot.thermal.cpuHotspotC.value, snapshot.thermal.cpuHotspotC.known());

    m_energy.value->setText(tr("CPU package: %1").arg(Format::watts(snapshot.power.cpuPackageW)));
    m_energy.bar->setVisible(false);

    const PressureSample& pressure = snapshot.pressure;
    m_pressure.value->setText(tr("CPU %1, memory %2, disk %3").arg(Format::percent(pressure.cpu.some10), Format::percent(pressure.memory.some10), Format::percent(pressure.io.some10)));
    const double worst = std::max({pressure.cpu.some10.value, pressure.memory.some10.value, pressure.io.some10.value});
    setBar(m_pressure, worst, pressure.cpu.some10.known() || pressure.memory.some10.known() || pressure.io.some10.known());

    std::vector<const ProcessSample*> busiest;
    busiest.reserve(snapshot.processes.size());
    const int self = ::getpid();
    const bool showSelf = Settings::instance().selfMonitor();
    for (const ProcessSample& process : snapshot.processes)
        if (process.cpuPct.known() && (showSelf || process.info->id.pid != self))
            busiest.push_back(&process);
    const std::size_t count = std::min<std::size_t>(kTopCount, busiest.size());
    std::partial_sort(busiest.begin(), busiest.begin() + static_cast<std::ptrdiff_t>(count), busiest.end(),
                      [](const ProcessSample* a, const ProcessSample* b) { return a->cpuPct.value > b->cpuPct.value; });

    while (m_top->topLevelItemCount() > static_cast<int>(count))
        delete m_top->takeTopLevelItem(m_top->topLevelItemCount() - 1);
    for (std::size_t i = 0; i < count; ++i) {
        QTreeWidgetItem* item = m_top->topLevelItem(static_cast<int>(i));
        if (!item)
            item = new QTreeWidgetItem(m_top);
        const ProcessSample& process = *busiest[i];
        item->setText(0, QString::fromStdString(process.info->name));
        item->setText(1, QString::number(process.info->id.pid));
        item->setText(2, Format::percent(process.cpuPct));
        item->setText(3, Format::bytes(process.rssBytes));
        item->setData(0, kPidRole, process.info->id.pid);
        item->setData(0, kStartRole, QVariant::fromValue<qulonglong>(process.info->id.startTicks));
    }
}

} // namespace fg
