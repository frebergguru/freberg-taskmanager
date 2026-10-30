#pragma once

#include "../shell/view.h"
#include "core/history.h"

#include <QHash>

class QTreeWidget;

namespace fg {

class Graph;
class InfoGrid;
class SamplerHub;

class PowerFreqView final : public View {
    Q_OBJECT
public:
    explicit PowerFreqView(SamplerHub& hub);

    Subsystems wants() const override { return SubCpu | SubPower; }
    void onSnapshot(const Snapshot& snapshot) override;
    void opened() override { refresh(); }
    void samplesMissed() override;
    void refresh() override;

private:
    SamplerHub& m_hub;
    History m_freqHistory, m_powerHistory;
    Graph* m_freqGraph;
    Graph* m_powerGraph;
    InfoGrid* m_grid;
    QTreeWidget* m_cores;
    QTreeWidget* m_idle;
};

} // namespace fg
