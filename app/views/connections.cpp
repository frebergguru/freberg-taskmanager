#include "connections.h"

#include "../format.h"
#include "../hub.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace fg {

namespace {
constexpr int kPidRole = Qt::UserRole;
constexpr int kStartRole = Qt::UserRole + 1;
constexpr int kAutoMs = 2000;
}

ConnectionsView::ConnectionsView(SamplerHub& hub)
    : m_hub(hub), m_list(new QTreeWidget), m_filter(new QLineEdit), m_auto(new QCheckBox(tr("&Update automatically"))),
      m_showUnix(new QCheckBox(tr("Show &local (Unix) sockets"))), m_message(new QLabel), m_timer(new QTimer(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* top = new QHBoxLayout;
    auto* label = new QLabel(tr("&Filter:"));
    label->setBuddy(m_filter);
    m_filter->setAccessibleName(tr("Filter connections"));
    top->addWidget(label);
    top->addWidget(m_filter, 1);
    top->addWidget(m_auto);
    top->addWidget(m_showUnix);
    layout->addLayout(top);

    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setSortingEnabled(true);
    m_list->setHeaderLabels({tr("Protocol"), tr("Local address"), tr("Remote address"), tr("State"), tr("Process"), tr("PID"), tr("Sent"), tr("Received")});
    m_list->setAccessibleName(tr("Network connections"));
    const int unit = fontMetrics().averageCharWidth();
    m_list->setColumnWidth(0, unit * 8);
    m_list->setColumnWidth(1, unit * 26);
    m_list->setColumnWidth(2, unit * 26);
    m_list->setColumnWidth(3, unit * 14);
    layout->addWidget(m_list, 1);

    auto* bottom = new QHBoxLayout;
    m_message->setWordWrap(true);
    bottom->addWidget(m_message, 1);
    auto* end = new QPushButton(tr("&End Process"));
    bottom->addWidget(end);
    layout->addLayout(bottom);

    m_timer->setInterval(kAutoMs);
    m_timer->setTimerType(Qt::VeryCoarseTimer);
    connect(m_timer, &QTimer::timeout, this, &ConnectionsView::refresh);
    connect(m_auto, &QCheckBox::toggled, this, [this](bool on) {
        if (on)
            m_timer->start();
        else
            m_timer->stop();
    });
    connect(m_showUnix, &QCheckBox::toggled, this, &ConnectionsView::refresh);
    connect(m_filter, &QLineEdit::textChanged, this, &ConnectionsView::applyFilter);
    connect(end, &QPushButton::clicked, this, &ConnectionsView::endOwner);
}

void ConnectionsView::opened()
{
    refresh();
    if (m_auto->isChecked())
        m_timer->start();
}

void ConnectionsView::closed()
{
    m_timer->stop();
    m_list->clear();
}

void ConnectionsView::refresh()
{
    const int selectedPid = m_list->currentItem() ? m_list->currentItem()->data(0, kPidRole).toInt() : 0;
    const QString selectedLocal = m_list->currentItem() ? m_list->currentItem()->text(1) : QString();
    const bool showUnix = m_showUnix->isChecked();

    m_list->setSortingEnabled(false);
    m_list->clear();
    int hidden = 0;
    for (const NetworkConnection& c : m_hub.backend().connections()) {
        if (c.protocol == "unix" && !showUnix)
            continue;
        const bool knownOwner = c.owner.pid != 0;
        if (!knownOwner)
            ++hidden;
        auto* item = new QTreeWidgetItem(m_list, {QString::fromStdString(c.protocol), QString::fromStdString(c.local), QString::fromStdString(c.remote), QString::fromStdString(c.state),
                                                  knownOwner ? QString::fromStdString(c.ownerName) : tr("Hidden (other user)"), knownOwner ? QString::number(c.owner.pid) : QString(),
                                                  Format::bytes(c.sent), Format::bytes(c.received)});
        item->setData(0, kPidRole, c.owner.pid);
        item->setData(0, kStartRole, QVariant::fromValue<qulonglong>(c.owner.startTicks));
        if (selectedPid != 0 && c.owner.pid == selectedPid && item->text(1) == selectedLocal)
            m_list->setCurrentItem(item);
    }
    m_list->setSortingEnabled(true);
    applyFilter();
    m_message->setText(hidden > 0 ? tr("%n connection(s) belong to processes we may not look into.", nullptr, hidden) : QString());
}

void ConnectionsView::applyFilter()
{
    const QString filter = m_filter->text();
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_list->topLevelItem(i);
        bool show = filter.isEmpty();
        for (int column = 0; !show && column < 6; ++column)
            show = item->text(column).contains(filter, Qt::CaseInsensitive);
        item->setHidden(!show);
    }
}

void ConnectionsView::endOwner()
{
    const QTreeWidgetItem* item = m_list->currentItem();
    if (!item || item->data(0, kPidRole).toInt() == 0) {
        m_message->setText(tr("Select a connection with a known process first."));
        return;
    }
    const ProcessId id{item->data(0, kPidRole).toInt(), item->data(0, kStartRole).toULongLong()};
    if (QMessageBox::warning(this, tr("End Process"), tr("Ask %1 (PID %2) to end?").arg(item->text(4)).arg(id.pid), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    const ActionResult result = m_hub.backend().signalProcess(id, ProcessSignal::End);
    m_message->setText(result.ok() ? tr("End process: done.") : (result.code == ActionResult::ProcessGone ? tr("The process has already ended.") : tr("End process failed: %1").arg(QString::fromStdString(result.detail))));
    refresh();
}

} // namespace fg
