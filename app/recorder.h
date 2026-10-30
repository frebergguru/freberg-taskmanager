// Keeps the Flight Recorder running whichever page is open, and also while the
// window is hidden. Off until the user starts it; it then reads every subsystem
// on each update, including walking all processes.
#pragma once

#include "core/trace.h"
#include "hub.h"

#include <QObject>

namespace fg {

class FlightRecorder final : public QObject {
    Q_OBJECT
public:
    explicit FlightRecorder(SamplerHub& hub, QObject* parent = nullptr);
    static FlightRecorder& shared(SamplerHub& hub);

    void start();
    void stop();
    bool isRecording() const { return m_recording; }
    const TraceBuffer& buffer() const { return m_buffer; }
    void clear();

    // Saves on its own the first time CPU use passes a level. 0 turns it off.
    void setTriggerPercent(int percent);
    int triggerPercent() const { return m_triggerPercent; }
    void setByteLimit(std::size_t bytes) { m_buffer.setByteLimit(bytes); }

signals:
    void frameAdded();
    void triggered(const QString& path, const QString& error);

private:
    void onSampled(const SnapshotPtr& snapshot);

    SamplerHub& m_hub;
    TraceBuffer m_buffer;
    bool m_recording = false;
    int m_triggerPercent = 0;
    bool m_triggerArmed = true;
};

// Starts a recording at launch, if the user asked for that.
void startFlightRecorderIfEnabled(SamplerHub& hub);

} // namespace fg
