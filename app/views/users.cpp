#include "users.h"

#include "../format.h"
#include "../system/dbus.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <pwd.h>

namespace fg {

namespace {
constexpr int kPathRole = Qt::UserRole;

struct Totals {
    double cpu = 0;
    std::uint64_t memory = 0;
    double disk = 0;
    int count = 0;
    bool cpuKnown = false;
    bool diskKnown = false;
};
} // namespace

UsersView::UsersView() : m_users(new QTreeWidget), m_sessions(new QTreeWidget), m_message(new QLabel)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* usersLabel = new QLabel(tr("&Users:"));
    usersLabel->setBuddy(m_users);
    layout->addWidget(usersLabel);
    m_users->setRootIsDecorated(false);
    m_users->setHeaderLabels({tr("User"), tr("Processes"), tr("CPU"), tr("Memory"), tr("Disk")});
    m_users->setAccessibleName(tr("Users"));
    layout->addWidget(m_users, 2);

    auto* sessionsLabel = new QLabel(tr("&Sessions:"));
    sessionsLabel->setBuddy(m_sessions);
    layout->addWidget(sessionsLabel);
    m_sessions->setRootIsDecorated(false);
    m_sessions->setHeaderLabels({tr("Session"), tr("User"), tr("Seat")});
    m_sessions->setAccessibleName(tr("Sessions"));
    layout->addWidget(m_sessions, 1);

    auto* bottom = new QHBoxLayout;
    m_message->setWordWrap(true);
    bottom->addWidget(m_message, 1);
    auto* lock = new QPushButton(tr("&Lock"));
    auto* signOut = new QPushButton(tr("Sign &Out"));
    bottom->addWidget(lock);
    bottom->addWidget(signOut);
    layout->addLayout(bottom);
    connect(lock, &QPushButton::clicked, this, [this] { sessionAction("Lock", tr("Lock")); });
    connect(signOut, &QPushButton::clicked, this, [this] { sessionAction("Terminate", tr("Sign out")); });
}

void UsersView::refresh()
{
    m_sessions->clear();
    QString error;
    for (const DBusSession& session : listSessions(&error)) {
        auto* item = new QTreeWidgetItem(m_sessions, {session.id, session.user, session.seat});
        item->setData(0, kPathRole, session.path.path());
    }
    if (!error.isEmpty())
        m_message->setText(tr("Sessions are not available: %1").arg(error));
}

void UsersView::onSnapshot(const Snapshot& snapshot)
{
    QMap<unsigned, Totals> totals;
    for (const ProcessSample& process : snapshot.processes) {
        Totals& t = totals[process.info->uid];
        ++t.count;
        t.memory += process.rssBytes.value;
        if (process.cpuPct.known()) {
            t.cpu += process.cpuPct.value;
            t.cpuKnown = true;
        }
        if (process.readBps.known() || process.writeBps.known()) {
            t.disk += (process.readBps.known() ? process.readBps.value : 0) + (process.writeBps.known() ? process.writeBps.value : 0);
            t.diskKnown = true;
        }
    }

    while (m_users->topLevelItemCount() > totals.size())
        delete m_users->takeTopLevelItem(m_users->topLevelItemCount() - 1);
    int row = 0;
    for (auto it = totals.constBegin(); it != totals.constEnd(); ++it, ++row) {
        QTreeWidgetItem* item = m_users->topLevelItem(row);
        if (!item)
            item = new QTreeWidgetItem(m_users);
        const passwd* entry = ::getpwuid(it.key());
        const Totals& t = it.value();
        item->setText(0, entry ? QString::fromLocal8Bit(entry->pw_name) : QString::number(it.key()));
        item->setText(1, QString::number(t.count));
        item->setText(2, t.cpuKnown ? Format::percent(t.cpu) : Format::why(Availability::NotYetSampled));
        item->setText(3, Format::bytes(t.memory));
        // Other users' disk counters are closed to us, so say so rather than show 0.
        item->setText(4, t.diskKnown ? Format::diskRate(t.disk) : Format::why(Availability::AccessDenied));
    }
}

void UsersView::sessionAction(const char* method, const QString& label)
{
    const QTreeWidgetItem* item = m_sessions->currentItem();
    if (!item) {
        m_message->setText(tr("Select a session first."));
        return;
    }
    if (QMessageBox::warning(this, label, tr("%1 session %2 of %3?\n\nUnsaved work may be lost.").arg(label, item->text(0), item->text(1)), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    const QString error = sessionCall(QDBusObjectPath(item->data(0, kPathRole).toString()), method);
    m_message->setText(error.isEmpty() ? tr("%1: done.").arg(label) : tr("%1 failed: %2").arg(label, error));
    refresh();
}

} // namespace fg
