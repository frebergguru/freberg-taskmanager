#pragma once

#include "../shell/view.h"
#include "core/diskscan.h"

#include <QAbstractItemModel>
#include <QElapsedTimer>
#include <atomic>

class QComboBox;
class QLabel;
class QPushButton;
class QTimer;
class QTreeView;
class QTreeWidget;

namespace fg {

class DiskModel final : public QAbstractItemModel {
    Q_OBJECT
public:
    using QAbstractItemModel::QAbstractItemModel;
    void setRoot(DiskNode* root);
    DiskNode* nodeAt(const QModelIndex& index) const { return static_cast<DiskNode*>(index.internalPointer()); }
    void removeNode(const QModelIndex& index);

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& = {}) const override { return 4; }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    DiskNode* m_root = nullptr;
};

// Rectangles sized by share of the disk, with the folder's own name written inside.
class Treemap final : public QWidget {
    Q_OBJECT
public:
    explicit Treemap(QWidget* parent = nullptr);
    void setNode(DiskNode* node);

signals:
    void nodeClicked(fg::DiskNode* node);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Cell {
        QRect rect;
        DiskNode* node;
    };
    void layoutCells();
    DiskNode* m_node = nullptr;
    QList<Cell> m_cells;
};

class DiskSpaceView final : public View {
    Q_OBJECT
public:
    DiskSpaceView();
    ~DiskSpaceView() override;
    void opened() override;
    void closed() override;

private:
    void startScan();
    void cancelScan();
    void finishScan();
    void moveSelectedToTrash();
    void loadHistory();
    void saveHistory(const QString& path, qint64 bytes, qint64 files, double seconds);
    void selectionChanged();

    QComboBox* m_where;
    QPushButton* m_scan;
    QPushButton* m_cancel;
    QPushButton* m_trash;
    QLabel* m_message;
    QTreeView* m_tree;
    Treemap* m_map;
    QTreeWidget* m_history;
    QTimer* m_progress;
    DiskModel* m_model;

    QThread* m_thread = nullptr;
    std::atomic<bool> m_cancelFlag{false};
    std::atomic<std::uint64_t> m_filesSeen{0};
    std::shared_ptr<DiskScanResult> m_pending;
    std::unique_ptr<DiskNode> m_root;
    QElapsedTimer m_clock;
};

} // namespace fg
