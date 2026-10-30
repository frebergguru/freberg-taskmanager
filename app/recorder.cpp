#include "recorder.h"

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <cmath>

namespace fg {

FlightRecorder::FlightRecorder(SamplerHub& hub, QObject* parent) : QObject(parent), m_hub(hub) {}

FlightRecorder& FlightRecorder::shared(SamplerHub& hub)
{
    static FlightRecorder* recorder = new FlightRecorder(hub, &hub);
    return *recorder;
}

void FlightRecorder::start()
{
    if (m_recording)
        return;
    m_recording = true;
    m_triggerArmed = true;
    connect(&m_hub, &SamplerHub::sampled, this, &FlightRecorder::onSampled, Qt::UniqueConnection);
    m_hub.setWanted(this, SubAll);
}

void FlightRecorder::stop()
{
    if (!m_recording)
        return;
    m_recording = false;
    disconnect(&m_hub, &SamplerHub::sampled, this, &FlightRecorder::onSampled);
    m_hub.clearWanted(this);
}

void FlightRecorder::clear()
{
    m_buffer.clear();
}

void FlightRecorder::setTriggerPercent(int percent)
{
    m_triggerPercent = percent;
    m_triggerArmed = true;
}

void FlightRecorder::onSampled(const SnapshotPtr& snapshot)
{
    // Other pages can ask for less on a sample of their own, and those are skipped.
    if ((snapshot->collected & SubAll) != SubAll)
        return;
    m_buffer.add(frameFromSnapshot(*snapshot));
    emit frameAdded();

    if (m_triggerPercent <= 0 || !snapshot->cpu.usagePct.known())
        return;
    const double usage = snapshot->cpu.usagePct.value;
    if (usage < m_triggerPercent / 2.0)
        m_triggerArmed = true; // fires again only after things have calmed down
    if (usage < m_triggerPercent || !m_triggerArmed)
        return;
    m_triggerArmed = false;

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/recordings");
    QDir().mkpath(dir);
    const QString path = dir + QDateTime::currentDateTime().toString(QStringLiteral("/'spike-'yyyyMMdd-HHmmss'.fgtrace'"));
    std::string error;
    const bool saved = saveTrace(path.toStdString(), m_buffer.frames(), error);
    emit triggered(path, saved ? QString() : QString::fromStdString(error));
}

} // namespace fg
