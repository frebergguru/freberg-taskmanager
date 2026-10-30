#include "diskspace.h"

#include "../format.h"
#include "../widgets/resultsdb.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QStorageInfo>
#include <QThread>
#include <QTimer>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtSql/QSqlQuery>
#include <qdrawutil.h>

namespace fg {

void DiskModel::setRoot(DiskNode* root)
{
    beginResetModel();
    m_root = root;
    endResetModel();
}

QModelIndex DiskModel::index(int row, int column, const QModelIndex& parent) const
{
    const DiskNode* holder = parent.isValid() ? nodeAt(parent) : m_root;
    if (!holder || row < 0 || row >= static_cast<int>(holder->children.size()))
        return {};
    return createIndex(row, column, holder->children[row].get());
}

QModelIndex DiskModel::parent(const QModelIndex& child) const
{
    if (!child.isValid())
        return {};
    DiskNode* up = nodeAt(child)->parent;
    if (!up || up == m_root || !up->parent)
        return {};
    const auto& siblings = up->parent->children;
    const auto found = std::find_if(siblings.begin(), siblings.end(), [up](const auto& item) { return item.get() == up; });
    return createIndex(static_cast<int>(found - siblings.begin()), 0, up);
}

int DiskModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0)
        return 0;
    const DiskNode* holder = parent.isValid() ? nodeAt(parent) : m_root;
    return holder ? static_cast<int>(holder->children.size()) : 0;
}

QVariant DiskModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    const DiskNode* node = nodeAt(index);
    if (role == Qt::TextAlignmentRole && index.column() > 0)
        return QVariant(Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole)
        return {};
    switch (index.column()) {
    case 0: return QString::fromStdString(node->name);
    case 1: return node->denied ? tr("%1 (access denied)").arg(Format::bytes(node->bytes)) : Format::bytes(node->bytes);
    case 2: {
        const std::uint64_t whole = node->parent ? node->parent->bytes : 0;
        return whole ? Format::percent(100.0 * static_cast<double>(node->bytes) / static_cast<double>(whole)) : QString();
    }
    case 3: return node->isDir ? QLocale().toString(static_cast<qulonglong>(node->files)) : QString();
    default: return {};
    }
}

QVariant DiskModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case 0: return tr("Name");
    case 1: return tr("Size");
    case 2: return tr("Share of folder");
    case 3: return tr("Files");
    default: return {};
    }
}

void DiskModel::removeNode(const QModelIndex& index)
{
    DiskNode* node = nodeAt(index);
    DiskNode* up = node->parent;
    if (!up)
        return;
    beginRemoveRows(index.parent(), index.row(), index.row());
    const std::uint64_t bytes = node->bytes;
    const std::uint64_t files = node->files;
    auto& siblings = up->children;
    siblings.erase(std::find_if(siblings.begin(), siblings.end(), [node](const auto& item) { return item.get() == node; }));
    for (DiskNode* above = up; above; above = above->parent) {
        above->bytes -= std::min(above->bytes, bytes);
        above->files -= std::min(above->files, files);
    }
    endRemoveRows();
}

Treemap::Treemap(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(60);
    setAccessibleName(tr("Map of the largest items"));
    setAccessibleDescription(tr("The same items as the list. Use the list with the keyboard."));
}

void Treemap::setNode(DiskNode* node)
{
    m_node = node;
    layoutCells();
    update();
}

void Treemap::resizeEvent(QResizeEvent*)
{
    layoutCells();
}

void Treemap::layoutCells()
{
    m_cells.clear();
    if (!m_node || m_node->bytes == 0)
        return;

    // Slice and dice over the largest items: cheap, and the labels stay readable.
    constexpr std::size_t kMaxCells = 24;
    QRect left = rect().adjusted(2, 2, -2, -2);
    double remaining = 0;
    const std::size_t count = std::min(kMaxCells, m_node->children.size());
    for (std::size_t i = 0; i < count; ++i)
        remaining += static_cast<double>(m_node->children[i]->bytes);
    for (std::size_t i = 0; i < count && remaining > 0 && left.width() > 1 && left.height() > 1; ++i) {
        DiskNode* child = m_node->children[i].get();
        const double share = static_cast<double>(child->bytes) / remaining;
        QRect cell = left;
        if (left.width() >= left.height()) {
            cell.setWidth(std::max(1, static_cast<int>(left.width() * share)));
            left.setLeft(cell.right() + 1);
        } else {
            cell.setHeight(std::max(1, static_cast<int>(left.height() * share)));
            left.setTop(cell.bottom() + 1);
        }
        m_cells.append({cell, child});
        remaining -= static_cast<double>(child->bytes);
    }
}

