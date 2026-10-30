#pragma once

#include "../shell/view.h"
#include "core/history.h"

#include <QHash>
#include <deque>
#include <memory>

class QComboBox;
class QGridLayout;
class QLabel;
class QListWidget;
class QProgressBar;
class QStackedWidget;

namespace fg {

class Graph;
class InfoGrid;
class SamplerHub;

class PerformanceView final : public View {
    Q_OBJECT
public:
    explicit PerformanceView(SamplerHub& hub);

    Subsystems wants() const override;
    void onSnapshot(const Snapshot& snapshot) override;
    void samplesMissed() override;

protected:
    void changeEvent(QEvent* event) override;

private:
    struct Page {
        QString key;
        QWidget* widget = nullptr;
        QLabel* heading = nullptr;
        QLabel* subheading = nullptr;
        Graph* graph = nullptr;
        InfoGrid* grid = nullptr;
        History first, second;
    };

    Page& page(const QString& key, const QString& label, const QString& heading);
    void updateCpu(const Snapshot& snapshot);
    void updateMemory(const Snapshot& snapshot);
    void updateDisks(const Snapshot& snapshot);
    void updateNetworks(const Snapshot& snapshot);
    void updateGpus(const Snapshot& snapshot);
    void updateThermals(const Snapshot& snapshot);
    void updateEnergy(const Snapshot& snapshot);
    void rebuildCoreGraphs();
    void selectionChanged();

    SamplerHub& m_hub;
    QListWidget* m_list;
    QStackedWidget* m_stack;
    std::deque<Page> m_pages; // deque, so Graphs may keep pointers to the histories
    QHash<QString, Page*> m_byKey;

    // CPU page: one small graph per logical processor or NUMA node.
    QComboBox* m_cpuMode = nullptr;
    QWidget* m_coreArea = nullptr;
    QGridLayout* m_coreGrid = nullptr;
    std::deque<History> m_coreHistory;
    QList<Graph*> m_coreGraphs;

    QProgressBar* m_memoryBar = nullptr;
};

} // namespace fg
