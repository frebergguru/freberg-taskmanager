#include "performance.h"

#include "../format.h"
#include "../hub.h"
#include "../settings.h"
#include "../widgets/graph.h"
#include "../widgets/infogrid.h"

#include <QComboBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QProgressBar>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace fg {

namespace {

const QString kCpu = QStringLiteral("cpu");
const QString kMemory = QStringLiteral("memory");
const QString kThermal = QStringLiteral("thermal");
const QString kEnergy = QStringLiteral("energy");
const QString kAllDisks = QStringLiteral("disks");

template <class T>
double valueOr0(const Reading<T>& reading)
{
    return reading.known() ? static_cast<double>(reading.value) : 0.0;
}

} // namespace

PerformanceView::PerformanceView(SamplerHub& hub) : m_hub(hub), m_list(new QListWidget(this)), m_stack(new QStackedWidget(this))
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_list);
    layout->addWidget(m_stack, 1);
    m_list->setFixedWidth(fontMetrics().averageCharWidth() * 26);
    m_list->setAccessibleName(tr("Device"));
    connect(m_list, &QListWidget::currentRowChanged, this, &PerformanceView::selectionChanged);
}

void PerformanceView::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::FontChange)
        m_list->setFixedWidth(fontMetrics().averageCharWidth() * 26);
    View::changeEvent(event);
}

Subsystems PerformanceView::wants() const
{
    Subsystems wanted = SubCpu | SubMemory | SubDisk | SubNetwork | SubGpu | SubThermal | SubPower | SubPressure;
    // Naming who uses the energy means walking all processes, so only on that page.
    const QListWidgetItem* item = m_list->currentItem();
    if (item && item->data(Qt::UserRole).toString() == kEnergy)
        wanted |= SubProcesses;
    return wanted;
}

void PerformanceView::selectionChanged()
{
    const QListWidgetItem* item = m_list->currentItem();
    if (!item)
        return;
    const QString key = item->data(Qt::UserRole).toString();
    if (Page* selected = m_byKey.value(key))
        m_stack->setCurrentWidget(selected->widget);
    Settings::instance().setValue("performance/selected", key);
    emit wantsChanged();
}

PerformanceView::Page& PerformanceView::page(const QString& key, const QString& label, const QString& heading)
{
    if (Page* existing = m_byKey.value(key))
        return *existing;

    Page& created = m_pages.emplace_back();
    created.key = key;
    created.widget = new QWidget(m_stack);
    auto* layout = new QVBoxLayout(created.widget);
    layout->setContentsMargins(6, 0, 0, 0);

    created.heading = new QLabel(heading, created.widget);
    QFont big = created.heading->font();
    big.setBold(true);
    created.heading->setFont(big);
    created.subheading = new QLabel(created.widget);
    created.subheading->setWordWrap(true);
    created.graph = new Graph(created.widget);
    created.graph->setAccessibleName(heading);
    created.graph->addSeries(&created.first);
    created.grid = new InfoGrid(created.widget);

    layout->addWidget(created.heading);
    layout->addWidget(created.subheading);
    layout->addWidget(created.graph, 1);
    layout->addWidget(created.grid);

    m_stack->addWidget(created.widget);
    m_byKey.insert(key, &created);

    auto* item = new QListWidgetItem(label, m_list);
    item->setData(Qt::UserRole, key);
    if (key == Settings::instance().value("performance/selected", kCpu).toString() || !m_list->currentItem())
        m_list->setCurrentItem(item);
    return created;
}

void PerformanceView::onSnapshot(const Snapshot& snapshot)
{
    updateCpu(snapshot);
    updateMemory(snapshot);
    updateDisks(snapshot);
    updateNetworks(snapshot);
    updateGpus(snapshot);
    updateThermals(snapshot);
    updateEnergy(snapshot);
}

