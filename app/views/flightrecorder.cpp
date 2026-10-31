#include "flightrecorder.h"

#include "../format.h"
#include "../hub.h"
#include "../recorder.h"
#include "../settings.h"
#include "../style/style.h"
#include "../widgets/confirm.h"
#include "../widgets/infogrid.h"

#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <qdrawutil.h>

namespace fg {

namespace {

constexpr int kReplayMs = 100;
constexpr int kAnalyseFramesEachSide = 30;

struct Panel {
    const char* title;
    TraceChannel channel;
    TraceChannel second;
    bool hasSecond;
    double fixedMax;
};

const Panel kPanels[] = {
    {QT_TRANSLATE_NOOP("fg::FlightRecorderView", "CPU"), TraceChannel::Cpu, TraceChannel::Cpu, false, 100},
    {QT_TRANSLATE_NOOP("fg::FlightRecorderView", "Memory"), TraceChannel::Memory, TraceChannel::Memory, false, 0},
    {QT_TRANSLATE_NOOP("fg::FlightRecorderView", "Disk (read, write)"), TraceChannel::DiskRead, TraceChannel::DiskWrite, true, 0},
    {QT_TRANSLATE_NOOP("fg::FlightRecorderView", "Network (receive, send)"), TraceChannel::NetRx, TraceChannel::NetTx, true, 0},
    {QT_TRANSLATE_NOOP("fg::FlightRecorderView", "GPU"), TraceChannel::Gpu, TraceChannel::Gpu, false, 100},
    {QT_TRANSLATE_NOOP("fg::FlightRecorderView", "Temperature"), TraceChannel::Temp, TraceChannel::Temp, false, 110},
    {QT_TRANSLATE_NOOP("fg::FlightRecorderView", "Power"), TraceChannel::Power, TraceChannel::Power, false, 0},
    {QT_TRANSLATE_NOOP("fg::FlightRecorderView", "Pressure (CPU, memory)"), TraceChannel::PressureCpu, TraceChannel::PressureMemory, true, 100},
};

} // namespace

// A peak with nothing behind it says so, and is never shown as 0.
static QString notRecorded() { return QCoreApplication::translate("fg::FlightRecorderView", "Not recorded"); }
static QString peakPercent(double value) { return std::isnan(value) ? notRecorded() : Format::percent(value); }
static QString peakBytes(double value) { return std::isnan(value) ? notRecorded() : Format::bytes(static_cast<std::uint64_t>(value)); }
static QString peakDiskRate(double value) { return std::isnan(value) ? notRecorded() : Format::diskRate(value); }
static QString peakRate(double value) { return std::isnan(value) ? notRecorded() : Format::rate(value); }
static QString peakTemperature(double value) { return std::isnan(value) ? notRecorded() : Format::temperature(value); }
static QString peakWatts(double value) { return std::isnan(value) ? notRecorded() : Format::watts(value); }

// One panel: the whole recording across its width, with a line at the cursor.
class TraceStrip final : public QWidget {
public:
    TraceStrip(const Panel& panel, QWidget* parent) : QWidget(parent), m_panel(panel)
    {
        setMinimumHeight(40);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAccessibleName(QCoreApplication::translate("fg::FlightRecorderView", panel.title));
    }

    std::function<void(int)> onScrub;

    void setFrames(const std::deque<TraceFrame>* frames)
    {
        m_frames = frames;
        update();
    }

    void setCursorIndex(int index)
    {
        if (index == m_cursor)
            return;
        m_cursor = index;
        update();
    }

protected:
    void mousePressEvent(QMouseEvent* event) override { scrubTo(event->position().x()); }
    void mouseMoveEvent(QMouseEvent* event) override { scrubTo(event->position().x()); }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const Scheme& scheme = static_cast<const Style*>(QApplication::style())->scheme();
        const QBrush back(scheme.graphBack);
        sunkenPanel(&p, rect(), palette(), &back);
        const QRect area = rect().adjusted(2, 2, -2, -2);
        p.setClipRect(area);
        p.setPen(scheme.graphGrid);
        for (int y = area.bottom(); y >= area.top(); y -= 12)
            p.drawLine(area.left(), y, area.right(), y);

