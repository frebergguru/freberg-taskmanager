// A menu bar that needs no clicks: pointing at a menu opens it, and moving away
// closes it. The timers run only around that moment, so an idle bar costs nothing.
#pragma once

#include <QElapsedTimer>
#include <QMenuBar>

class QTimer;

namespace fg {

class HoverMenuBar final : public QMenuBar {
    Q_OBJECT
public:
    explicit HoverMenuBar(QWidget* parent = nullptr);

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void openPending();
    void watchPointer();
    void stopWatching();
    bool pointerIsOverMenus() const;

    QTimer* m_openTimer;
    QTimer* m_watchTimer;
    QAction* m_pending = nullptr;
    QElapsedTimer m_outside;
    bool m_leftMenus = false;
};

} // namespace fg
