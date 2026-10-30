#pragma once

#include "../shell/view.h"

class QLabel;
class QPushButton;
class QTreeWidget;

namespace fg {

class StartupView final : public View {
    Q_OBJECT
public:
    StartupView();
    void opened() override { refresh(); }
    void closed() override;
    void refresh() override;

private:
    void toggleSelected(bool enabled);
    void measureTimes();

    QTreeWidget* m_list;
    QLabel* m_message;
    QPushButton* m_enable;
    QPushButton* m_disable;
    QPushButton* m_measure;
};

} // namespace fg
