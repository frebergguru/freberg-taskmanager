#pragma once

#include "../shell/view.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace fg {

class InstalledAppsView final : public View {
    Q_OBJECT
public:
    InstalledAppsView();
    void opened() override { refresh(); }
    void closed() override;
    void refresh() override;

private:
    void applyFilter();
    void uninstall();

    QTreeWidget* m_list;
    QLineEdit* m_filter;
    QLabel* m_message;
    QPushButton* m_uninstall;
    int m_generation = 0; // a newer listing outruns an older one still in progress
};

} // namespace fg