void Treemap::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QBrush back = palette().brush(QPalette::Base);
    qDrawWinPanel(&p, rect(), palette(), true, &back);
    for (const Cell& cell : std::as_const(m_cells)) {
        const QBrush face = palette().brush(cell.node->isDir ? QPalette::Button : QPalette::Base);
        qDrawWinButton(&p, cell.rect, palette(), false, &face);
        if (cell.rect.width() > 30 && cell.rect.height() > fontMetrics().height() + 4) {
            p.setPen(palette().color(QPalette::ButtonText));
            const QRect text = cell.rect.adjusted(4, 2, -4, -2);
            const QString label = QStringLiteral("%1\n%2").arg(QString::fromStdString(cell.node->name), Format::bytes(cell.node->bytes));
            p.drawText(text, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, label);
        }
    }
}

void Treemap::mousePressEvent(QMouseEvent* event)
{
    for (const Cell& cell : std::as_const(m_cells))
        if (cell.rect.contains(event->position().toPoint())) {
            emit nodeClicked(cell.node);
            return;
        }
}

DiskSpaceView::DiskSpaceView()
    : m_where(new QComboBox), m_scan(new QPushButton(tr("&Scan"))), m_cancel(new QPushButton(tr("&Cancel"))), m_trash(new QPushButton(tr("Move to &Trash..."))),
      m_message(new QLabel), m_tree(new QTreeView), m_map(new Treemap), m_history(new QTreeWidget), m_progress(new QTimer(this)), m_model(new DiskModel(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* top = new QHBoxLayout;
    auto* label = new QLabel(tr("&Folder:"));
    label->setBuddy(m_where);
    m_where->setEditable(true);
    m_where->setAccessibleName(tr("Folder to scan"));
    m_where->lineEdit()->setAccessibleName(tr("Folder to scan"));
    m_where->addItem(QDir::homePath());
    for (const QStorageInfo& volume : QStorageInfo::mountedVolumes())
        if (volume.isValid() && volume.isReady() && !volume.isReadOnly() && m_where->findText(volume.rootPath()) < 0)
            m_where->addItem(volume.rootPath());
    auto* browse = new QPushButton(tr("&Browse..."));
    top->addWidget(label);
    top->addWidget(m_where, 1);
    top->addWidget(browse);
    top->addWidget(m_scan);
    top->addWidget(m_cancel);
    layout->addLayout(top);

    m_tree->setModel(m_model);
    m_tree->setUniformRowHeights(true);
    m_tree->setAnimated(false);
    m_tree->setAccessibleName(tr("Folders and files, largest first"));
    m_tree->setColumnWidth(0, fontMetrics().averageCharWidth() * 40);

    m_history->setRootIsDecorated(false);
    m_history->setHeaderLabels({tr("Finished"), tr("Folder"), tr("Size"), tr("Files"), tr("Took")});
    m_history->setAccessibleName(tr("Earlier scans"));

    auto* split = new QSplitter(Qt::Vertical);
    split->addWidget(m_tree);
    split->addWidget(m_map);
    split->addWidget(m_history);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setStretchFactor(2, 1);
    layout->addWidget(split, 1);

    auto* bottom = new QHBoxLayout;
    m_message->setWordWrap(true);
    bottom->addWidget(m_message, 1);
    bottom->addWidget(m_trash);
    layout->addLayout(bottom);

    m_cancel->setEnabled(false);
    m_trash->setEnabled(false);
    m_progress->setInterval(500);
    connect(m_progress, &QTimer::timeout, this, [this] { m_message->setText(tr("Scanning... %1 files so far.").arg(QLocale().toString(static_cast<qulonglong>(m_filesSeen.load())))); });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString chosen = QFileDialog::getExistingDirectory(this, tr("Choose a folder"), m_where->currentText());
        if (!chosen.isEmpty())
            m_where->setCurrentText(chosen);
    });
    connect(m_scan, &QPushButton::clicked, this, &DiskSpaceView::startScan);
    connect(m_cancel, &QPushButton::clicked, this, &DiskSpaceView::cancelScan);
    connect(m_trash, &QPushButton::clicked, this, &DiskSpaceView::moveSelectedToTrash);
    connect(m_map, &Treemap::nodeClicked, this, [this](DiskNode* node) {
        if (node->isDir)
            m_map->setNode(node);
    });
}

DiskSpaceView::~DiskSpaceView()
{
    cancelScan();
    if (m_thread)
        m_thread->wait();
}