void PerformanceView::updateCpu(const Snapshot& snapshot)
{
    Page& p = page(kCpu, tr("CPU"), tr("CPU"));
    if (!m_cpuMode) {
        const CpuTopology& topology = m_hub.backend().topology();
        p.subheading->setText(QString::fromStdString(topology.model));
        p.graph->setFixedMaximum(100);

        m_cpuMode = new QComboBox(p.widget);
        m_cpuMode->addItem(tr("Overall"), 0);
        m_cpuMode->addItem(tr("Logical processors"), 1);
        m_cpuMode->addItem(tr("NUMA nodes"), 2);
        m_cpuMode->setAccessibleName(tr("Graph shows"));
        m_cpuMode->setCurrentIndex(Settings::instance().value("performance/cpuMode", 0).toInt());
        connect(m_cpuMode, &QComboBox::currentIndexChanged, this, [this](int mode) {
            Settings::instance().setValue("performance/cpuMode", mode);
            rebuildCoreGraphs();
        });
        static_cast<QVBoxLayout*>(p.widget->layout())->insertWidget(2, m_cpuMode);

        m_coreArea = new QWidget(p.widget);
        m_coreGrid = new QGridLayout(m_coreArea);
        m_coreGrid->setContentsMargins(0, 0, 0, 0);
        m_coreGrid->setSpacing(2);
        static_cast<QVBoxLayout*>(p.widget->layout())->insertWidget(4, m_coreArea, 1);
        rebuildCoreGraphs();
    }

    p.first.push(snapshot.cpu.usagePct);
    p.graph->sampled(tr("CPU usage %1").arg(Format::percent(snapshot.cpu.usagePct)));

    const CpuTopology& topology = m_hub.backend().topology();
    if (m_coreHistory.size() != snapshot.cpu.cores.size()) {
        m_coreHistory.clear();
        for (std::size_t core = 0; core < snapshot.cpu.cores.size(); ++core)
            m_coreHistory.emplace_back();
        rebuildCoreGraphs();
    }
    for (std::size_t core = 0; core < snapshot.cpu.cores.size(); ++core)
        m_coreHistory[core].push(snapshot.cpu.cores[core].usagePct);
    for (qsizetype g = 0; g < m_coreGraphs.size(); ++g) {
        if (m_cpuMode->currentIndex() == 1 && static_cast<std::size_t>(g) < snapshot.cpu.cores.size()) {
            m_coreGraphs[g]->sampled(tr("Processor %1: %2").arg(g).arg(Format::percent(snapshot.cpu.cores[g].usagePct)));
        } else if (m_cpuMode->currentIndex() == 2) {
            m_coreGraphs[g]->sampled(tr("NUMA node %1").arg(g));
        }
    }

    p.grid->set(tr("Usage:"), Format::percent(snapshot.cpu.usagePct));
    p.grid->set(tr("Speed:"), Format::megahertz(snapshot.cpu.freqMHz));
    p.grid->set(tr("Maximum speed:"), Format::megahertz(topology.maxFreqMHz));
    p.grid->set(tr("Cores:"), tr("%1 physical, %2 logical").arg(topology.physical).arg(topology.logical));
    p.grid->set(tr("NUMA nodes:"), QString::number(topology.numaNodes));
    if (topology.hybrid) {
        const auto efficient = std::count(topology.efficiency.begin(), topology.efficiency.end(), true);
        p.grid->set(tr("Core types:"), tr("%1 performance (P), %2 efficiency (E)").arg(topology.logical - efficient).arg(efficient));
    }
}

void PerformanceView::rebuildCoreGraphs()
{
    if (!m_coreGrid)
        return;
    qDeleteAll(m_coreGraphs);
    m_coreGraphs.clear();
    const int mode = m_cpuMode->currentIndex();
    Page* cpu = m_byKey.value(kCpu);
    cpu->graph->setVisible(mode == 0);
    m_coreArea->setVisible(mode != 0);
    if (mode == 0)
        return;

    const CpuTopology& topology = m_hub.backend().topology();
    const int count = mode == 1 ? static_cast<int>(m_coreHistory.size()) : topology.numaNodes;
    const int columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(count))));
    for (int i = 0; i < count; ++i) {
        auto* graph = new Graph(m_coreArea);
        graph->setFixedMaximum(mode == 1 ? 100 : 100.0 * std::max<int>(1, static_cast<int>(std::count(topology.coreNode.begin(), topology.coreNode.end(), i))));
        if (mode == 1) {
            const bool efficient = i < static_cast<int>(topology.efficiency.size()) && topology.efficiency[i];
            graph->addSeries(&m_coreHistory[i], efficient);
            // The letter says what the colour says, so colour is not the only sign.
            graph->setAccessibleName(topology.hybrid ? tr("Processor %1 (%2)").arg(i).arg(efficient ? tr("E") : tr("P")) : tr("Processor %1").arg(i));
        } else {
            for (int core = 0; core < static_cast<int>(m_coreHistory.size()); ++core)
                if (topology.coreNode[core] == i)
                    graph->addSeries(&m_coreHistory[core]);
            graph->setAccessibleName(tr("NUMA node %1").arg(i));
        }
        m_coreGrid->addWidget(graph, i / columns, i % columns);
        m_coreGraphs << graph;
    }
}

