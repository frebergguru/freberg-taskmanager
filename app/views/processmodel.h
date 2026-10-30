// Live table or tree of processes. Rebuilt on each sample, and selection and
// expansion survive because rows are matched by PID and start time.
#pragma once

#include "core/snapshot.h"

#include <QAbstractItemModel>
#include <QHash>
#include <vector>

namespace fg {

class ProcessModel final : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Column { Name, Pid, User, Status, Cpu, Memory, Disk, Energy, Network, Gpu, Threads, Nice, Group, Command, ColumnCount };

    explicit ProcessModel(QObject* parent = nullptr);

    void update(const Snapshot& snapshot);
    void setFilter(const QString& filter);
    void setTree(bool tree);
    bool isTree() const { return m_tree; }

    // Newly seen processes that have children, so the view can open them once.
    const QList<QModelIndex>& newParents() const { return m_newParents; }

    const ProcessSample* sampleAt(const QModelIndex& index) const;
    QModelIndex indexOf(const ProcessId& id) const;
    bool existsUnfiltered(const ProcessId& id) const;
    std::vector<ProcessId> withDescendants(const ProcessId& id) const;

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& = {}) const override { return ColumnCount; }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

private:
    struct Node {
        ProcessSample sample;
        int parent = -1;
        int row = 0;
        std::vector<int> children;
    };

    void rebuild();
    void sortSiblings(std::vector<int>& siblings);
    bool lessThan(const Node& a, const Node& b) const;
    QString userName(unsigned uid) const;
    Reading<double> energyOf(const ProcessSample& s) const;

    SnapshotPtr m_snapshot;
    std::vector<Node> m_nodes;
    std::vector<int> m_roots;
    QHash<std::uint64_t, int> m_nodeOf; // (pid, start) hash -> node
    QSet<std::uint64_t> m_seen;
    QList<QModelIndex> m_newParents;
    QString m_filter;
    bool m_tree = true;
    int m_sortColumn = Cpu;
    Qt::SortOrder m_sortOrder = Qt::DescendingOrder;
    mutable QHash<unsigned, QString> m_userNames;
};

} // namespace fg
