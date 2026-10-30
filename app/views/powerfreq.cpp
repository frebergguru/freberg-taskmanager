#include "powerfreq.h"

#include "../format.h"
#include "../hub.h"
#include "../widgets/graph.h"
#include "../widgets/infogrid.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace fg {

PowerFreqView::PowerFreqView(SamplerHub& hub)
    : m_hub(hub), m_freqGraph(new Graph), m_powerGraph(new Graph), m_grid(new InfoGrid), m_cores(new QTreeWidget), m_idle(new QTreeWidget)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* graphs = new QHBoxLayout;
    auto* freqColumn = new QVBoxLayout;
    freqColumn->addWidget(new QLabel(tr("Average speed")));
    freqColumn->addWidget(m_freqGraph, 1);
    auto* powerColumn = new QVBoxLayout;
    powerColumn->addWidget(new QLabel(tr("CPU package power")));
    powerColumn->addWidget(m_powerGraph, 1);
    graphs->addLayout(freqColumn);
    graphs->addLayout(powerColumn);
    layout->addLayout(graphs, 1);

    m_freqGraph->addSeries(&m_freqHistory);
    m_freqGraph->setAccessibleName(tr("Average speed"));
    m_powerGraph->addSeries(&m_powerHistory);
    m_powerGraph->setAccessibleName(tr("CPU package power"));

    layout->addWidget(m_grid);

    auto* tables = new QHBoxLayout;
    m_cores->setRootIsDecorated(false);
    m_cores->setHeaderLabels({tr("Processor"), tr("Speed"), tr("Usage")});
    m_cores->setAccessibleName(tr("Speed per processor"));
    m_idle->setRootIsDecorated(false);
    m_idle->setHeaderLabels({tr("Idle state"), tr("Share of idle time")});
    m_idle->setAccessibleName(tr("Idle states"));
    tables->addWidget(m_cores, 1);
    tables->addWidget(m_idle, 1);
    layout->addLayout(tables, 2);
}

void PowerFreqView::refresh()
{
    const PowerDetails details = m_hub.backend().powerDetails();
    m_grid->set(tr("Speed range:"), tr("%1 to %2").arg(Format::megahertz(details.minFreqMHz), Format::megahertz(details.maxFreqMHz)));
    m_grid->set(tr("Boost:"), details.boost.known() ? (details.boost.value == "1" ? tr("On") : tr("Off")) : Format::why(details.boost.state));
    if (details.availableGovernors.known()) {
        QStringList governors;
        for (const std::string& governor : details.availableGovernors.value)
            governors << QString::fromStdString(governor);
        m_grid->set(tr("Available governors:"), governors.join(QStringLiteral(", ")));
    } else {
        m_grid->set(tr("Available governors:"), Format::why(details.availableGovernors.state));
    }

    m_idle->clear();
    std::uint64_t total = 0;
    for (const IdleState& state : details.idleStates)
        total += state.timeUs;
    for (const IdleState& state : details.idleStates)
        new QTreeWidgetItem(m_idle, {QString::fromStdString(state.name), total ? Format::percent(100.0 * static_cast<double>(state.timeUs) / static_cast<double>(total)) : Format::why(Availability::Unsupported)});
    if (details.idleStates.empty())
        new QTreeWidgetItem(m_idle, {Format::why(Availability::Unsupported), QString()});
}

void PowerFreqView::onSnapshot(const Snapshot& snapshot)
{
    m_freqHistory.push(snapshot.cpu.freqMHz);
    m_freqGraph->sampled(tr("Average speed %1").arg(Format::megahertz(snapshot.cpu.freqMHz)));
    m_powerHistory.push(snapshot.power.cpuPackageW);
    m_powerGraph->sampled(tr("CPU package power %1").arg(Format::watts(snapshot.power.cpuPackageW)));

    m_grid->set(tr("Governor:"), Format::text(snapshot.power.governor));
    m_grid->set(tr("Energy preference:"), Format::text(snapshot.power.energyPreference));
    m_grid->set(tr("Platform profile:"), Format::text(snapshot.power.platformProfile));

    const int cores = static_cast<int>(snapshot.cpu.cores.size());
    while (m_cores->topLevelItemCount() > cores)
        delete m_cores->takeTopLevelItem(m_cores->topLevelItemCount() - 1);
    for (int core = 0; core < cores; ++core) {
        QTreeWidgetItem* item = m_cores->topLevelItem(core);
        if (!item) {
            item = new QTreeWidgetItem(m_cores);
            item->setText(0, tr("Processor %1").arg(core));
        }
        item->setText(1, Format::megahertz(snapshot.cpu.cores[core].freqMHz));
        item->setText(2, Format::percent(snapshot.cpu.cores[core].usagePct));
    }
}

} // namespace fg
