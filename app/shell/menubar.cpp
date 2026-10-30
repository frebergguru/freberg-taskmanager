#include "menubar.h"

#include "../settings.h"

#include <QApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QTimer>

namespace fg {

namespace {
constexpr int kOpenDelayMs = 120;   // long enough that passing over the bar opens nothing
constexpr int kCloseDelayMs = 400;  // forgiving of a pointer that slips off the edge
constexpr int kWatchMs = 100;
}

HoverMenuBar::HoverMenuBar(QWidget* parent) : QMenuBar(parent), m_openTimer(new QTimer(this)), m_watchTimer(new QTimer(this))
{
    setMouseTracking(true);
    m_openTimer->setSingleShot(true);
    m_openTimer->setInterval(kOpenDelayMs);
    m_watchTimer->setInterval(kWatchMs);
    connect(m_openTimer, &QTimer::timeout, this, &HoverMenuBar::openPending);
    connect(m_watchTimer, &QTimer::timeout, this, &HoverMenuBar::watchPointer);
}

void HoverMenuBar::mouseMoveEvent(QMouseEvent* event)
{
    QMenuBar::mouseMoveEvent(event); // moving along the bar with a menu open switches menus
    if (!Settings::instance().hoverMenus())
        return;
    QAction* over = actionAt(event->position().toPoint());
    if (over == m_pending)
        return;
    m_pending = over;
    m_openTimer->stop();
    if (over && over->isEnabled() && !QApplication::activePopupWidget())
        m_openTimer->start();
}

void HoverMenuBar::mousePressEvent(QMouseEvent* event)
{
    // A click means the user took over, so it can close a menu without hover reopening it.
    m_openTimer->stop();
    m_pending = actionAt(event->position().toPoint());
    stopWatching();
    QMenuBar::mousePressEvent(event);
}

void HoverMenuBar::leaveEvent(QEvent* event)
{
    m_openTimer->stop();
    if (!QApplication::activePopupWidget())
        m_pending = nullptr;
    QMenuBar::leaveEvent(event);
}

void HoverMenuBar::openPending()
{
    // Leaving the bar or moving on has already cleared or changed m_pending. The pointer's
    // global position is not asked for, as Wayland doesn't reliably give one.
    if (!m_pending || QApplication::activePopupWidget())
        return;
    setActiveAction(m_pending);
    m_outside.invalidate();
    m_leftMenus = false;
    // Enter and leave are the only sign on desktops that hide the pointer's position from
    // apps. Listening to the whole app is fine, as this lasts only while a menu is open.
    qApp->installEventFilter(this);
    m_watchTimer->start();
}

bool HoverMenuBar::eventFilter(QObject* watched, QEvent* event)
{
    if (qobject_cast<QMenu*>(watched) || watched == this) {
        if (event->type() == QEvent::Leave)
            m_leftMenus = true;
        else if (event->type() == QEvent::Enter || event->type() == QEvent::MouseMove)
            m_leftMenus = false; // movement counts too, in case a desktop sends no enter event
    }
    return QMenuBar::eventFilter(watched, event);
}

bool HoverMenuBar::pointerIsOverMenus() const
{
    const QPoint pointer = QCursor::pos();
    if (rect().contains(mapFromGlobal(pointer)))
        return true;
    const auto windows = QApplication::topLevelWidgets();
    for (const QWidget* window : windows)
        if (qobject_cast<const QMenu*>(window) && window->isVisible() && window->frameGeometry().contains(pointer))
            return true;
    return false;
}

void HoverMenuBar::watchPointer()
{
    if (!QApplication::activePopupWidget()) {
        // Closed some other way: a choice was made, or Esc was pressed.
        stopWatching();
        return;
    }
    // On Wayland only enter and leave can be trusted. Elsewhere the position is a second check.
    static const bool wayland = QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
    if (!m_leftMenus && (wayland || pointerIsOverMenus())) {
        m_outside.invalidate();
        return;
    }
    if (!m_outside.isValid()) {
        m_outside.start();
        return;
    }
    if (m_outside.elapsed() < kCloseDelayMs)
        return;
    stopWatching();
    m_pending = nullptr;
    while (QWidget* popup = QApplication::activePopupWidget())
        popup->close();
}

void HoverMenuBar::stopWatching()
{
    m_watchTimer->stop();
    qApp->removeEventFilter(this);
}

} // namespace fg
