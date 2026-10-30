#pragma once

#include "../shell/view.h"
#include "core/trace.h"

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;

namespace fg {

class FlightRecorder;
class InfoGrid;
class SamplerHub;
class TraceStrip;

class FlightRecorderView final : public View {
    Q_OBJECT
public:
    explicit FlightRecorderView(SamplerHub& hub);
    void opened() override;
    void closed() override;

private:
    const std::deque<TraceFrame>& shownFrames() const;
    void toggleRecording();
    void saveFile();
    void openFile();
    void exportFile();
    void compareFile();
    void clearRecording();
    void togglePlay();
    void setCursor(int index);
    void showFrames();
    void showAtCursor();
    void analyse();
    void updateButtons();

    FlightRecorder& m_recorder;
    std::deque<TraceFrame> m_opened; // a file opened for replay
    bool m_showingFile = false;
    int m_cursor = -1;

    QList<TraceStrip*> m_strips;
    QSlider* m_scrub;
    QPushButton* m_record;
    QPushButton* m_save;
    QPushButton* m_open;
    QPushButton* m_export;
    QPushButton* m_compare;
    QPushButton* m_clear;
    QPushButton* m_play;
    QPushButton* m_analyse;
    QSpinBox* m_trigger;
    QLabel* m_status;
    InfoGrid* m_values;
    QPlainTextEdit* m_report;
    QTimer* m_playTimer;
};

} // namespace fg