void PerformanceView::updateMemory(const Snapshot& snapshot)
{
    Page& p = page(kMemory, tr("Memory"), tr("Memory"));
    const MemorySample& m = snapshot.memory;
    if (!m_memoryBar) {
        m_memoryBar = new QProgressBar(p.widget);
        m_memoryBar->setTextVisible(false);
        m_memoryBar->setRange(0, 1000);
        m_memoryBar->setAccessibleName(tr("Memory in use"));
        static_cast<QVBoxLayout*>(p.widget->layout())->insertWidget(3, m_memoryBar);
    }
    if (m.total.known()) {
        p.graph->setFixedMaximum(static_cast<double>(m.total.value));
        m_memoryBar->setValue(static_cast<int>(1000.0 * valueOr0(m.used) / static_cast<double>(m.total.value)));
    }
    p.subheading->setText(tr("%1 in use of %2").arg(Format::bytes(m.used), Format::bytes(m.total)));
    p.first.push(m.used);
    p.graph->sampled(p.subheading->text());

    p.grid->set(tr("Available:"), Format::bytes(m.available));
    p.grid->set(tr("Cached:"), Format::bytes(m.cached));
    p.grid->set(tr("Free:"), Format::bytes(m.free));
    p.grid->set(tr("Buffers:"), Format::bytes(m.buffers));
    p.grid->set(tr("Shared:"), Format::bytes(m.shared));
    p.grid->set(tr("Swap:"), tr("%1 of %2").arg(Format::bytes(m.swapUsed), Format::bytes(m.swapTotal)));
    p.grid->set(tr("Compressed (zram):"), tr("%1 stored in %2").arg(Format::bytes(m.zramOriginal), Format::bytes(m.zramCompressed)));
    p.grid->set(tr("Compressed (zswap):"), tr("%1 stored in %2").arg(Format::bytes(m.zswapStored), Format::bytes(m.zswapCompressed)));
    const auto pressure = [](const PressureLine& line) { return tr("%1 some, %2 full").arg(Format::percent(line.some10), Format::percent(line.full10)); };
    p.grid->set(tr("Pressure:"), pressure(snapshot.pressure.memory));
    p.grid->set(tr("Out-of-memory kills since start:"), m.oomKills.known() ? QString::number(m.oomKills.value) : Format::why(m.oomKills.state));
}