        const QString caption = accessibleName();
        const auto drawCaption = [&] {
            const QRect box = fontMetrics().boundingRect(caption).translated(area.left() + 4 - fontMetrics().boundingRect(caption).left(), area.top() + 2 - fontMetrics().boundingRect(caption).top());
            p.setRenderHint(QPainter::Antialiasing, false);
            p.fillRect(box.adjusted(-3, -1, 3, 1), scheme.graphBack); // readable over the grid and the lines
            p.setPen(scheme.graphLine);
            p.drawText(box, Qt::AlignLeft | Qt::AlignVCenter, caption);
        };

        if (!m_frames || m_frames->empty()) {
            drawCaption();
            return;
        }
        const std::size_t size = m_frames->size();

        double top = m_panel.fixedMax;
        if (top <= 0) {
            for (const TraceFrame& frame : *m_frames) {
                for (const TraceChannel channel : {m_panel.channel, m_panel.second}) {
                    const float value = channelValue(frame, channel);
                    if (!std::isnan(value))
                        top = std::max(top, static_cast<double>(value));
                }
            }
            top = top > 0 ? top * 1.1 : 1.0;
        }

        const auto draw = [&](TraceChannel channel, const QColor& color, bool second) {
            const int height = area.height() - 1;
            // More frames than pixels: each column shows its highest value, so spikes survive.
            QPainterPath line;
            bool have = false;
            for (int x = 0; x < area.width(); ++x) {
                const std::size_t from = static_cast<std::size_t>(x) * size / static_cast<std::size_t>(area.width());
                const std::size_t to = std::max(from + 1, static_cast<std::size_t>(x + 1) * size / static_cast<std::size_t>(area.width()));
                if (from >= size)
                    break;
                double best = -1;
                for (std::size_t i = from; i < std::min(to, size); ++i) {
                    const float value = channelValue((*m_frames)[i], channel);
                    if (!std::isnan(value))
                        best = std::max(best, static_cast<double>(value));
                }
                if (best < 0) {
                    have = false; // a missing sample is a gap
                    continue;
                }
                const QPointF point(area.left() + x, area.bottom() - std::min(1.0, best / top) * height);
                if (have)
                    line.lineTo(point);
                else
                    line.moveTo(point);
                have = true;
            }
            strokeSeries(p, line, color, area.bottom(), second);
        };
        draw(m_panel.channel, scheme.graphLine, false);
        if (m_panel.hasSecond)
            draw(m_panel.second, scheme.graphLine2, true);

        if (m_cursor >= 0 && size > 0) {
            const int x = area.left() + static_cast<int>(static_cast<double>(m_cursor) / static_cast<double>(std::max<std::size_t>(1, size - 1)) * (area.width() - 1));
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(scheme.text);
            p.drawLine(x, area.top(), x, area.bottom());
        }
        drawCaption();
    }

private:
    void scrubTo(double x)
    {
        if (!m_frames || m_frames->empty() || !onScrub)
            return;
        const double fraction = std::clamp((x - 2) / std::max(1, width() - 4), 0.0, 1.0);
        onScrub(static_cast<int>(fraction * static_cast<double>(m_frames->size() - 1)));
    }

    const Panel& m_panel;
    const std::deque<TraceFrame>* m_frames = nullptr;
    int m_cursor = -1;
};

