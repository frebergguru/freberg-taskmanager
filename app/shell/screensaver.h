// F12: the whole screen shows system load, and Esc returns. It draws only when a
// sample arrives, and has no animation of its own.
#pragma once

#include "../hub.h"
#include "core/history.h"

#include <QWidget>

namespace fg {

class Graph;

class Screensaver final : public QWidget {
    Q_OBJECT
public:
    explicit Screensaver(SamplerHub& hub);
    ~Screensaver() override;

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onSampled(const SnapshotPtr& snapshot);

    SamplerHub& m_hub;
    History m_cpu{1200}, m_memory{1200};
    QString m_cpuText, m_memoryText, m_thermalText, m_powerText;
};

} // namespace fg
