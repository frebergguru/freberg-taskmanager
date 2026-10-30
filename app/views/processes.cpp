#include "processes.h"

#include "../hub.h"
#include "../settings.h"
#include "inspector.h"
#include "processmodel.h"

#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTreeView>
#include <QVBoxLayout>

namespace fg {

ProcessesView::ProcessesView(SamplerHub& hub)
    : m_hub(hub), m_model(new ProcessModel(this)), m_tree(new QTreeView(this)), m_filter(new QLineEdit(this)),
      m_treeMode(new QCheckBox(tr("Show as &tree"), this)), m_endTask(new QPushButton(tr("&End Task"), this)), m_export(new QPushButton(tr("E&xport..."), this)), m_message(new QLabel(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* top = new QHBoxLayout;
    auto* filterLabel = new QLabel(tr("&Filter:"), this);
    filterLabel->setBuddy(m_filter);
    m_filter->setPlaceholderText(tr("Name, command, user or PID"));
    m_filter->setClearButtonEnabled(false);
    m_filter->setAccessibleName(tr("Filter processes"));
    m_treeMode->setChecked(m_model->isTree());
    top->addWidget(filterLabel);
    top->addWidget(m_filter, 1);
    top->addWidget(m_treeMode);
    layout->addLayout(top);

    m_tree->setModel(m_model);
    m_tree->setUniformRowHeights(true);
    m_tree->setSortingEnabled(true);
    m_tree->setAllColumnsShowFocus(true);
    m_tree->setRootIsDecorated(m_model->isTree());
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setAccessibleName(tr("Processes"));
    m_tree->setAnimated(false);
    QHeaderView* header = m_tree->header();
    header->setSectionsMovable(true);
    header->setStretchLastSection(true);
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    const QByteArray saved = Settings::instance().value("processes/header").toByteArray();
    if (saved.isEmpty()) {
        const int unit = fontMetrics().averageCharWidth();
        m_tree->setColumnWidth(ProcessModel::Name, unit * 32);
        m_tree->setColumnWidth(ProcessModel::Pid, unit * 8);
        m_tree->setColumnWidth(ProcessModel::User, unit * 12);
        m_tree->setColumnWidth(ProcessModel::Status, unit * 12);
        m_tree->setColumnWidth(ProcessModel::Cpu, unit * 8);
        m_tree->setColumnWidth(ProcessModel::Memory, unit * 11);
        m_tree->setColumnWidth(ProcessModel::Disk, unit * 15);
        m_tree->setColumnWidth(ProcessModel::Energy, unit * 19);
        m_tree->setColumnHidden(ProcessModel::Network, true);
        m_tree->setColumnHidden(ProcessModel::Gpu, true);
        m_tree->setColumnHidden(ProcessModel::Group, true);
        m_tree->setColumnHidden(ProcessModel::Threads, true);
        m_tree->setColumnHidden(ProcessModel::Nice, true);
    } else {
        header->restoreState(saved);
    }
    const auto sortColumn = Settings::instance().value("processes/sortColumn", ProcessModel::Cpu).toInt();
    const auto sortOrder = static_cast<Qt::SortOrder>(Settings::instance().value("processes/sortOrder", Qt::DescendingOrder).toInt());
    header->setSortIndicator(sortColumn, sortOrder);
    layout->addWidget(m_tree, 1);

    auto* bottom = new QHBoxLayout;
    m_message->setWordWrap(true);
    bottom->addWidget(m_message, 1);
    bottom->addWidget(m_export);
    bottom->addWidget(m_endTask);
    layout->addLayout(bottom);

    applyHighlightSetting();
    connect(&Settings::instance(), &Settings::changed, this, [this](const QString& key) {
        if (key == QLatin1String("app/rowHighlight"))
            applyHighlightSetting();
    });
    connect(m_filter, &QLineEdit::textChanged, m_model, &ProcessModel::setFilter);
    connect(m_treeMode, &QCheckBox::toggled, this, [this](bool on) {
        m_model->setTree(on);
        m_tree->setRootIsDecorated(on);
    });
    connect(m_endTask, &QPushButton::clicked, this, [this] { endSelected(false, false); });
    connect(m_export, &QPushButton::clicked, this, &ProcessesView::exportList);
    connect(m_tree, &QTreeView::customContextMenuRequested, this, &ProcessesView::showContextMenu);
    connect(m_tree, &QTreeView::activated, this, &ProcessesView::showProperties);
    connect(header, &QHeaderView::customContextMenuRequested, this, &ProcessesView::showHeaderMenu);
}

Subsystems ProcessesView::wants() const
{
    Subsystems wanted = SubProcesses | SubCpu | SubPower;
    // These two cost far more than the rest, so they run only while they can be seen.
    if (!m_tree->isColumnHidden(ProcessModel::Gpu))
        wanted |= SubProcessGpu;
    if (!m_tree->isColumnHidden(ProcessModel::Network))
        wanted |= SubProcessNetwork;
    return wanted;
}

void ProcessesView::applyHighlightSetting()
{
    m_tree->setSelectionBehavior(Settings::instance().rowHighlight() ? QAbstractItemView::SelectRows : QAbstractItemView::SelectItems);
}

void ProcessesView::onSnapshot(const Snapshot& snapshot)
{
    m_model->update(snapshot);
    for (const QModelIndex& parent : m_model->newParents())
        m_tree->expand(parent);
}

void ProcessesView::closed()
{
    Settings::instance().setValue("processes/header", m_tree->header()->saveState());
}

const ProcessSample* ProcessesView::currentProcess() const
{
    return m_model->sampleAt(m_tree->currentIndex());
}

void ProcessesView::goToProcess(const ProcessId& id)
{
    const QModelIndex found = m_model->indexOf(id);
    if (found.isValid()) {
        m_tree->setCurrentIndex(found);
        m_tree->scrollTo(found);
        m_tree->setFocus();
        m_message->clear();
    } else if (m_model->existsUnfiltered(id)) {
        m_message->setText(tr("That process is hidden by the current filter."));
    } else if (m_model->rowCount() == 0) {
        // The first sample has not arrived; it will be shown once it does.
        m_message->setText(tr("Looking for process %1...").arg(id.pid));
    } else {
        m_message->setText(tr("Process %1 has ended.").arg(id.pid));
    }
}

void ProcessesView::report(const QString& action, const ActionResult& result)
{
    switch (result.code) {
    case ActionResult::Ok:
        m_message->setText(tr("%1: done.").arg(action));
        break;
    case ActionResult::ProcessGone:
        m_message->setText(tr("%1: the process has already ended.").arg(action));
        break;
    case ActionResult::AccessDenied:
        m_message->setText(tr("%1: access denied. This process belongs to another user.").arg(action));
        break;
    default:
        m_message->setText(tr("%1: failed (%2).").arg(action, QString::fromStdString(result.detail)));
        break;
    }
}

void ProcessesView::endSelected(bool force, bool wholeTree)
{
    const ProcessSample* process = currentProcess();
    if (!process) {
        m_message->setText(tr("Select a process first."));
        return;
    }
    const ProcessId id = process->info->id;
    const QString name = QString::fromStdString(process->info->name);
    const QString question = wholeTree ? tr("End %1 (PID %2) and everything it started?\n\nUnsaved work in those programs will be lost.").arg(name).arg(id.pid)
        : force                        ? tr("Force %1 (PID %2) to end now?\n\nIt gets no chance to save its work.").arg(name).arg(id.pid)
                                       : tr("Ask %1 (PID %2) to end?").arg(name).arg(id.pid);
    if (QMessageBox::warning(this, tr("End Task"), question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    const ProcessSignal signal = force ? ProcessSignal::Kill : ProcessSignal::End;
    if (!wholeTree) {
        report(force ? tr("Force end") : tr("End task"), m_hub.backend().signalProcess(id, signal));
        return;
    }
    // Children first, so a parent cannot restart them while we work.
    const std::vector<ProcessId> family = m_model->withDescendants(id);
    ActionResult last;
    for (auto it = family.rbegin(); it != family.rend(); ++it) {
        const ActionResult result = m_hub.backend().signalProcess(*it, signal);
        if (!result.ok() && result.code != ActionResult::ProcessGone)
            last = result;
    }
    report(tr("End tree"), last);
}

void ProcessesView::changePriority(int nice)
{
    if (const ProcessSample* process = currentProcess())
        report(tr("Set priority"), m_hub.backend().setPriority(process->info->id, nice));
}

void ProcessesView::changeIoPriority(int choice)
{
    const ProcessSample* process = currentProcess();
    if (!process)
        return;
    const IoPriority priority = choice == 0 ? IoPriority::Idle : IoPriority::BestEffort;
    report(tr("Set disk priority"), m_hub.backend().setIoPriority(process->info->id, priority, choice == 1 ? 4 : (choice == 2 ? 0 : 0)));
}

void ProcessesView::changeAffinity()
{
    const ProcessSample* process = currentProcess();
    if (!process)
        return;
    const ProcessId id = process->info->id;
    std::vector<int> current;
    if (const ActionResult result = m_hub.backend().currentAffinity(id, current); !result.ok()) {
        report(tr("Read processor affinity"), result);
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Processor Affinity"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Processors %1 may run on:").arg(QString::fromStdString(process->info->name)), &dialog));
    auto* list = new QListWidget(&dialog);
    list->setAccessibleName(tr("Processors"));
    const int logical = m_hub.backend().topology().logical;
    for (int cpu = 0; cpu < logical; ++cpu) {
        auto* item = new QListWidgetItem(tr("Processor %1").arg(cpu), list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(std::find(current.begin(), current.end(), cpu) != current.end() ? Qt::Checked : Qt::Unchecked);
    }
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;

    std::vector<int> chosen;
    for (int cpu = 0; cpu < logical; ++cpu)
        if (list->item(cpu)->checkState() == Qt::Checked)
            chosen.push_back(cpu);
    if (chosen.empty()) {
        m_message->setText(tr("A process needs at least one processor. Nothing was changed."));
        return;
    }
    report(tr("Set processor affinity"), m_hub.backend().setAffinity(id, chosen));
}

void ProcessesView::showProperties()
{
    const ProcessSample* process = currentProcess();
    if (!process)
        return;
    const ProcessId id = process->info->id;
    ProcessDetails details;
    if (const ActionResult result = m_hub.backend().processDetails(id, details); !result.ok()) {
        report(tr("Read process details"), result);
        return;
    }
    InspectorDialog dialog(*process->info, details, this);
    dialog.exec();
}

void ProcessesView::showContextMenu(const QPoint& position)
{
    const ProcessSample* process = currentProcess();
    if (!process)
        return;
    const ProcessId id = process->info->id;

    QMenu menu(this);
    menu.addAction(tr("&End Task"), this, [this] { endSelected(false, false); });
    menu.addAction(tr("&Force End"), this, [this] { endSelected(true, false); });
    menu.addAction(tr("End Process &Tree"), this, [this] { endSelected(false, true); });
    menu.addSeparator();
    menu.addAction(tr("&Suspend"), this, [this, id] { report(tr("Suspend"), m_hub.backend().signalProcess(id, ProcessSignal::Suspend)); });
    menu.addAction(tr("&Resume"), this, [this, id] { report(tr("Resume"), m_hub.backend().signalProcess(id, ProcessSignal::Resume)); });
    menu.addSeparator();

    QMenu* priority = menu.addMenu(tr("Set &Priority"));
    const struct {
        const char* label;
        int nice;
    } levels[] = {{QT_TR_NOOP("High (-10)"), -10}, {QT_TR_NOOP("Above Normal (-5)"), -5}, {QT_TR_NOOP("Normal (0)"), 0}, {QT_TR_NOOP("Below Normal (5)"), 5}, {QT_TR_NOOP("Low (19)"), 19}};
    for (const auto& level : levels) {
        QAction* action = priority->addAction(tr(level.label), this, [this, nice = level.nice] { changePriority(nice); });
        action->setCheckable(true);
        action->setChecked(process->nice == level.nice);
    }
    QMenu* disk = menu.addMenu(tr("Set &Disk Priority"));
    disk->addAction(tr("Idle"), this, [this] { changeIoPriority(0); });
    disk->addAction(tr("Normal"), this, [this] { changeIoPriority(1); });
    disk->addAction(tr("High"), this, [this] { changeIoPriority(2); });
    menu.addAction(tr("Set Processor &Affinity..."), this, &ProcessesView::changeAffinity);
    menu.addSeparator();
    QAction* properties = menu.addAction(tr("P&roperties..."), this, &ProcessesView::showProperties);
    menu.setDefaultAction(properties);
    menu.exec(m_tree->viewport()->mapToGlobal(position));
}

void ProcessesView::exportList()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Export Process List"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), tr("Spreadsheet (*.csv)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QLatin1String(".csv")))
        path += QStringLiteral(".csv");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_message->setText(tr("Could not export: %1").arg(file.errorString()));
        return;
    }
    QStringList header;
    for (int column = 0; column < ProcessModel::ColumnCount; ++column)
        header << m_model->headerData(column, Qt::Horizontal).toString();
    file.write(header.join(QLatin1Char(',')).toUtf8() + '\n');
    const std::function<void(const QModelIndex&)> writeRows = [&](const QModelIndex& parent) {
        for (int row = 0; row < m_model->rowCount(parent); ++row) {
            const QModelIndex first = m_model->index(row, 0, parent);
            QStringList cells;
            for (int column = 0; column < ProcessModel::ColumnCount; ++column) {
                QString cell = m_model->index(row, column, parent).data().toString();
                cell.replace(QLatin1Char('"'), QStringLiteral("\"\""));
                cells << QLatin1Char('"') + cell + QLatin1Char('"');
            }
            file.write(cells.join(QLatin1Char(',')).toUtf8() + '\n');
            writeRows(first);
        }
    };
    writeRows({});
    m_message->setText(tr("Exported to %1").arg(path));
}

void ProcessesView::showHeaderMenu(const QPoint& position)
{
    QMenu menu(this);
    for (int column = 0; column < ProcessModel::ColumnCount; ++column) {
        QAction* action = menu.addAction(m_model->headerData(column, Qt::Horizontal).toString());
        action->setCheckable(true);
        action->setChecked(!m_tree->isColumnHidden(column));
        action->setEnabled(column != ProcessModel::Name);
        connect(action, &QAction::toggled, this, [this, column](bool on) {
            m_tree->setColumnHidden(column, !on);
            emit wantsChanged();
        });
    }
    menu.exec(m_tree->header()->mapToGlobal(position));
}

} // namespace fg
