#include "processmodel.h"

#include "../format.h"
#include "../settings.h"

#include <QBrush>
#include <QSet>
#include <algorithm>
#include <pwd.h>
#include <unistd.h>
#include <unordered_map>

namespace fg {

namespace {

std::uint64_t keyOf(const ProcessId& id)
{
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(id.pid)) << 40) ^ id.startTicks;
}

QString stateText(char state)
{
    switch (state) {
    case 'R': return ProcessModel::tr("Running");
    case 'S': return ProcessModel::tr("Sleeping");
    case 'D': return ProcessModel::tr("Waiting on disk");
    case 'Z': return ProcessModel::tr("Zombie");
    case 'T': return ProcessModel::tr("Stopped");
    case 't': return ProcessModel::tr("Being traced");
    case 'I': return ProcessModel::tr("Idle");
    default: return QString(QLatin1Char(state));
    }
}

double readingOrLow(const Reading<double>& r)
{
    return r.known() ? r.value : -1.0;
}

double diskTotal(const ProcessSample& s)
{
    if (!s.readBps.known() && !s.writeBps.known())
        return -1.0;
    return (s.readBps.known() ? s.readBps.value : 0) + (s.writeBps.known() ? s.writeBps.value : 0);
}

} // namespace

ProcessModel::ProcessModel(QObject* parent) : QAbstractItemModel(parent)
{
    const Settings& settings = Settings::instance();
    m_sortColumn = settings.value("processes/sortColumn", Cpu).toInt();
    m_sortOrder = static_cast<Qt::SortOrder>(settings.value("processes/sortOrder", Qt::DescendingOrder).toInt());
    m_tree = settings.value("processes/tree", true).toBool();
}

QString ProcessModel::userName(unsigned uid) const
{
    const auto found = m_userNames.constFind(uid);
    if (found != m_userNames.constEnd())
        return *found;
    const passwd* entry = ::getpwuid(uid);
    const QString name = entry ? QString::fromLocal8Bit(entry->pw_name) : QString::number(uid);
    m_userNames.insert(uid, name);
    return name;
}

void ProcessModel::update(const Snapshot& snapshot)
{
    // Holding the pointer keeps every process name alive without copying them.
    m_snapshot = std::make_shared<const Snapshot>(snapshot);
    rebuild();
}

void ProcessModel::setFilter(const QString& filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    if (m_snapshot)
        rebuild();
}

void ProcessModel::setTree(bool tree)
{
    if (m_tree == tree)
        return;
    m_tree = tree;
    Settings::instance().setValue("processes/tree", tree);
    if (m_snapshot)
        rebuild();
}

void ProcessModel::sort(int column, Qt::SortOrder order)
{
    m_sortColumn = column;
    m_sortOrder = order;
    Settings::instance().setValue("processes/sortColumn", column);
    Settings::instance().setValue("processes/sortOrder", static_cast<int>(order));
    if (m_snapshot)
        rebuild();
}

bool ProcessModel::lessThan(const Node& a, const Node& b) const
{
    const ProcessSample& x = a.sample;
    const ProcessSample& y = b.sample;
    switch (m_sortColumn) {
    case Pid: return x.info->id.pid < y.info->id.pid;
    case User: return x.info->uid < y.info->uid;
    case Status: return x.state < y.state;
    case Cpu: return readingOrLow(x.cpuPct) < readingOrLow(y.cpuPct);
    case Memory: return x.rssBytes.value < y.rssBytes.value;
    case Disk: return diskTotal(x) < diskTotal(y);
    case Threads: return x.threads < y.threads;
    case Nice: return x.nice < y.nice;
    case Command: return x.info->commandLine < y.info->commandLine;
    default: return QString::compare(QString::fromStdString(x.info->name), QString::fromStdString(y.info->name), Qt::CaseInsensitive) < 0;
    }
}

