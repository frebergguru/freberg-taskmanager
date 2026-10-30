#include "sheet.h"

#include "../style/style.h"

#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QShortcut>
#include <QStackedWidget>
#include <QStyleOption>
#include <QStylePainter>

namespace fg {

namespace {
constexpr int kLift = 2; // the chosen tab stands this much taller and joins the page
}

SheetTab::SheetTab(const QString& text, QWidget* parent) : QAbstractButton(parent)
{
    setText(text);
    setCheckable(true);
    setAutoExclusive(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(text);
    setAttribute(Qt::WA_Hover, true);
}

QSize SheetTab::sizeHint() const
{
    const QFontMetrics metrics = fontMetrics();
    if (currentScheme().design != Design::Classic)
        return QSize(metrics.horizontalAdvance(text()) + 4 * metrics.averageCharWidth() + 8, metrics.height() + 12);
    return QSize(metrics.horizontalAdvance(text()) + 2 * metrics.averageCharWidth() + 8, metrics.height() + 8);
}

// A row in the sidebar: a soft fill when chosen, with a short bar in the accent colour
// so that the choice is shown by shape as well as by colour.
void SheetTab::paintFlat(QPainter& p)
{
    const Scheme& s = currentScheme();
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF row = QRectF(rect()).adjusted(2, 1, -2, -1);
    QPainterPath path;
    path.addRoundedRect(row, 5, 5);
    if (isChecked()) {
        glowStroke(p, path, s.accent, 3);
        p.fillPath(path, s.hover);
        QPainterPath bar;
        bar.addRoundedRect(QRectF(row.left() + 3, row.top() + row.height() * 0.2, 3, row.height() * 0.6), 1.5, 1.5);
        p.fillPath(bar, s.accent);
    } else if (underMouse() && isEnabled()) {
        p.fillPath(path, s.hover);
    }
    p.setPen(isEnabled() ? s.text : s.disabledText);
    QFont chosen = font();
    chosen.setBold(isChecked());
    p.setFont(chosen);
    const int indent = 2 * fontMetrics().averageCharWidth() + 4;
    p.drawText(rect().adjusted(indent, 0, -4, 0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextShowMnemonic, text());
    if (hasFocus()) {
        QStyleOptionFocusRect focus;
        focus.initFrom(this);
        focus.rect = rect().adjusted(2, 1, -2, -1);
        style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, &p, this);
    }
}

void SheetTab::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    if (currentScheme().design != Design::Classic) {
        paintFlat(p);
        return;
    }
    const QPalette pal = palette();
    const QRect r = rect();
    const bool chosen = isChecked();

    p.fillRect(r, pal.brush(QPalette::Button));
    const int top = chosen ? 0 : kLift;
    const int bottom = r.bottom() - (chosen ? 0 : 2);
    const int left = r.left() + (chosen ? 0 : 1);
    const int right = r.right() - (chosen ? 0 : 1);

    if (!chosen)
        p.fillRect(r.left(), r.top(), r.width(), kLift, parentWidget()->palette().brush(QPalette::Window));

    p.setPen(pal.color(QPalette::Light));
    p.drawLine(left, top + 2, left, bottom);
    p.drawLine(left + 2, top, right - 2, top);
    p.drawPoint(left + 1, top + 1);
    p.setPen(pal.color(QPalette::Midlight));
    p.drawLine(left + 1, top + 2, left + 1, bottom);
    p.drawLine(left + 2, top + 1, right - 2, top + 1);
    p.setPen(pal.color(QPalette::Shadow));
    p.drawLine(right, top + 2, right, bottom);
    p.drawPoint(right - 1, top + 1);
    p.setPen(pal.color(QPalette::Dark));
    p.drawLine(right - 1, top + 2, right - 1, bottom);

    p.setPen(pal.color(isEnabled() ? QPalette::ButtonText : QPalette::PlaceholderText));
    const QRect textRect = r.adjusted(3, top, -3, -2);
    p.drawText(textRect, Qt::AlignCenter | Qt::TextShowMnemonic, text());

    if (hasFocus()) {
        QStyleOptionFocusRect focus;
        focus.initFrom(this);
        focus.rect = textRect;
        style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, &p, this);
    }
}

void SheetTab::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Up:
        emit stepRequested(-1);
        return;
    case Qt::Key_Right:
    case Qt::Key_Down:
        emit stepRequested(1);
        return;
    default:
        QAbstractButton::keyPressEvent(event);
    }
}

Sheet::Sheet(QWidget* parent) : QWidget(parent), m_stack(new QStackedWidget(this))
{
    m_stack->setFrameShape(QFrame::NoFrame);
    auto* next = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), this);
    connect(next, &QShortcut::activated, this, [this] { step(1); });
    auto* previous = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), this);
    connect(previous, &QShortcut::activated, this, [this] { step(-1); });
}

int Sheet::addPage(const QString& title, QWidget* page)
{
    const int index = m_tabs.size();
    auto* tab = new SheetTab(title, this);
    connect(tab, &QAbstractButton::clicked, this, [this, index] { setCurrentIndex(index); });
    connect(tab, &SheetTab::stepRequested, this, &Sheet::step);
    m_tabs << tab;
    m_stack->addWidget(page);
    tab->show();
    relayout();
    return index;
}

void Sheet::setPage(int index, QWidget* page)
{
    QWidget* old = m_stack->widget(index);
    m_stack->insertWidget(index, page);
    m_stack->removeWidget(old);
    old->deleteLater();
    if (index == m_current)
        m_stack->setCurrentIndex(index);
}

