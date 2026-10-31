#pragma once

#include "../shell/view.h"

#include <atomic>
#include <memory>

class QLabel;
class QLineEdit;
class QPushButton;
class QThread;
class QTreeWidget;

namespace fg {

class InstalledAppsView final : public View {
    Q_OBJECT
public:
    InstalledAppsView();
    ~InstalledAppsView() override;
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
    QThread* m_scan = nullptr;
    std::shared_ptr<std::atomic<bool>> m_cancelScan;
    bool m_scanAgain = false;
    int m_generation = 0; // a newer listing outruns an older one still in progress
};

} // namespace fg
