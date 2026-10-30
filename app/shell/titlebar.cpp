#include "titlebar.h"

#include "../style/style.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QLinearGradient>
#include <QMenu>
#include <QPainter>
#include <QResizeEvent>
#include <QWindow>
#include <qdrawutil.h>

namespace fg {

CaptionButton::CaptionButton(Kind kind, QWidget* parent) : QAbstractButton(parent), m_kind(kind)
{
    setFocusPolicy(Qt::NoFocus); // Alt+Space opens the same commands from the keyboard
    setAttribute(Qt::WA_Hover, true);
    setKind(kind);
}

void CaptionButton::setKind(Kind kind)
{
    m_kind = kind;
    switch (kind) {
    case Minimize: setAccessibleName(tr("Minimize")); break;
    case Maximize: setAccessibleName(tr("Maximize")); break;
    case Restore: setAccessibleName(tr("Restore")); break;
    case Close: setAccessibleName(tr("Close")); break;
    }
    setToolTip(accessibleName());
    update();
}

QSize CaptionButton::sizeHint() const
{
    const int height = fontMetrics().height() + 2;
    if (currentScheme().design != Design::Classic)
        return QSize(height * 2, height + 12);
    return QSize(height + 2, height);
}

bool CaptionButton::event(QEvent* event)
{
    if (event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverLeave)
        update();
    return QAbstractButton::event(event);
}

void CaptionButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    if (const Scheme& s = currentScheme(); s.design != Design::Classic) {
        // Flat: a thin glyph on nothing, and a soft fill under the mouse. Close turns red.
        p.setRenderHint(QPainter::Antialiasing, true);
        const bool over = underMouse() || isDown();
        const QColor closeRed(0xc4, 0x2b, 0x1c);
        if (over)
            p.fillRect(rect(), m_kind == Close ? closeRed : s.hover);
        const qreal half = height() * 0.16;
        const QPointF c = QRectF(rect()).center();
        p.setPen(QPen(over && m_kind == Close ? QColor(Qt::white) : s.text, 1.2));
        p.setBrush(Qt::NoBrush);
        switch (m_kind) {
        case Minimize:
            p.drawLine(c + QPointF(-half, 0), c + QPointF(half, 0));
            break;
        case Maximize:
            p.drawRect(QRectF(c.x() - half, c.y() - half, 2 * half, 2 * half));
            break;
        case Restore:
            p.drawRect(QRectF(c.x() - half, c.y() - half * 0.6, half * 1.6, half * 1.6));
            p.drawLine(c + QPointF(-half * 0.5, -half), c + QPointF(half, -half));
            p.drawLine(c + QPointF(half, -half), c + QPointF(half, half * 0.5));
            break;
        case Close:
            p.drawLine(c + QPointF(-half, -half), c + QPointF(half, half));
            p.drawLine(c + QPointF(half, -half), c + QPointF(-half, half));
            break;
        }
        return;
    }
    const QBrush face = palette().brush(QPalette::Button);
    qDrawWinButton(&p, rect(), palette(), isDown(), &face);

    const int shift = isDown() ? 1 : 0;
    const QRect g = rect().adjusted(4 + shift, 3 + shift, -4 + shift, -3 + shift);
    const QColor ink = palette().color(QPalette::ButtonText);
    switch (m_kind) {
    case Minimize:
        p.fillRect(g.left(), g.bottom() - 1, qMax(4, g.width() * 2 / 3), 2, ink);
        break;
    case Maximize:
        p.setPen(ink);
        p.drawRect(g.adjusted(0, 0, -1, -1));
        p.fillRect(g.left(), g.top(), g.width(), 2, ink);
        break;
    case Restore: {
        const int inset = qMax(2, g.width() / 4);
        const QRect back = g.adjusted(inset, 0, 0, -inset);
        const QRect front = g.adjusted(0, inset, -inset, 0);
        p.setPen(ink);
        p.drawRect(back.adjusted(0, 0, -1, -1));
        p.fillRect(back.left(), back.top(), back.width(), 2, ink);
        p.fillRect(front, palette().brush(QPalette::Button));
        p.drawRect(front.adjusted(0, 0, -1, -1));
        p.fillRect(front.left(), front.top(), front.width(), 2, ink);
        break;
    }
    case Close: {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::SquareCap));
        p.drawLine(g.topLeft(), g.bottomRight());
        p.drawLine(g.topRight(), g.bottomLeft());
        break;
    }
    }
}

