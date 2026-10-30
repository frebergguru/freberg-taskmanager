// Linux keeps no per-app history, so we keep our own. Off by default: while it is
// on, the app walks all processes on each update, even when the window is hidden.
#pragma once

#include "../shell/view.h"

#include <QHash>
#include <QSqlDatabase>

class QCheckBox;
class QLabel;
class QPushButton;
class QTimer;
class QTreeWidget;

namespace fg {

class SamplerHub;

class AppHistoryLog final : public QObject {
    Q_OBJECT
public:
    explicit AppHistoryLog(SamplerHub& hub, QObject* parent = nullptr);
    ~AppHistoryLog() override;

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    void flush();
    void clearAll();

    struct Row {
        QString app;
        double cpuSeconds = 0;
        double diskBytes = 0;
        qint64 firstSeen = 0, lastSeen = 0;
    };
    QList<Row> rows();

private:
    void onSampled(const SnapshotPtr& snapshot);
    bool openDatabase();

    struct Pending {
        double cpuSeconds = 0;
        double diskBytes = 0;
    };
    SamplerHub& m_hub;
    QHash<QString, Pending> m_pending;
    QTimer* m_flushTimer = nullptr;
    std::int64_t m_previousMs = 0;
    bool m_enabled = false;
    bool m_opened = false;
    int m_logicalCpus = 1;
};

class AppHistoryView final : public View {
    Q_OBJECT
public:
    explicit AppHistoryView(SamplerHub& hub);
    void opened() override { refresh(); }
    void closed() override;
    void refresh() override;

private:
    AppHistoryLog* m_log;
    QCheckBox* m_enabled;
    QTreeWidget* m_list;
    QLabel* m_message;
};

// Starts logging at launch when the user has left it on.
void startAppHistoryIfEnabled(SamplerHub& hub);

} // namespace fg