FlightRecorderView::FlightRecorderView(SamplerHub& hub)
    : m_recorder(FlightRecorder::shared(hub)), m_scrub(new QSlider(Qt::Horizontal)), m_record(new QPushButton), m_save(new QPushButton(tr("&Save..."))),
      m_open(new QPushButton(tr("&Open..."))), m_export(new QPushButton(tr("E&xport..."))), m_compare(new QPushButton(tr("Co&mpare..."))), m_clear(new QPushButton(tr("C&lear"))),
      m_play(new QPushButton), m_analyse(new QPushButton(tr("&Why Was It Slow?"))), m_trigger(new QSpinBox), m_status(new QLabel), m_values(new InfoGrid), m_report(new QPlainTextEdit), m_playTimer(new QTimer(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* buttons = new QHBoxLayout;
    for (QPushButton* button : {m_record, m_save, m_open, m_export, m_compare, m_clear})
        buttons->addWidget(button);
    buttons->addStretch(1);
    auto* triggerLabel = new QLabel(tr("Save when CPU passes:"));
    triggerLabel->setBuddy(m_trigger);
    m_trigger->setRange(0, 100);
    m_trigger->setSuffix(tr("%"));
    m_trigger->setSpecialValueText(tr("Off"));
    m_trigger->setValue(m_recorder.triggerPercent());
    m_trigger->setAccessibleName(tr("Save automatically when CPU use passes this level"));
    buttons->addWidget(triggerLabel);
    buttons->addWidget(m_trigger);
    layout->addLayout(buttons);

    auto* autoStart = new QCheckBox(tr("&Begin recording when the app starts (reads all sensors and processes until stopped, even with the window hidden)"));
    autoStart->setChecked(Settings::instance().value("flightRecorder/autoStart", false).toBool());
    connect(autoStart, &QCheckBox::toggled, this, [](bool on) { Settings::instance().setValue("flightRecorder/autoStart", on); });
    layout->addWidget(autoStart);

    auto* grid = new QGridLayout;
    grid->setSpacing(2);
    int index = 0;
    for (const Panel& panel : kPanels) {
        auto* strip = new TraceStrip(panel, this);
        strip->onScrub = [this](int frame) { setCursor(frame); };
        grid->addWidget(strip, index / 2, index % 2);
        m_strips << strip;
        ++index;
    }
    auto* top = new QWidget;
    top->setLayout(grid);

    auto* transport = new QHBoxLayout;
    m_scrub->setAccessibleName(tr("Position in the recording"));
    m_scrub->setEnabled(false);
    transport->addWidget(m_play);
    transport->addWidget(m_scrub, 1);
    transport->addWidget(m_analyse);

    auto* lower = new QWidget;
    auto* lowerLayout = new QHBoxLayout(lower);
    lowerLayout->setContentsMargins(0, 0, 0, 0);
    lowerLayout->addWidget(m_values, 1);
    m_report->setReadOnly(true);
    m_report->setAccessibleName(tr("Analysis"));
    m_report->setPlaceholderText(tr("Choose a moment in the recording, then ask why it was slow. The report covers about a minute around it."));
    lowerLayout->addWidget(m_report, 1);

    auto* split = new QSplitter(Qt::Vertical);
    split->addWidget(top);
    split->addWidget(lower);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);

    layout->addWidget(split, 1);
    layout->addLayout(transport);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    m_playTimer->setInterval(kReplayMs);
    connect(m_playTimer, &QTimer::timeout, this, [this] {
        if (m_cursor + 1 >= static_cast<int>(shownFrames().size())) {
            togglePlay();
            return;
        }
        setCursor(m_cursor + 1);
    });
    connect(m_record, &QPushButton::clicked, this, &FlightRecorderView::toggleRecording);
    connect(m_save, &QPushButton::clicked, this, &FlightRecorderView::saveFile);
    connect(m_open, &QPushButton::clicked, this, &FlightRecorderView::openFile);
    connect(m_export, &QPushButton::clicked, this, &FlightRecorderView::exportFile);
    connect(m_compare, &QPushButton::clicked, this, &FlightRecorderView::compareFile);
    connect(m_clear, &QPushButton::clicked, this, &FlightRecorderView::clearRecording);
    connect(m_play, &QPushButton::clicked, this, &FlightRecorderView::togglePlay);
    connect(m_analyse, &QPushButton::clicked, this, &FlightRecorderView::analyse);
    connect(m_scrub, &QSlider::valueChanged, this, &FlightRecorderView::setCursor);
    connect(m_trigger, &QSpinBox::valueChanged, this, [this](int percent) { m_recorder.setTriggerPercent(percent); });
    connect(&m_recorder, &FlightRecorder::frameAdded, this, [this] {
        if (!m_showingFile && isVisible())
            showFrames();
    });
    connect(&m_recorder, &FlightRecorder::triggered, this, [this](const QString& path, const QString& error) {
        m_status->setText(error.isEmpty() ? tr("CPU passed the level, so the recording was saved to %1").arg(path) : tr("Could not save the recording: %1").arg(error));
    });

    updateButtons();
    showFrames();
}

