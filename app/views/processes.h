#pragma once

#include "../shell/view.h"

#include <optional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeView;

namespace fg {

class ProcessModel;
class SamplerHub;
struct ProcessSample;

class ProcessesView final : public View {
    Q_OBJECT
public:
    explicit ProcessesView(SamplerHub& hub);

    Subsystems wants() const override;
    void onSnapshotShared(const SnapshotPtr& snapshot) override;
    void closed() override;
    void goToProcess(const ProcessId& id) override;

private:
    std::optional<ProcessId> m_pendingGoTo;
    const ProcessSample* currentProcess() const;
    void showContextMenu(const QPoint& position);
    void showHeaderMenu(const QPoint& position);
    void endSelected(bool force, bool wholeTree);
    void changePriority(int nice);
    void changeIoPriority(int choice);
    void changeAffinity();
    void showProperties();
    void report(const QString& action, const struct ActionResult& result);
    void applyHighlightSetting();
    void exportList();

    SamplerHub& m_hub;
    ProcessModel* m_model;
    QTreeView* m_tree;
    QLineEdit* m_filter;
    QCheckBox* m_treeMode;
    QPushButton* m_endTask;
    QPushButton* m_export;
    QLabel* m_message;
};

} // namespace fg