void ProcessModel::sortSiblings(std::vector<int>& siblings)
{
    std::sort(siblings.begin(), siblings.end(), [this](int a, int b) {
        const bool less = lessThan(m_nodes[a], m_nodes[b]);
        const bool greater = lessThan(m_nodes[b], m_nodes[a]);
        if (!less && !greater)
            return m_nodes[a].sample.info->id.pid < m_nodes[b].sample.info->id.pid; // keeps rows from jumping
        return m_sortOrder == Qt::AscendingOrder ? less : greater;
    });
    for (std::size_t row = 0; row < siblings.size(); ++row)
        m_nodes[siblings[row]].row = static_cast<int>(row);
}

void ProcessModel::rebuild()
{
    emit layoutAboutToBeChanged();

    // Remember which process each persistent index stands for.
    const QModelIndexList persistent = persistentIndexList();
    std::vector<std::pair<ProcessId, int>> remembered;
    remembered.reserve(static_cast<std::size_t>(persistent.size()));
    for (const QModelIndex& index : persistent) {
        const ProcessSample* sample = sampleAt(index);
        remembered.emplace_back(sample ? sample->info->id : ProcessId{}, index.column());
    }

    m_nodes.clear();
    m_roots.clear();
    m_nodeOf.clear();

    const std::vector<ProcessSample>& all = m_snapshot->processes;
    m_nodes.reserve(all.size());
    const int self = ::getpid();
    const bool showSelf = Settings::instance().selfMonitor();

    const auto matches = [this](const ProcessSample& s) {
        if (m_filter.isEmpty())
            return true;
        return QString::fromStdString(s.info->name).contains(m_filter, Qt::CaseInsensitive)
            || QString::fromStdString(s.info->commandLine).contains(m_filter, Qt::CaseInsensitive)
            || QString::number(s.info->id.pid) == m_filter
            || userName(s.info->uid).contains(m_filter, Qt::CaseInsensitive);
    };

    std::unordered_map<int, const ProcessSample*> byPid;
    byPid.reserve(all.size());
    for (const ProcessSample& s : all)
        byPid.emplace(s.info->id.pid, &s);

    // A process is kept when it matches, and in a tree its ancestors stay too.
    QSet<int> keep;
    for (const ProcessSample& s : all) {
        if (s.info->id.pid == self && !showSelf)
            continue;
        if (!matches(s))
            continue;
        keep.insert(s.info->id.pid);
        if (m_tree) {
            const ProcessSample* up = &s;
            while (up->ppid != 0) {
                const auto parent = byPid.find(up->ppid);
                if (parent == byPid.end() || keep.contains(up->ppid))
                    break;
                keep.insert(up->ppid);
                up = parent->second;
            }
        }
    }

    std::unordered_map<int, int> nodeOfPid;
    nodeOfPid.reserve(static_cast<std::size_t>(keep.size()));
    for (const ProcessSample& s : all) {
        if (!keep.contains(s.info->id.pid))
            continue;
        nodeOfPid.emplace(s.info->id.pid, static_cast<int>(m_nodes.size()));
        m_nodeOf.insert(keyOf(s.info->id), static_cast<int>(m_nodes.size()));
        m_nodes.push_back({s, -1, 0, {}});
    }
    for (std::size_t i = 0; i < m_nodes.size(); ++i) {
        const auto parent = m_tree ? nodeOfPid.find(m_nodes[i].sample.ppid) : nodeOfPid.end();
        if (parent != nodeOfPid.end() && parent->second != static_cast<int>(i)) {
            m_nodes[i].parent = parent->second;
            m_nodes[parent->second].children.push_back(static_cast<int>(i));
        } else {
            m_roots.push_back(static_cast<int>(i));
        }
    }
    sortSiblings(m_roots);
    for (Node& node : m_nodes)
        sortSiblings(node.children);

    QModelIndexList updated;
    updated.reserve(persistent.size());
    for (const auto& [id, column] : remembered) {
        QModelIndex found = indexOf(id);
        updated << (found.isValid() ? found.siblingAtColumn(column) : QModelIndex());
    }
    changePersistentIndexList(persistent, updated);

    m_newParents.clear();
    for (std::size_t i = 0; i < m_nodes.size(); ++i) {
        const std::uint64_t key = keyOf(m_nodes[i].sample.info->id);
        if (m_seen.contains(key))
            continue;
        m_seen.insert(key);
        if (!m_nodes[i].children.empty())
            m_newParents << createIndex(m_nodes[i].row, 0, static_cast<quintptr>(i));
    }

    emit layoutChanged();
}