QWidget* Sheet::page(int index) const
{
    return m_stack->widget(index);
}

void Sheet::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_tabs.size() || index == m_current)
        return;
    const bool tabHadFocus = m_current >= 0 && m_tabs[m_current]->hasFocus();
    m_current = index;
    m_tabs[index]->setChecked(true);
    if (tabHadFocus)
        m_tabs[index]->setFocus(Qt::OtherFocusReason);
    m_stack->setCurrentIndex(index);
    relayout();
    emit currentChanged(index);
}

void Sheet::step(int direction)
{
    if (m_tabs.isEmpty())
        return;
    const int next = (m_current + direction + m_tabs.size()) % m_tabs.size();
    setCurrentIndex(next);
    m_tabs[next]->setFocus(Qt::TabFocusReason);
}

QSize Sheet::minimumSizeHint() const
{
    // The tabs and page are placed by hand, so the layout can't work this out itself.
    const int inset = 6;
    const QSize page = m_stack->minimumSizeHint();
    if (m_sidebarWidth > 0) {
        const int rows = m_tabs.isEmpty() ? 0 : m_tabs.size() * m_tabs.first()->sizeHint().height();
        return QSize(m_sidebarWidth + page.width() + 2 * inset, qMax(rows, page.height() + 2 * inset));
    }
    return QSize(page.width() + 2 * inset, m_tabsHeight + page.height() + 2 * inset);
}

void Sheet::resizeEvent(QResizeEvent*)
{
    relayout();
}

bool Sheet::event(QEvent* event)
{
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        relayout();
    return QWidget::event(event);
}

void Sheet::relayoutSidebar()
{
    int wide = 0;
    for (const SheetTab* tab : std::as_const(m_tabs))
        wide = qMax(wide, tab->sizeHint().width());
    QFont chosen = font();
    chosen.setBold(true); // the chosen row is bold, so all rows make room for that
    wide += QFontMetrics(chosen).averageCharWidth() * 2;
    int y = 0;
    for (SheetTab* tab : std::as_const(m_tabs)) {
        const int high = tab->sizeHint().height();
        tab->setGeometry(0, y, wide, high);
        tab->raise();
        y += high;
    }
    if (m_sidebarWidth != wide || m_tabsHeight != 0) {
        m_sidebarWidth = wide;
        m_tabsHeight = 0;
        updateGeometry();
    }
    const int gap = 8;
    m_stack->setGeometry(wide + gap, 2, qMax(0, width() - wide - gap - 2), qMax(0, height() - 4));
    update();
}

void Sheet::relayout()
{
    if (m_tabs.isEmpty())
        return;
    if (currentScheme().design != Design::Classic) {
        relayoutSidebar();
        return;
    }
    m_sidebarWidth = 0;

    // Fill rows in order, then bring the row with the chosen tab to the front.
    QList<QList<SheetTab*>> rows;
    rows.append(QList<SheetTab*>{});
    int used = 0;
    const int room = qMax(1, width() - 2 * kLift);
    for (SheetTab* tab : std::as_const(m_tabs)) {
        const int wide = tab->sizeHint().width();
        if (used + wide > room && !rows.last().isEmpty()) {
            rows.append(QList<SheetTab*>{});
            used = 0;
        }
        rows.last().append(tab);
        used += wide;
    }
    for (qsizetype row = 0; row < rows.size(); ++row)
        if (m_current >= 0 && rows[row].contains(m_tabs[m_current])) {
            rows.append(rows.takeAt(row));
            break;
        }

    const int rowHeight = m_tabs.first()->sizeHint().height();
    int y = 0;
    for (qsizetype row = 0; row < rows.size(); ++row) {
        // Every row is stretched to full width, as stacked tabs are.
        int total = 0;
        for (const SheetTab* tab : rows[row])
            total += tab->sizeHint().width();
        const bool stretch = rows.size() > 1;
        const int spare = stretch ? qMax(0, room - total) : 0;
        int x = kLift;
        for (qsizetype i = 0; i < rows[row].size(); ++i) {
            SheetTab* tab = rows[row][i];
            int wide = tab->sizeHint().width();
            if (stretch)
                wide += spare / rows[row].size() + (i < spare % rows[row].size() ? 1 : 0);
            const bool chosen = tab->isChecked();
            tab->setGeometry(x - (chosen ? kLift : 0), y, wide + (chosen ? 2 * kLift : 0), rowHeight + (row == rows.size() - 1 ? (chosen ? 2 : 0) : 0));
            tab->raise();
            x += wide;
        }
        y += rowHeight - kLift;
    }
    if (m_tabsHeight != y + kLift) {
        m_tabsHeight = y + kLift;
        updateGeometry();
    }

    const int inset = 6;
    m_stack->setGeometry(inset, m_tabsHeight + inset, width() - 2 * inset, height() - m_tabsHeight - 2 * inset);
    if (m_current >= 0)
        m_tabs[m_current]->raise();
    update();
}

void Sheet::paintEvent(QPaintEvent*)
{
    if (m_sidebarWidth > 0)
        return; // the rows and the page draw themselves
    QStylePainter p(this);
    QStyleOptionFrame frame;
    frame.initFrom(this);
    frame.rect = QRect(0, m_tabsHeight - 2, width(), height() - m_tabsHeight + 2);
    p.drawPrimitive(QStyle::PE_FrameTabWidget, frame);
}

} // namespace fg