void DiskSpaceView::opened()
{
    loadHistory();
}

void DiskSpaceView::closed()
{
    // A big tree can hold many megabytes, and leaving the page gives them back.
    cancelScan();
    m_map->setNode(nullptr);
    m_model->setRoot(nullptr);
    m_root.reset();
    m_trash->setEnabled(false);
    m_history->clear();
    closeResultsDb();
}

void DiskSpaceView::startScan()
{
    if (m_thread)
        return;
    const std::string path = QDir(m_where->currentText()).absolutePath().toStdString();
    m_map->setNode(nullptr);
    m_model->setRoot(nullptr);
    m_root.reset();
    m_cancelFlag = false;
    m_filesSeen = 0;
    m_pending = std::make_shared<DiskScanResult>();
    m_scan->setEnabled(false);
    m_cancel->setEnabled(true);
    m_trash->setEnabled(false);
    m_clock.start();
    m_progress->start();

    auto pending = m_pending;
    m_thread = QThread::create([path, pending, this] { *pending = scanFolder(path, m_cancelFlag, &m_filesSeen); });
    connect(m_thread, &QThread::finished, this, &DiskSpaceView::finishScan);
    m_thread->start(QThread::LowPriority);
}

void DiskSpaceView::cancelScan()
{
    m_cancelFlag = true;
}

void DiskSpaceView::finishScan()
{
    m_thread->deleteLater();
    m_thread = nullptr;
    m_progress->stop();
    m_scan->setEnabled(true);
    m_cancel->setEnabled(false);

    const std::shared_ptr<DiskScanResult> result = std::move(m_pending);
    if (result->cancelled) {
        m_message->setText(tr("Scan cancelled."));
        return;
    }
    if (result->root->denied) {
        m_message->setText(tr("Could not open that folder."));
        return;
    }
    const double seconds = static_cast<double>(m_clock.elapsed()) / 1000.0;
    m_root = std::move(result->root);
    m_model->setRoot(m_root.get());
    m_map->setNode(m_root.get());
    m_trash->setEnabled(true);

    const QString total = tr("%1 in %2 files.").arg(Format::bytes(m_root->bytes), QLocale().toString(static_cast<qulonglong>(m_root->files)));
    m_message->setText(result->deniedFolders ? tr("%1 %n folder(s) could not be read, so the true size is larger.", nullptr, static_cast<int>(result->deniedFolders)).arg(total) : total);
    saveHistory(QString::fromStdString(m_root->name), static_cast<qint64>(m_root->bytes), static_cast<qint64>(m_root->files), seconds);
    loadHistory();
}

void DiskSpaceView::moveSelectedToTrash()
{
    const QModelIndex index = m_tree->currentIndex().siblingAtColumn(0);
    if (!index.isValid()) {
        m_message->setText(tr("Select something first."));
        return;
    }
    const QString path = QString::fromStdString(nodePath(*m_model->nodeAt(index)));
    if (QMessageBox::warning(this, tr("Move to Trash"), tr("Move this to the trash?\n\n%1").arg(path), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    if (!QFile::moveToTrash(path)) {
        m_message->setText(tr("Could not move %1 to the trash.").arg(path));
        return;
    }
    m_map->setNode(nullptr);
    m_model->removeNode(index);
    m_map->setNode(m_root.get());
    m_message->setText(tr("Moved to the trash: %1").arg(path));
}

void DiskSpaceView::loadHistory()
{
    m_history->clear();
    QSqlQuery query = resultsQuery();
    query.exec(QStringLiteral("SELECT finished, path, bytes, files, seconds FROM scans ORDER BY finished DESC LIMIT 50"));
    const QLocale locale;
    while (query.next())
        new QTreeWidgetItem(m_history, {locale.toString(QDateTime::fromSecsSinceEpoch(query.value(0).toLongLong()), QLocale::ShortFormat), query.value(1).toString(),
                                        Format::bytes(query.value(2).toULongLong()), locale.toString(query.value(3).toLongLong()), tr("%1 s").arg(locale.toString(query.value(4).toDouble(), 'f', 1))});
}

void DiskSpaceView::saveHistory(const QString& path, qint64 bytes, qint64 files, double seconds)
{
    QSqlQuery query = resultsQuery();
    query.prepare(QStringLiteral("INSERT INTO scans (finished, path, bytes, files, seconds) VALUES (?, ?, ?, ?, ?)"));
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    query.addBindValue(path);
    query.addBindValue(bytes);
    query.addBindValue(files);
    query.addBindValue(seconds);
    query.exec();
}

} // namespace fg
