// Runs the backend on its own thread. The timer only exists while someone wants
// data; with no subscribers the thread sleeps in its event loop and costs nothing.
#pragma once

#include "core/snapshot.h"
#include "platform/backend.h"

#include <QHash>
#include <QObject>
#include <QThread>

class QTimer;

namespace fg {

class SamplerWorker final : public QObject {
    Q_OBJECT
public:
    explicit SamplerWorker(std::unique_ptr<ISystemBackend> backend);

public slots:
    void configure(unsigned wanted, int intervalMs);
    void sampleNow();

signals:
    void sampled(fg::SnapshotPtr snapshot);

private:
    std::unique_ptr<ISystemBackend> m_backend;
    QTimer* m_timer = nullptr;
    unsigned m_wanted = 0;
};

class SamplerHub final : public QObject {
    Q_OBJECT
public:
    explicit SamplerHub(QObject* parent = nullptr);
    ~SamplerHub() override;

    // Each owner states what it needs; the sampler reads the union of them all.
    void setWanted(const void* owner, Subsystems wanted);
    void clearWanted(const void* owner);

    // 0 pauses updates.
    void setIntervalMs(int intervalMs);
    void refreshNow();

    Subsystems wanted() const { return m_wanted; }

    // For actions and one-off questions. Separate from the sampling thread's backend.
    ISystemBackend& backend() { return *m_actions; }

signals:
    void sampled(fg::SnapshotPtr snapshot);

private:
    void apply();

    QThread m_thread;
    SamplerWorker* m_worker;
    std::unique_ptr<ISystemBackend> m_actions;
    QHash<const void*, Subsystems> m_owners;
    Subsystems m_wanted = SubNone;
    int m_intervalMs = 1000;
};

} // namespace fg

Q_DECLARE_METATYPE(fg::SnapshotPtr)
