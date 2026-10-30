#include "drivers.h"

#include "../format.h"
#include "../hub.h"
#include "../widgets/sortitem.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace fg {

DriversView::DriversView(SamplerHub& hub) : m_hub(hub), m_list(new QTreeWidget), m_filter(new QLineEdit), m_details(new QPlainTextEdit)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* top = new QHBoxLayout;
    auto* label = new QLabel(tr("&Filter:"));
    label->setBuddy(m_filter);
    m_filter->setAccessibleName(tr("Filter drivers"));
    top->addWidget(label);
    top->addWidget(m_filter, 1);
    layout->addLayout(top);

    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setSortingEnabled(true);
    m_list->setHeaderLabels({tr("Module"), tr("Size"), tr("Uses"), tr("Used by"), tr("Devices")});
    m_list->setAccessibleName(tr("Kernel modules"));
    m_list->setColumnWidth(0, fontMetrics().averageCharWidth() * 24);
    m_list->sortByColumn(0, Qt::AscendingOrder);

    m_details->setReadOnly(true);
    m_details->setAccessibleName(tr("Module details"));
    m_details->setPlaceholderText(tr("Details appear here when a module is selected."));

    auto* split = new QSplitter(Qt::Vertical);
    split->addWidget(m_list);
    split->addWidget(m_details);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    layout->addWidget(split, 1);

    connect(m_filter, &QLineEdit::textChanged, this, &DriversView::applyFilter);
    connect(m_list, &QTreeWidget::currentItemChanged, this, &DriversView::showDetails);
}

void DriversView::closed()
{
    m_list->clear();
    m_details->clear();
}

void DriversView::refresh()
{
    m_list->setSortingEnabled(false);
    m_list->clear();
    for (const KernelModule& module : m_hub.backend().kernelModules()) {
        QStringList devices;
        for (const std::string& device : module.devices)
            devices << QString::fromStdString(device);
        auto* item = new SortItem(m_list, {QString::fromStdString(module.name), Format::bytes(module.sizeBytes), QString::number(module.useCount), QString::fromStdString(module.usedBy), devices.join(QStringLiteral(", "))});
        item->setSortValue(1, static_cast<double>(module.sizeBytes));
        item->setSortValue(2, module.useCount);
    }
    m_list->setSortingEnabled(true);
    applyFilter();
}

void DriversView::applyFilter()
{
    const QString filter = m_filter->text();
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_list->topLevelItem(i);
        item->setHidden(!filter.isEmpty() && !item->text(0).contains(filter, Qt::CaseInsensitive) && !item->text(4).contains(filter, Qt::CaseInsensitive));
    }
}

void DriversView::showDetails()
{
    m_details->clear();
    const QTreeWidgetItem* item = m_list->currentItem();
    if (!item)
        return;
    const QString name = item->text(0);
    auto* process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process, name] {
        const QTreeWidgetItem* current = m_list->currentItem();
        if (current && current->text(0) == name)
            m_details->setPlainText(QString::fromLocal8Bit(process->readAllStandardOutput()));
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_details->setPlainText(tr("The modinfo tool is not installed."));
            process->deleteLater();
        }
    });
    process->start(QStringLiteral("modinfo"), {name});
}

} // namespace fg
