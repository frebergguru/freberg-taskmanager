#pragma once

#include "../shell/view.h"

#include <QList>

class QLabel;
class QProgressBar;
class QTreeWidget;

namespace fg {

class SummaryView final : public View {
    Q_OBJECT
public:
    SummaryView();

    Subsystems wants() const override { return SubAll; }
    void onSnapshot(const Snapshot& snapshot) override;

private:
    struct Tile {
        QLabel* value = nullptr;
        QProgressBar* bar = nullptr;
    };
    Tile addTile(const QString& name, int row, int column);

    Tile m_cpu, m_memory, m_disk, m_network, m_gpu, m_thermal, m_energy, m_pressure;
    QTreeWidget* m_top;
};

} // namespace fg
