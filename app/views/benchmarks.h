#pragma once

#include "../shell/view.h"

#include <atomic>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QThread;
class QTreeWidget;

namespace fg {

class BenchmarksView final : public View {
    Q_OBJECT
public:
    BenchmarksView();
    ~BenchmarksView() override;
    void opened() override { loadResults(); }
    void closed() override;

private:
    void start();
    void loadResults();

    QLineEdit* m_folder;
    QPushButton* m_run;
    QPushButton* m_cancel;
    QProgressBar* m_bar;
    QLabel* m_message;
    QTreeWidget* m_results;
    QThread* m_thread = nullptr;
    std::atomic<bool> m_cancelFlag{false};
};

} // namespace fg