void PerformanceView::updateDisks(const Snapshot& snapshot)
{
    double totalRead = 0, totalWrite = 0;
    bool anyRate = false;
    for (const DiskSample& disk : snapshot.disks) {
        const QString name = QString::fromStdString(disk.name);
        Page& p = page(QStringLiteral("disk:") + name, tr("Disk %1").arg(name), tr("Disk %1").arg(name));
        if (p.graph->accessibleDescription().isEmpty() && p.first.size() == 0) {
            p.graph->addSeries(&p.second, true);
            p.subheading->setText(QString::fromStdString(disk.model));
            // Reading health needs root, so it asks only when the user presses this.
            auto* health = new QPushButton(tr("Check &Health..."), p.widget);
            static_cast<QVBoxLayout*>(p.widget->layout())->addWidget(health, 0, Qt::AlignRight);
            connect(health, &QPushButton::clicked, this, [this, name, health] {
                if (QStandardPaths::findExecutable(QStringLiteral("smartctl")).isEmpty()) {
                    QMessageBox::information(this, tr("Disk Health"), tr("Reading disk health needs the smartctl tool from the smartmontools package, which is not installed."));
                    return;
                }
                health->setEnabled(false);
                auto* process = new QProcess(this);
                connect(process, &QProcess::finished, this, [this, process, name, health] {
                    health->setEnabled(true);
                    const QString text = QString::fromLocal8Bit(process->readAllStandardOutput()) + QString::fromLocal8Bit(process->readAllStandardError());
                    QMessageBox::information(this, tr("Disk Health: %1").arg(name), text.trimmed().isEmpty() ? tr("No answer from the disk.") : text.trimmed());
                    process->deleteLater();
                });
                process->start(QStringLiteral("pkexec"), {QStringLiteral("smartctl"), QStringLiteral("-H"), QStringLiteral("/dev/") + name});
            });
        }
        p.first.push(disk.readBps);
        p.second.push(disk.writeBps);
        p.graph->sampled(tr("Read %1, write %2").arg(Format::diskRate(disk.readBps), Format::diskRate(disk.writeBps)));

        p.grid->set(tr("Active time:"), Format::percent(disk.activePct));
        p.grid->set(tr("Read (solid line):"), Format::diskRate(disk.readBps));
        p.grid->set(tr("Write (second line):"), Format::diskRate(disk.writeBps));
        p.grid->set(tr("Capacity:"), Format::bytes(disk.sizeBytes));
        if (disk.fsTotal.known())
            p.grid->set(tr("Filesystem free:"), tr("%1 of %2").arg(Format::bytes(disk.fsFree), Format::bytes(disk.fsTotal)));
        else
            p.grid->set(tr("Filesystem free:"), Format::why(disk.fsTotal.state));
        p.grid->set(tr("Type:"), disk.rotational ? tr("Spinning") : tr("Solid state"));
        p.grid->set(tr("Removable:"), disk.removable ? tr("Yes") : tr("No"));
        if (!disk.mounts.empty()) {
            QStringList mounts;
            for (const std::string& mount : disk.mounts)
                mounts << QString::fromStdString(mount);
            p.grid->set(tr("Mounted at:"), mounts.join(QStringLiteral(", ")));
        }
        if (disk.readBps.known() && disk.writeBps.known()) {
            totalRead += disk.readBps.value;
            totalWrite += disk.writeBps.value;
            anyRate = true;
        }
    }

    if (snapshot.disks.size() > 1) {
        Page& all = page(kAllDisks, tr("All disks"), tr("All disks combined"));
        if (all.first.size() == 0)
            all.graph->addSeries(&all.second, true);
        const auto read = anyRate ? Reading<double>::measured(totalRead) : Reading<double>{};
        const auto write = anyRate ? Reading<double>::measured(totalWrite) : Reading<double>{};
        all.first.push(read);
        all.second.push(write);
        all.graph->sampled(tr("Read %1, write %2").arg(Format::diskRate(read), Format::diskRate(write)));
        all.grid->set(tr("Read (solid line):"), Format::diskRate(read));
        all.grid->set(tr("Write (second line):"), Format::diskRate(write));
    }
}

void PerformanceView::updateNetworks(const Snapshot& snapshot)
{
    for (const NetworkSample& net : snapshot.networks) {
        if (net.loopback)
            continue;
        const QString name = QString::fromStdString(net.name);
        Page& p = page(QStringLiteral("net:") + name, tr("Network %1").arg(name), tr("Network %1").arg(name));
        if (p.first.size() == 0) {
            p.graph->addSeries(&p.second, true);
            p.subheading->setText(net.wireless ? tr("Wi-Fi") : tr("Wired or virtual"));
        }
        p.first.push(net.rxBps);
        p.second.push(net.txBps);
        p.graph->sampled(tr("Receive %1, send %2").arg(Format::rate(net.rxBps), Format::rate(net.txBps)));

        p.grid->set(tr("Receive (solid line):"), Format::rate(net.rxBps));
        p.grid->set(tr("Send (second line):"), Format::rate(net.txBps));
        p.grid->set(tr("Received in total:"), Format::bytes(net.rxTotal));
        p.grid->set(tr("Sent in total:"), Format::bytes(net.txTotal));
        p.grid->set(tr("Link speed:"), net.linkMbps.known() ? tr("%1 Mbit/s").arg(net.linkMbps.value) : Format::why(net.linkMbps.state));
        p.grid->set(tr("State:"), QString::fromStdString(net.operState));
        if (net.wireless) {
            p.grid->set(tr("Wi-Fi signal:"), net.wifiSignalDbm.known() ? tr("%1 dBm").arg(net.wifiSignalDbm.value) : Format::why(net.wifiSignalDbm.state));
            p.grid->set(tr("Wi-Fi quality:"), Format::percent(net.wifiQualityPct));
        }
        p.grid->set(tr("Hardware address:"), QString::fromStdString(net.mac));
    }
}

