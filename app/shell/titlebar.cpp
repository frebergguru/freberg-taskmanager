#include "titlebar.h"

#include "../style/style.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QLinearGradient>
#include <QMenu>
#include <QPainter>
#include <QWindow>
#include <qdrawutil.h>

namespace fg {

namespace {

const Scheme& currentScheme()
{
    return static_cast<const Style*>(QApplication::style())->scheme();
}

} // namespace

CaptionButton::CaptionButton(Kind kind, QWidget* parent) : QAbstractButton(parent), m_kind(kind)
{
    setFocusPolicy(Qt::NoFocus); // Alt+Space opens the same commands from the keyboard
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
    return QSize(height + 2, height);
}

void CaptionButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
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
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(ink, 2));
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
    if (active) {
        QLinearGradient gradient(0, 0, width(), 0);
        gradient.setColorAt(0, scheme.titleStart);
        gradient.setColorAt(1, scheme.titleEnd);
        p.fillRect(rect(), gradient);
    } else {
        p.fillRect(rect(), scheme.titleInactive);
    }

    int x = 3;
    const QIcon icon = m_window->windowIcon();
    if (!icon.isNull()) {
        const int size = height() - 4;
        icon.paint(&p, QRect(x, 2, size, size));
        x += size + 3;
    }
    QFont bold = font();
    bold.setBold(true);
    p.setFont(bold);
    p.setPen(scheme.titleText);
    const QRect text(x, 0, m_minimize->x() - x - 4, height());
    p.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(bold).elidedText(m_window->windowTitle(), Qt::ElideRight, text.width()));
}

void TitleBar::resizeEvent(QResizeEvent*)
{
    const QSize button = m_close->sizeHint();
    const int y = (height() - button.height()) / 2;
    int x = width() - button.width() - 2;
    m_close->setGeometry(x, y, button.width(), button.height());
    x -= button.width() + 2;
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
    QWidget::changeEvent(event);
}

} // namespace fg
