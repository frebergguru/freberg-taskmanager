#include "sysinfo.h"

#include "../format.h"
#include "../hub.h"
#include "../widgets/infogrid.h"

#include <QGuiApplication>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QtVersion>

namespace fg {

SystemInfoView::SystemInfoView(SamplerHub& hub) : m_hub(hub), m_grid(new InfoGrid)
{
    auto* area = new QScrollArea(this);
    area->setWidget(m_grid);
    area->setWidgetResizable(true);
    area->setAccessibleName(tr("System information"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(area);
}

void SystemInfoView::refresh()
{
    const SystemFacts facts = m_hub.backend().systemFacts();
    const CpuTopology& cpu = m_hub.backend().topology();
    const auto text = [](const std::string& value) { return value.empty() ? Format::why(Availability::Unsupported) : QString::fromStdString(value); };

    m_grid->set(tr("Computer name:"), text(facts.hostName));
    m_grid->set(tr("Operating system:"), text(facts.osName));
    m_grid->set(tr("Kernel:"), text(facts.kernel));
    m_grid->set(tr("Architecture:"), text(facts.architecture));
    m_grid->set(tr("Display system:"), QGuiApplication::platformName());
    m_grid->set(tr("Toolkit:"), tr("Qt %1").arg(QString::fromLatin1(qVersion())));
    m_grid->set(tr("Running as:"), text(facts.runningAs));

    if (facts.uptimeSeconds.known()) {
        const int minutes = static_cast<int>(facts.uptimeSeconds.value / 60);
        m_grid->set(tr("Up for:"), tr("%1, %2, %3").arg(tr("%n day(s)", nullptr, minutes / 1440), tr("%n hour(s)", nullptr, (minutes / 60) % 24), tr("%n minute(s)", nullptr, minutes % 60)));
    } else {
        m_grid->set(tr("Up for:"), Format::why(facts.uptimeSeconds.state));
    }

    m_grid->set(tr("Manufacturer:"), text(facts.boardVendor));
    m_grid->set(tr("Product:"), text(facts.productName));
    m_grid->set(tr("Motherboard:"), text(facts.boardName));
    m_grid->set(tr("BIOS version:"), text(facts.biosVersion));

    m_grid->set(tr("Processor:"), text(cpu.model));
    m_grid->set(tr("Cores:"), cpu.physical > 0 ? tr("%1 physical, %2 logical").arg(cpu.physical).arg(cpu.logical) : tr("%1 logical").arg(cpu.logical));
    m_grid->set(tr("NUMA nodes:"), QString::number(cpu.numaNodes));

    QStringList gpus;
    for (const std::string& gpu : facts.gpus)
        gpus << QString::fromStdString(gpu);
    m_grid->set(tr("Graphics:"), gpus.isEmpty() ? Format::why(Availability::Unsupported) : gpus.join(QLatin1Char('\n')));

    if (facts.memoryModules.known()) {
        QStringList modules;
        int used = 0;
        for (const MemoryModule& module : facts.memoryModules.value) {
            if (!module.sizeKnown) {
                ++used; // something is there, even if the firmware won't say how big
                modules << tr("%1: size not reported").arg(QString::fromStdString(module.locator));
                continue;
            }
            if (module.sizeBytes == 0) {
                modules << tr("%1: empty").arg(QString::fromStdString(module.locator));
                continue;
            }
            ++used;
            const QString description = tr("%1: %2 %3 %4").arg(QString::fromStdString(module.locator), Format::bytes(module.sizeBytes), QString::fromStdString(module.type), QString::fromStdString(module.formFactor));
            // A speed of 0 means the firmware doesn't say, which virtual machines often do.
            modules << (module.speedMTs > 0 ? tr("%1, %2 MT/s").arg(description).arg(module.speedMTs) : description);
        }
        m_grid->set(tr("Memory slots used:"), tr("%1 of %2").arg(used).arg(facts.memoryModules.value.size()));
        m_grid->set(tr("Memory modules:"), modules.join(QLatin1Char('\n')));
    } else {
        const QString why = facts.memoryModules.state == Availability::AccessDenied
            ? tr("Access denied. The kernel lets only root read this table. See the permission guide in the README.")
            : Format::why(facts.memoryModules.state);
        m_grid->set(tr("Memory slots used:"), why);
        m_grid->set(tr("Memory modules:"), why);
    }
}

} // namespace fg