void PerformanceView::updateGpus(const Snapshot& snapshot)
{
    for (const GpuSample& gpu : snapshot.gpus) {
        const QString name = QString::fromStdString(gpu.card);
        Page& p = page(QStringLiteral("gpu:") + name, tr("GPU %1").arg(name), tr("GPU %1").arg(name));
        p.graph->setFixedMaximum(100);
        p.subheading->setText(tr("Driver: %1").arg(QString::fromStdString(gpu.driver)));
        p.first.push(gpu.busyPct);
        p.graph->sampled(tr("GPU usage %1").arg(Format::percent(gpu.busyPct)));

        p.grid->set(tr("Usage:"), Format::percent(gpu.busyPct));
        p.grid->set(tr("Speed:"), Format::megahertz(gpu.freqMHz));
        p.grid->set(tr("Video memory:"), tr("%1 of %2").arg(Format::bytes(gpu.vramUsed), Format::bytes(gpu.vramTotal)));
        p.grid->set(tr("Temperature:"), Format::temperature(gpu.tempC));
        p.grid->set(tr("Power:"), Format::watts(gpu.powerW));
    }
}

void PerformanceView::updateThermals(const Snapshot& snapshot)
{
    Page& p = page(kThermal, tr("Thermals"), tr("Thermals"));
    const ThermalSample& t = snapshot.thermal;
    p.graph->setFixedMaximum(110);
    p.first.push(t.cpuHotspotC);
    p.subheading->setText(tr("CPU hotspot: %1").arg(Format::temperature(t.cpuHotspotC)));
    p.graph->sampled(p.subheading->text());
    p.grid->set(tr("Thermal pressure:"), t.throttleEvents.known() ? tr("%1 throttle events").arg(t.throttleEvents.value) : Format::why(t.throttleEvents.state));
    for (const ThermalSensor& sensor : t.sensors)
        p.grid->set(QStringLiteral("%1 %2:").arg(QString::fromStdString(sensor.chip), QString::fromStdString(sensor.label)), Format::temperature(sensor.tempC));
}

void PerformanceView::updateEnergy(const Snapshot& snapshot)
{
    Page& p = page(kEnergy, tr("Energy"), tr("Energy"));
    const PowerSample& power = snapshot.power;
    p.first.push(power.cpuPackageW);
    p.subheading->setText(power.cpuPackageW.state == Availability::AccessDenied
                              ? tr("Power access restricted. The kernel lets only root read this counter. See the permission guide in the README for how to grant read access.")
                              : tr("CPU package power: %1").arg(Format::watts(power.cpuPackageW)));
    p.graph->sampled(p.subheading->text());

    p.grid->set(tr("Power source:"), power.onAc.known() ? (power.onAc.value ? tr("Mains") : tr("Battery")) : Format::why(power.onAc.state));
    for (const BatterySample& battery : power.batteries)
        p.grid->set(tr("Battery %1:").arg(QString::fromStdString(battery.name)),
                    tr("%1, %2, %3").arg(Format::percent(battery.percent), QString::fromStdString(battery.status), Format::watts(battery.powerW)));
    p.grid->set(tr("Governor:"), Format::text(power.governor));
    p.grid->set(tr("Energy preference:"), Format::text(power.energyPreference));
    p.grid->set(tr("Platform profile:"), Format::text(power.platformProfile));

    if (snapshot.collected & SubProcesses) {
        // Linux has no per-process power counter, so this is a share of the package figure.
        std::vector<const ProcessSample*> busiest;
        for (const ProcessSample& process : snapshot.processes)
            if (process.cpuPct.known() && process.cpuPct.value > 0)
                busiest.push_back(&process);
        const std::size_t count = std::min<std::size_t>(5, busiest.size());
        std::partial_sort(busiest.begin(), busiest.begin() + static_cast<std::ptrdiff_t>(count), busiest.end(),
                          [](const ProcessSample* a, const ProcessSample* b) { return a->cpuPct.value > b->cpuPct.value; });
        QStringList lines;
        const double usage = valueOr0(snapshot.cpu.usagePct);
        for (std::size_t i = 0; i < count; ++i) {
            const ProcessSample& process = *busiest[i];
            if (power.cpuPackageW.known() && usage > 0)
                lines << tr("%1: about %2 (estimated)").arg(QString::fromStdString(process.info->name), Format::watts(power.cpuPackageW.value * process.cpuPct.value / usage));
            else
                lines << tr("%1: %2 of CPU").arg(QString::fromStdString(process.info->name), Format::percent(process.cpuPct.value));
        }
        p.grid->set(tr("Responsible processes:"), lines.join(QLatin1Char('\n')));
    }
}

} // namespace fg