const std::deque<TraceFrame>& FlightRecorderView::shownFrames() const
{
    return m_showingFile ? m_opened : m_recorder.buffer().frames();
}

void FlightRecorderView::opened()
{
    showFrames();
    updateButtons();
}

void FlightRecorderView::closed()
{
    if (m_playTimer->isActive())
        togglePlay();
    // An opened file can be large and is easy to open again. A recording keeps going.
    if (m_showingFile) {
        m_showingFile = false;
        m_opened.clear();
        m_cursor = -1;
    }
}

void FlightRecorderView::updateButtons()
{
    const bool recording = m_recorder.isRecording();
    const bool haveFrames = !shownFrames().empty();
    m_record->setText(recording ? tr("Stop &Recording") : tr("&Record"));
    m_save->setEnabled(haveFrames);
    m_export->setEnabled(haveFrames);
    m_compare->setEnabled(haveFrames);
    m_clear->setEnabled(haveFrames);
    m_analyse->setEnabled(haveFrames);
    m_play->setEnabled(haveFrames);
    m_play->setText(m_playTimer->isActive() ? tr("&Pause") : tr("&Play"));
}

void FlightRecorderView::toggleRecording()
{
    if (m_recorder.isRecording()) {
        m_recorder.stop();
        m_status->setText(tr("Stopped."));
    } else {
        m_showingFile = false;
        m_opened.clear();
        m_cursor = -1;
        m_recorder.start();
        // With updates paused there are no samples, so nothing gets recorded either.
        m_status->setText(Settings::instance().updateMs() <= 0 ? tr("Recording, but updates are paused, so nothing is captured until you turn them back on in the View menu.")
                                                               : tr("Recording. It keeps going on other pages and while the window is hidden. All sensors and processes are read on each update until you stop it."));
    }
    showFrames();
    updateButtons();
}

void FlightRecorderView::clearRecording()
{
    if (!m_showingFile && !m_recorder.buffer().frames().empty() && !confirmed(this, tr("Clear"), tr("Clear this recording?\n\nWhat hasn't been saved will be lost.")))
        return;
    if (m_showingFile) {
        m_showingFile = false;
        m_opened.clear();
    } else {
        m_recorder.clear();
    }
    m_cursor = -1;
    m_report->clear();
    showFrames();
    updateButtons();
}

void FlightRecorderView::showFrames()
{
    const auto& frames = shownFrames();
    for (TraceStrip* strip : std::as_const(m_strips))
        strip->setFrames(&frames);

    const int last = static_cast<int>(frames.size()) - 1;
    {
        const QSignalBlocker block(m_scrub);
        m_scrub->setEnabled(last > 0);
        m_scrub->setRange(0, std::max(0, last));
    }
    if (!m_showingFile && m_recorder.isRecording())
        m_cursor = last; // following the live edge
    else if (m_cursor > last)
        m_cursor = last;
    else if (m_cursor < 0 && last >= 0)
        m_cursor = 0;
    {
        const QSignalBlocker block(m_scrub);
        m_scrub->setValue(std::max(0, m_cursor));
    }
    showAtCursor();
    if (!m_showingFile)
        updateButtons();
}

void FlightRecorderView::setCursor(int index)
{
    m_cursor = index;
    const QSignalBlocker block(m_scrub);
    m_scrub->setValue(index);
    showAtCursor();
}