TitleBar::TitleBar(QWidget* window)
    : QWidget(window), m_window(window), m_minimize(new CaptionButton(CaptionButton::Minimize, this)),
      m_maximize(new CaptionButton(CaptionButton::Maximize, this)), m_close(new CaptionButton(CaptionButton::Close, this))
{
    connect(m_minimize, &QAbstractButton::clicked, m_window, &QWidget::showMinimized);
    connect(m_maximize, &QAbstractButton::clicked, this, &TitleBar::toggleMaximized);
    connect(m_close, &QAbstractButton::clicked, m_window, &QWidget::close);
}

QSize TitleBar::sizeHint() const
{
    if (currentScheme().design != Design::Classic)
        return QSize(200, m_close->sizeHint().height());
    return QSize(200, fontMetrics().height() + 6);
}

void TitleBar::syncState()
{
    m_maximize->setKind(m_window->isMaximized() ? CaptionButton::Restore : CaptionButton::Maximize);
    update();
}

void TitleBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const Scheme& scheme = currentScheme();
    const bool active = m_window->isActiveWindow();
    const bool classic = scheme.design == Design::Classic;
    if (active && classic) {
        QLinearGradient gradient(0, 0, width(), 0);
        gradient.setColorAt(0, scheme.titleStart);
        gradient.setColorAt(1, scheme.titleEnd);
        p.fillRect(rect(), gradient);
    } else {
        p.fillRect(rect(), scheme.titleInactive);
    }

    int x = classic ? 3 : 8;
    const QIcon icon = m_window->windowIcon();
    if (!icon.isNull()) {
        const int size = height() - 4;
        icon.paint(&p, QRect(x, 2, size, size));
        x += size + 3;
    }
    QFont bold = font();
    bold.setBold(classic);
    p.setFont(bold);
    p.setPen(active || classic ? scheme.titleText : scheme.disabledText);
    const QRect text(x, 0, m_minimize->x() - x - 4, height());
    p.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(bold).elidedText(m_window->windowTitle(), Qt::ElideRight, text.width()));
}

void TitleBar::resizeEvent(QResizeEvent*)
{
    const QSize button = m_close->sizeHint();
    const int y = (height() - button.height()) / 2;
    const bool classic = currentScheme().design == Design::Classic;
    int x = width() - button.width() - (classic ? 2 : 0);
    m_close->setGeometry(x, y, button.width(), button.height());
    x -= button.width() + (classic ? 2 : 0);
    m_maximize->setGeometry(x, y, button.width(), button.height());
    x -= button.width();
    m_minimize->setGeometry(x, y, button.width(), button.height());
}

void TitleBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_window->windowHandle())
        m_window->windowHandle()->startSystemMove();
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        toggleMaximized();
}

void TitleBar::toggleMaximized()
{
    if (m_window->isMaximized())
        m_window->showNormal();
    else
        m_window->showMaximized();
}

void TitleBar::contextMenuEvent(QContextMenuEvent* event)
{
    showSystemMenu(event->globalPos());
}

void TitleBar::showSystemMenu(const QPoint& where)
{
    QMenu menu(this);
    QAction* restore = menu.addAction(tr("&Restore"), m_window, &QWidget::showNormal);
    restore->setEnabled(m_window->isMaximized());
    QAction* move = menu.addAction(tr("&Move"), this, [this] {
        if (m_window->windowHandle())
            m_window->windowHandle()->startSystemMove();
    });
    move->setEnabled(!m_window->isMaximized());
    menu.addAction(tr("Mi&nimize"), m_window, &QWidget::showMinimized);
    QAction* maximize = menu.addAction(tr("Ma&ximize"), m_window, &QWidget::showMaximized);
    maximize->setEnabled(!m_window->isMaximized());
    menu.addSeparator();
    QAction* close = menu.addAction(tr("&Close"), m_window, &QWidget::close);
    close->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F4));
    menu.setDefaultAction(close);
    menu.exec(where);
}

void TitleBar::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::ActivationChange)
        update();
    if (event->type() == QEvent::StyleChange) {
        updateGeometry();
        QResizeEvent same(size(), size());
        resizeEvent(&same);
    }
    QWidget::changeEvent(event);
}

} // namespace fg