const ProcessSample* ProcessModel::sampleAt(const QModelIndex& index) const
{
    if (!index.isValid() || index.internalId() >= m_nodes.size())
        return nullptr;
    return &m_nodes[index.internalId()].sample;
}

QModelIndex ProcessModel::indexOf(const ProcessId& id) const
{
    const auto found = m_nodeOf.constFind(keyOf(id));
    if (found == m_nodeOf.constEnd())
        return {};
    return createIndex(m_nodes[*found].row, 0, static_cast<quintptr>(*found));
}

bool ProcessModel::existsUnfiltered(const ProcessId& id) const
{
    if (!m_snapshot)
        return false;
    return std::any_of(m_snapshot->processes.begin(), m_snapshot->processes.end(), [&id](const ProcessSample& s) { return s.info->id == id; });
}

std::vector<ProcessId> ProcessModel::withDescendants(const ProcessId& id) const
{
    std::vector<ProcessId> out{id};
    if (!m_snapshot)
        return out;
    // Works from the full list, so a filter cannot hide a child from a tree kill.
    for (std::size_t i = 0; i < out.size(); ++i)
        for (const ProcessSample& s : m_snapshot->processes)
            if (s.ppid == out[i].pid)
                out.push_back(s.info->id);
    return out;
}

QModelIndex ProcessModel::index(int row, int column, const QModelIndex& parent) const
{
    const std::vector<int>& siblings = parent.isValid() ? m_nodes[parent.internalId()].children : m_roots;
    if (row < 0 || row >= static_cast<int>(siblings.size()) || column < 0 || column >= ColumnCount)
        return {};
    return createIndex(row, column, static_cast<quintptr>(siblings[row]));
}

QModelIndex ProcessModel::parent(const QModelIndex& child) const
{
    if (!child.isValid() || child.internalId() >= m_nodes.size())
        return {};
    const int parent = m_nodes[child.internalId()].parent;
    if (parent < 0)
        return {};
    return createIndex(m_nodes[parent].row, 0, static_cast<quintptr>(parent));
}

int ProcessModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0)
        return 0;
    if (!parent.isValid())
        return static_cast<int>(m_roots.size());
    return static_cast<int>(m_nodes[parent.internalId()].children.size());
}

QVariant ProcessModel::data(const QModelIndex& index, int role) const
{
    const ProcessSample* s = sampleAt(index);
    if (!s)
        return {};
    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case Pid:
        case Cpu:
        case Memory:
        case Disk:
        case Threads:
        case Nice:
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return {};
        }
    }
    if (role == Qt::ToolTipRole && index.column() == Name)
        return QString::fromStdString(s->info->commandLine);
    if (role != Qt::DisplayRole && role != Qt::AccessibleTextRole)
        return {};

    switch (index.column()) {
    case Name: return QString::fromStdString(s->info->name);
    case Pid: return s->info->id.pid;
    case User: return userName(s->info->uid);
    case Status: return stateText(s->state);
    case Cpu: return Format::percent(s->cpuPct);
    case Memory: return Format::bytes(s->rssBytes);
    case Disk: {
        if (!s->readBps.known() && !s->writeBps.known())
            return Format::why(s->readBps.state);
        return Format::diskRate(diskTotal(*s));
    }
    case Threads: return s->threads;
    case Nice: return s->nice;
    case Command: return QString::fromStdString(s->info->commandLine);
    default: return {};
    }
}

QVariant ProcessModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case Name: return tr("Name");
    case Pid: return tr("PID");
    case User: return tr("User");
    case Status: return tr("Status");
    case Cpu: return tr("CPU");
    case Memory: return tr("Memory");
    case Disk: return tr("Disk");
    case Threads: return tr("Threads");
    case Nice: return tr("Nice");
    case Command: return tr("Command line");
    default: return {};
    }
}

} // namespace fg