void FlightRecorderView::showAtCursor()
{
    for (TraceStrip* strip : std::as_const(m_strips))
        strip->setCursorIndex(m_cursor);

    const auto& frames = shownFrames();
    if (m_cursor < 0 || m_cursor >= static_cast<int>(frames.size())) {
        m_values->clear();
        return;
    }
    const TraceFrame& frame = frames[static_cast<std::size_t>(m_cursor)];
    const QLocale locale;
    const auto number = [](float value, auto show) { return std::isnan(value) ? tr("Not recorded") : show(static_cast<double>(value)); };

    m_values->set(tr("Time:"), tr("%1 s of %2 s").arg(locale.toString(static_cast<double>(frame.ms - frames.front().ms) / 1000.0, 'f', 0), locale.toString(static_cast<double>(frames.back().ms - frames.front().ms) / 1000.0, 'f', 0)));
    m_values->set(tr("CPU:"), number(frame.cpuPct, [](double v) { return Format::percent(v); }));
    m_values->set(tr("Memory:"), number(frame.memoryUsedBytes, [](double v) { return Format::bytes(static_cast<std::uint64_t>(v)); }));
    m_values->set(tr("Disk read, write:"), tr("%1, %2").arg(number(frame.diskReadBps, [](double v) { return Format::diskRate(v); }), number(frame.diskWriteBps, [](double v) { return Format::diskRate(v); })));
    m_values->set(tr("Network receive, send:"), tr("%1, %2").arg(number(frame.netRxBps, [](double v) { return Format::rate(v); }), number(frame.netTxBps, [](double v) { return Format::rate(v); })));
    m_values->set(tr("GPU:"), number(frame.gpuPct, [](double v) { return Format::percent(v); }));
    m_values->set(tr("Temperature:"), number(frame.cpuTempC, [](double v) { return Format::temperature(v); }));
    m_values->set(tr("Power:"), number(frame.cpuPowerW, [](double v) { return Format::watts(v); }));
    QStringList top;
    for (const TraceProcess& process : frame.top)
        top << tr("%1 (%2)").arg(QString::fromStdString(process.name), Format::percent(static_cast<double>(process.cpuPct)));
    m_values->set(tr("Busiest:"), top.join(QStringLiteral(", ")));
}

void FlightRecorderView::togglePlay()
{
    if (m_playTimer->isActive()) {
        m_playTimer->stop();
    } else {
        if (m_cursor + 1 >= static_cast<int>(shownFrames().size()))
            setCursor(0);
        m_playTimer->start();
    }
    updateButtons();
}

void FlightRecorderView::saveFile()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save Recording"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), tr("Recordings (*.fgtrace)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QLatin1String(".fgtrace")))
        path += QStringLiteral(".fgtrace");
    std::string error;
    m_status->setText(saveTrace(path.toStdString(), shownFrames(), error) ? tr("Saved to %1").arg(path) : tr("Could not save: %1").arg(QString::fromStdString(error)));
}

void FlightRecorderView::openFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Recording"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), tr("Recordings (*.fgtrace)"));
    if (path.isEmpty())
        return;
    std::deque<TraceFrame> loaded;
    std::string error;
    if (!loadTrace(path.toStdString(), loaded, error)) {
        m_status->setText(tr("Could not open: %1").arg(QString::fromStdString(error)));
        return;
    }
    m_opened = std::move(loaded);
    m_showingFile = true;
    m_cursor = 0;
    m_report->clear();
    m_status->setText(tr("Opened %1").arg(path));
    showFrames();
    updateButtons();
}

void FlightRecorderView::exportFile()
{
    const QString csv = tr("Spreadsheet (*.csv)");
    const QString json = tr("JSON (*.json)");
    QString chosen;
    QString path = QFileDialog::getSaveFileName(this, tr("Export Recording"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), csv + QStringLiteral(";;") + json, &chosen);
    if (path.isEmpty())
        return;
    const bool wantJson = chosen == json;
    const QString suffix = wantJson ? QStringLiteral(".json") : QStringLiteral(".csv");
    if (!path.endsWith(suffix))
        path += suffix;
    std::string error;
    const bool done = wantJson ? exportJson(path.toStdString(), shownFrames(), error) : exportCsv(path.toStdString(), shownFrames(), error);
    m_status->setText(done ? tr("Exported to %1").arg(path) : tr("Could not export: %1").arg(QString::fromStdString(error)));
}

