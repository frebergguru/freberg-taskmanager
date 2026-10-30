#include "hub.h"

#include <QTimer>

namespace fg {

SamplerWorker::SamplerWorker(std::unique_ptr<ISystemBackend> backend) : m_backend(std::move(backend)) {}

void SamplerWorker::configure(unsigned wanted, int intervalMs)
{
    if (!m_timer) {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &SamplerWorker::sampleNow);
    }
    const bool changed = wanted != m_wanted;
    m_wanted = wanted;
    if (wanted == SubNone || intervalMs <= 0) {
        m_timer->stop();
        return;
    }
    // Coarse timers let the kernel group our wakeups with other work.
    m_timer->setTimerType(Qt::VeryCoarseTimer);
    if (intervalMs < 1000)
        m_timer->setTimerType(Qt::CoarseTimer);
    m_timer->start(intervalMs);
    if (changed)
        sampleNow();
}

void SamplerWorker::sampleNow()
{
    if (m_wanted != SubNone)
        emit sampled(m_backend->sample(m_wanted));
}

SamplerHub::SamplerHub(QObject* parent)
    : QObject(parent), m_worker(new SamplerWorker(createLinuxBackend())), m_actions(createLinuxBackend())
{
    qRegisterMetaType<fg::SnapshotPtr>();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &SamplerWorker::sampled, this, &SamplerHub::sampled, Qt::QueuedConnection);
    m_thread.start(QThread::LowPriority);
}

SamplerHub::~SamplerHub()
{
    m_thread.quit();
    m_thread.wait();
}

void SamplerHub::setWanted(const void* owner, Subsystems wanted)
{
    m_owners.insert(owner, wanted);
    apply();
}

void SamplerHub::clearWanted(const void* owner)
{
    m_owners.remove(owner);
    apply();
}

void SamplerHub::setIntervalMs(int intervalMs)
{
    m_intervalMs = intervalMs;
    QMetaObject::invokeMethod(m_worker, "configure", Qt::QueuedConnection, Q_ARG(unsigned, m_wanted), Q_ARG(int, m_intervalMs));
}

void SamplerHub::refreshNow()
{
    QMetaObject::invokeMethod(m_worker, "sampleNow", Qt::QueuedConnection);
}

void SamplerHub::apply()
{
    Subsystems wanted = SubNone;
    for (const Subsystems mask : std::as_const(m_owners))
        wanted |= mask;
    if (wanted == m_wanted)
        return;
    m_wanted = wanted;
    QMetaObject::invokeMethod(m_worker, "configure", Qt::QueuedConnection, Q_ARG(unsigned, m_wanted), Q_ARG(int, m_intervalMs));
}

} // namespace fg
