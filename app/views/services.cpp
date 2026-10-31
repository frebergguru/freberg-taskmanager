#include "services.h"

#include "../format.h"
#include "../widgets/confirm.h"
#include "../system/dbus.h"

#include <QDir>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace fg {

ServicesView::ServicesView()
    : m_list(new QTreeWidget), m_filter(new QLineEdit), m_journal(new QPlainTextEdit), m_message(new QLabel),
      m_start(new QPushButton(tr("&Start"))), m_stop(new QPushButton(tr("St&op"))), m_restart(new QPushButton(tr("&Restart"))),
      m_enable(new QPushButton(tr("&Enable"))), m_disable(new QPushButton(tr("&Disable")))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* top = new QHBoxLayout;
    auto* label = new QLabel(tr("&Filter:"));
    label->setBuddy(m_filter);
    m_filter->setAccessibleName(tr("Filter services"));
    top->addWidget(label);
    top->addWidget(m_filter, 1);
    layout->addLayout(top);

    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setSortingEnabled(true);
    m_list->setHeaderLabels({tr("Name"), tr("State"), tr("Detail"), tr("At startup"), tr("Description")});
    m_list->setAccessibleName(tr("Services"));
    m_list->header()->setStretchLastSection(true);
    m_list->setColumnWidth(0, fontMetrics().averageCharWidth() * 36);
    m_list->sortByColumn(0, Qt::AscendingOrder);

    m_journal->setReadOnly(true);
    m_journal->setAccessibleName(tr("Recent log lines"));
    m_journal->setPlaceholderText(tr("Recent log lines appear here when a service is selected."));

    auto* split = new QSplitter(Qt::Vertical);
    split->addWidget(m_list);
    split->addWidget(m_journal);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    layout->addWidget(split, 1);

    auto* bottom = new QHBoxLayout;
    m_message->setWordWrap(true);
    bottom->addWidget(m_message, 1);
    for (QPushButton* button : {m_start, m_stop, m_restart, m_enable, m_disable})
        bottom->addWidget(button);
    layout->addLayout(bottom);

    connect(m_filter, &QLineEdit::textChanged, this, &ServicesView::applyFilter);
    connect(m_list, &QTreeWidget::currentItemChanged, this, &ServicesView::showJournal);
    connect(m_start, &QPushButton::clicked, this, [this] { act("StartUnit", tr("Start")); });
    connect(m_stop, &QPushButton::clicked, this, [this] { act("StopUnit", tr("Stop")); });
    connect(m_restart, &QPushButton::clicked, this, [this] { act("RestartUnit", tr("Restart")); });
    connect(m_enable, &QPushButton::clicked, this, [this] { setEnabledState(true); });
    connect(m_disable, &QPushButton::clicked, this, [this] { setEnabledState(false); });
}

void ServicesView::closed()
{
    // The list can be long, and it is read again on opening.
    m_list->clear();
    m_journal->clear();
}

void ServicesView::refresh()
{
    const QString previous = selectedUnit();
    m_list->clear();
    m_managed = systemdAvailable();
    for (QPushButton* button : {m_start, m_stop, m_restart, m_enable, m_disable})
        button->setEnabled(m_managed);
    if (!m_managed) {
        listWithoutSystemd();
        return;
    }

    QString error;
    const QList<DBusUnit> units = listUnits(false, &error);
    QHash<QString, QString> enabledState;
    QString fileError;
    for (const DBusUnitFile& file : listUnitFiles(false, &fileError))
        enabledState.insert(QFileInfo(file.path).fileName(), file.state);
    if (!error.isEmpty()) {
        m_message->setText(tr("Could not list services: %1").arg(error));
        return;
    }
    QSet<QString> listed;
    for (const DBusUnit& unit : units) {
        if (!unit.name.endsWith(QLatin1String(".service")))
            continue;
        auto* item = new QTreeWidgetItem(m_list, {unit.name, unit.activeState, unit.subState, enabledState.value(unit.name), unit.description});
        listed.insert(unit.name);
        if (unit.name == previous)
            m_list->setCurrentItem(item);
    }
    // Services that are not running are only known from their files. An alias says nothing
    // about itself: the unit it points to is listed under its real name.
    for (auto it = enabledState.constBegin(); it != enabledState.constEnd(); ++it)
        if (it.key().endsWith(QLatin1String(".service")) && !it.key().contains(QLatin1Char('@')) && !listed.contains(it.key()) && it.value() != QLatin1String("alias"))
            new QTreeWidgetItem(m_list, {it.key(), tr("inactive"), tr("dead"), it.value(), QString()});
    applyFilter();
}

