#pragma once

#include "../shell/view.h"
#include "core/history.h"

#include <deque>
#include <optional>
#include <vector>

class QLabel;
class QTreeWidget;

namespace fg {

class Graph;
class InfoGrid;
class SortItem;

class ThermalsView final : public View {
    Q_OBJECT
public:
    ThermalsView();

    // Cheap: a few small files. Nothing here walks the processes.
    Subsystems wants() const override { return SubThermal; }
    void onSnapshot(const Snapshot& snapshot) override;
    void samplesMissed() override;

private:
    struct Tracked {
        History history;
        std::optional<double> lowest, highest;
    };

    void showSelected();
    QString statusOf(const ThermalSensor& sensor) const;

    std::deque<Tracked> m_tracked; // one per sensor, in the order the system lists them
    std::vector<SortItem*> m_rows; // likewise
    History m_hotspot;
    std::optional<std::uint64_t> m_firstThrottleCount;
    int m_shownSensor = -2; // -1 is the CPU hotspot

    QLabel* m_graphTitle;
    Graph* m_graph;
    InfoGrid* m_grid;
    QTreeWidget* m_sensors;
    QTreeWidget* m_fans;
    QLabel* m_fansLabel;
    QLabel* m_noFans;
};

} // namespace fg
