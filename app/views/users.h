#pragma once

#include "../shell/view.h"

class QLabel;
class QTreeWidget;

namespace fg {

class UsersView final : public View {
    Q_OBJECT
public:
    UsersView();
    Subsystems wants() const override { return SubProcesses; }
    void onSnapshot(const Snapshot& snapshot) override;
    void opened() override { refresh(); }
    void refresh() override;

private:
    void sessionAction(const char* method, const QString& label);

    QTreeWidget* m_users;
    QTreeWidget* m_sessions;
    QLabel* m_message;
};

} // namespace fg