void ServicesView::listWithoutSystemd()
{
    // OpenRC and runit: list what is installed. State and control are not offered.
    const QStringList places = {QStringLiteral("/etc/init.d"), QStringLiteral("/etc/runit/sv"), QStringLiteral("/etc/sv")};
    for (const QString& place : places)
        for (const QString& name : QDir(place).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot))
            new QTreeWidgetItem(m_list, {name, Format::why(Availability::Unsupported), QString(), QString(), place});
    m_message->setText(tr("This system does not use systemd. Services are listed, but can't be controlled from here."));
    applyFilter();
}

void ServicesView::applyFilter()
{
    const QString filter = m_filter->text();
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_list->topLevelItem(i);
        item->setHidden(!filter.isEmpty() && !item->text(0).contains(filter, Qt::CaseInsensitive) && !item->text(4).contains(filter, Qt::CaseInsensitive));
    }
}

QString ServicesView::selectedUnit() const
{
    const QTreeWidgetItem* item = m_list->currentItem();
    return item ? item->text(0) : QString();
}

void ServicesView::act(const char* method, const QString& label)
{
    const QString unit = selectedUnit();
    if (unit.isEmpty()) {
        m_message->setText(tr("Select a service first."));
        return;
    }
    if (!confirmed(this, label, tr("%1 the service %2?").arg(label, unit)))
        return;
    m_message->setText(tr("%1 %2: waiting for the system...").arg(label, unit));
    manageUnit(false, method, unit, this, [this, label, unit](const QString& error) {
        m_message->setText(error.isEmpty() ? tr("%1 %2: done.").arg(label, unit) : tr("%1 %2 failed: %3").arg(label, unit, error));
        refresh();
    });
}

void ServicesView::setEnabledState(bool enabled)
{
    const QString unit = selectedUnit();
    if (unit.isEmpty()) {
        m_message->setText(tr("Select a service first."));
        return;
    }
    const QString label = enabled ? tr("Enable") : tr("Disable");
    if (!confirmed(this, label, tr("%1 the service %2 at startup?").arg(label, unit)))
        return;
    m_message->setText(tr("%1 %2: waiting for the system...").arg(label, unit));
    setUnitEnabled(false, unit, enabled, this, [this, label, unit](const QString& error) {
        m_message->setText(error.isEmpty() ? tr("%1 %2: done.").arg(label, unit) : tr("%1 %2 failed: %3").arg(label, unit, error));
        refresh();
    });
}

void ServicesView::showJournal()
{
    m_journal->clear();
    const QString unit = selectedUnit();
    if (unit.isEmpty() || !m_managed)
        return;
    auto* process = new QProcess(this);
    connect(process, &QProcess::errorOccurred, this, [process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            process->deleteLater();
    });
    connect(process, &QProcess::finished, this, [this, process, unit] {
        if (selectedUnit() == unit)
            m_journal->setPlainText(QString::fromLocal8Bit(process->readAllStandardOutput()));
        process->deleteLater();
    });
    process->start(QStringLiteral("journalctl"), {QStringLiteral("--no-pager"), QStringLiteral("-n"), QStringLiteral("30"), QStringLiteral("-u"), unit});
}

} // namespace fg