void FlightRecorderView::analyse()
{
    const auto& frames = shownFrames();
    if (frames.empty())
        return;
    const int at = std::max(0, m_cursor);
    const std::size_t first = static_cast<std::size_t>(std::max(0, at - kAnalyseFramesEachSide));
    const std::size_t last = static_cast<std::size_t>(at + kAnalyseFramesEachSide);
    const TraceAnalysis a = analyseTrace(frames, first, last);
    const QLocale locale;

    QStringList lines;
    lines << tr("Over %1 seconds around this moment:").arg(locale.toString(a.seconds, 'f', 0));
    lines << tr("Highest CPU use: %1").arg(peakPercent(a.peakCpu));
    lines << tr("Highest memory use: %1").arg(peakBytes(a.peakMemoryBytes));
    lines << tr("Highest disk traffic: %1").arg(peakDiskRate(a.peakDiskBps));
    lines << tr("Highest network traffic: %1").arg(peakRate(a.peakNetBps));
    lines << tr("Highest temperature: %1").arg(peakTemperature(a.peakTempC));
    lines << tr("Highest power: %1").arg(peakWatts(a.peakPowerW));
    lines << tr("Highest pressure: %1").arg(peakPercent(a.peakPressure));
    if (a.gaps > 0)
        lines << tr("%n sample(s) are missing in this stretch.", nullptr, static_cast<int>(a.gaps));
    lines << QString() << tr("Biggest users of the CPU:");
    for (const TraceContributor& c : a.contributors)
        lines << tr("  %1: %2 on average, up to %3 of memory").arg(QString::fromStdString(c.name), Format::percent(c.averageCpuPct), Format::bytes(static_cast<std::uint64_t>(c.peakRssBytes)));
    m_report->setPlainText(lines.join(QLatin1Char('\n')));
}

void FlightRecorderView::compareFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Compare With Recording"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), tr("Recordings (*.fgtrace)"));
    if (path.isEmpty())
        return;
    std::deque<TraceFrame> other;
    std::string error;
    if (!loadTrace(path.toStdString(), other, error)) {
        m_status->setText(tr("Could not open: %1").arg(QString::fromStdString(error)));
        return;
    }
    const auto& frames = shownFrames();
    const TraceAnalysis a = analyseTrace(frames, 0, frames.size());
    const TraceAnalysis b = analyseTrace(other, 0, other.size());
    const QLocale locale;
    const auto row = [](const QString& what, const QString& x, const QString& y) { return tr("%1: this one %2, the other %3").arg(what, x, y); };

    QStringList lines;
    lines << row(tr("Length"), tr("%1 s").arg(locale.toString(a.seconds, 'f', 0)), tr("%1 s").arg(locale.toString(b.seconds, 'f', 0)));
    lines << row(tr("Highest CPU use"), peakPercent(a.peakCpu), peakPercent(b.peakCpu));
    lines << row(tr("Highest memory use"), peakBytes(a.peakMemoryBytes), peakBytes(b.peakMemoryBytes));
    lines << row(tr("Highest disk traffic"), peakDiskRate(a.peakDiskBps), peakDiskRate(b.peakDiskBps));
    lines << row(tr("Highest network traffic"), peakRate(a.peakNetBps), peakRate(b.peakNetBps));
    lines << row(tr("Highest temperature"), peakTemperature(a.peakTempC), peakTemperature(b.peakTempC));
    lines << row(tr("Highest power"), peakWatts(a.peakPowerW), peakWatts(b.peakPowerW));
    m_report->setPlainText(lines.join(QLatin1Char('\n')));
}

} // namespace fg
