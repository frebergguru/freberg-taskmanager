// How the Modern and Neon Glow themes draw controls: flat, with rounded corners and
// thin outlines. Only Neon Glow ever adds a glow. Classic drawing lives in style.cpp.
#include "style.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>
#include <QWidget>

namespace fg {

namespace {

constexpr double kRadius = 5.0;

QRectF inner(const QRect& r)
{
    return QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5);
}

QPainterPath rounded(const QRectF& r, double radius = kRadius)
{
    QPainterPath path;
    path.addRoundedRect(r, radius, radius);
    return path;
}

void chevron(QPainter* p, const QRect& r, Qt::ArrowType type, const QColor& color)
{
    const double s = qMin(r.width(), r.height()) * 0.2;
    const QPointF c = QRectF(r).center();
    QPainterPath path;
    switch (type) {
    case Qt::UpArrow:
        path.moveTo(c + QPointF(-s, s / 2));
        path.lineTo(c + QPointF(0, -s / 2));
        path.lineTo(c + QPointF(s, s / 2));
        break;
    case Qt::DownArrow:
        path.moveTo(c + QPointF(-s, -s / 2));
        path.lineTo(c + QPointF(0, s / 2));
        path.lineTo(c + QPointF(s, -s / 2));
        break;
    case Qt::LeftArrow:
        path.moveTo(c + QPointF(s / 2, -s));
        path.lineTo(c + QPointF(-s / 2, 0));
        path.lineTo(c + QPointF(s / 2, s));
        break;
    default:
        path.moveTo(c + QPointF(-s / 2, -s));
        path.lineTo(c + QPointF(s / 2, 0));
        path.lineTo(c + QPointF(-s / 2, s));
        break;
    }
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->drawPath(path);
    p->restore();
}

} // namespace

bool Style::drawModernPrimitive(PrimitiveElement element, const QStyleOption* o, QPainter* p, const QWidget*) const
{
    const Scheme& s = m_scheme;
    const QPalette& pal = o->palette;
    const QRect& r = o->rect;
    const bool enabled = o->state & State_Enabled;
    const bool hover = enabled && (o->state & State_MouseOver);
    const bool down = o->state & (State_Sunken | State_On);
    const bool focus = o->state & State_HasFocus;

    const auto scoped = [p](auto draw) {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        draw();
        p->restore();
    };

    switch (element) {
    case PE_PanelButtonCommand:
    case PE_PanelButtonBevel:
        scoped([&] {
            const auto* button = qstyleoption_cast<const QStyleOptionButton*>(o);
            const bool isDefault = button && (button->features & QStyleOptionButton::DefaultButton);
            const QPainterPath path = rounded(inner(r));
            p->fillPath(path, down ? s.hover.darker(108) : (hover ? s.hover : s.base));
            const QColor edge = (isDefault || hover) && enabled ? s.accent : s.border;
            if (hover || focus)
                glowStroke(*p, path, s.accent, 3);
            p->setPen(QPen(edge, 1));
            p->setBrush(Qt::NoBrush);
            p->drawPath(path);
        });
        return true;
    case PE_FrameDefaultButton:
        return true;
    case PE_PanelButtonTool:
        if (down || hover)
            scoped([&] { p->fillPath(rounded(inner(r)), down ? s.hover.darker(108) : s.hover); });
        return true;
    case PE_Frame:
    case PE_FrameWindow:
    case PE_FrameLineEdit:
        scoped([&] {
            p->setPen(QPen(focus ? s.accent : s.border, 1));
            p->setBrush(Qt::NoBrush);
            p->drawPath(rounded(inner(r)));
        });
        return true;
    case PE_PanelLineEdit:
        scoped([&] {
            const QPainterPath path = rounded(inner(r));
            p->fillPath(path, pal.brush(QPalette::Base));
            if (focus)
                glowStroke(*p, path, s.accent, 3);
            p->setPen(QPen(focus ? s.accent : s.border, 1));
            p->setBrush(Qt::NoBrush);
            p->drawPath(path);
        });
        return true;
    case PE_FrameStatusBarItem:
    case PE_FrameTabWidget:
    case PE_FrameTabBarBase:
    case PE_PanelMenuBar:
    case PE_PanelToolBar:
    case PE_PanelStatusBar:
    case PE_IndicatorDockWidgetResizeHandle:
        return true;
    case PE_FrameGroupBox:
        scoped([&] {
            p->setPen(QPen(s.border, 1));
            p->setBrush(Qt::NoBrush);
            p->drawPath(rounded(inner(r), 7));
        });
        return true;
    case PE_FrameMenu:
    case PE_PanelMenu:
        scoped([&] {
            const QPainterPath path = rounded(inner(r), 6);
            p->fillPath(path, s.base);
            p->setPen(QPen(s.border, 1));
            p->setBrush(Qt::NoBrush);
            p->drawPath(path);
        });
        return true;
    case PE_IndicatorToolBarSeparator: {
        const int x = r.center().x();
        p->setPen(s.border);
        p->drawLine(x, r.top() + 3, x, r.bottom() - 3);
        return true;
    }
    case PE_FrameFocusRect:
        scoped([&] {
            const QColor color = o->state & State_FocusAtBorder ? pal.color(QPalette::HighlightedText) : s.accent;
            p->setPen(QPen(color, 2));
            p->setBrush(Qt::NoBrush);
            p->drawPath(rounded(QRectF(r).adjusted(1, 1, -1, -1), 4));
        });
        return true;
    case PE_IndicatorCheckBox:
    case PE_IndicatorItemViewItemCheck:
        scoped([&] {
            const QPainterPath path = rounded(inner(r), 3);
            const bool on = o->state & (State_On | State_NoChange);
            p->fillPath(path, on && enabled ? s.selection : s.base);
            p->setPen(QPen(on && enabled ? s.selection : (hover ? s.accent : s.shadow), 1));
            p->setBrush(Qt::NoBrush);
            p->drawPath(path);
            if (on) {
                const QRectF in = QRectF(r).adjusted(3, 3, -3, -3);
                QPainterPath tick;
                tick.moveTo(in.left(), in.center().y());
                tick.lineTo(in.left() + in.width() * 0.38, in.bottom() - 0.5);
                tick.lineTo(in.right(), in.top() + 0.5);
                p->setPen(QPen(enabled ? s.selectionText : s.disabledText, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p->drawPath(tick);
            }
        });
        return true;
    case PE_IndicatorRadioButton:
        scoped([&] {
            const QRectF outer = inner(r);
            const bool on = o->state & State_On;
            p->setBrush(s.base);
            p->setPen(QPen(on && enabled ? s.selection : (hover ? s.accent : s.shadow), on ? 1.5 : 1));
            p->drawEllipse(outer);
            if (on) {
                const double inset = outer.width() * 0.27;
                p->setPen(Qt::NoPen);
                p->setBrush(enabled ? s.selection : s.disabledText);
                p->drawEllipse(outer.adjusted(inset, inset, -inset, -inset));
            }
        });
        return true;
    case PE_IndicatorArrowUp:
    case PE_IndicatorSpinUp:
        chevron(p, r, Qt::UpArrow, pal.color(QPalette::ButtonText));
        return true;
    case PE_IndicatorArrowDown:
    case PE_IndicatorSpinDown:
        chevron(p, r, Qt::DownArrow, pal.color(QPalette::ButtonText));
        return true;
    case PE_IndicatorArrowLeft:
        chevron(p, r, Qt::LeftArrow, pal.color(QPalette::ButtonText));
        return true;
    case PE_IndicatorArrowRight:
        chevron(p, r, Qt::RightArrow, pal.color(QPalette::ButtonText));
        return true;
    case PE_IndicatorHeaderArrow:
        if (const auto* header = qstyleoption_cast<const QStyleOptionHeader*>(o))
            chevron(p, r.adjusted(-3, -3, 3, 3), header->sortIndicator & QStyleOptionHeader::SortUp ? Qt::UpArrow : Qt::DownArrow, pal.color(QPalette::ButtonText));
        return true;
    case PE_IndicatorBranch:
        // Just a chevron where a row can open, with no tree lines.
        if (o->state & State_Children)
            chevron(p, r, o->state & State_Open ? Qt::DownArrow : Qt::RightArrow, pal.color(QPalette::Text));
        return true;
    case PE_PanelTipLabel:
        scoped([&] {
            const QPainterPath path = rounded(inner(r), 4);
            p->fillPath(path, pal.brush(QPalette::ToolTipBase));
            p->setPen(QPen(s.border, 1));
            p->setBrush(Qt::NoBrush);
            p->drawPath(path);
        });
        return true;
    default:
        return false;
    }
}

bool Style::drawModernControl(ControlElement element, const QStyleOption* o, QPainter* p, const QWidget* w) const
{
    const Scheme& s = m_scheme;
    const QPalette& pal = o->palette;
    const QRect& r = o->rect;

    switch (element) {
    case CE_HeaderSection:
        p->fillRect(r, o->state & State_Sunken ? s.hover : s.face);
        p->setPen(s.border);
        p->drawLine(r.bottomLeft(), r.bottomRight());
        p->drawLine(r.right(), r.top() + 4, r.right(), r.bottom() - 4);
        return true;
    case CE_ProgressBarGroove:
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->fillPath(rounded(inner(r), qMin(r.height() / 2.0, 4.0)), s.hover);
        p->restore();
        return true;
    case CE_ProgressBarContents:
        if (const auto* bar = qstyleoption_cast<const QStyleOptionProgressBar*>(o)) {
            if (bar->maximum <= bar->minimum)
                return true;
            const double fraction = double(bar->progress - bar->minimum) / (bar->maximum - bar->minimum);
            const int filled = qRound(r.width() * fraction);
            if (filled <= 0)
                return true;
            p->save();
            p->setRenderHint(QPainter::Antialiasing, true);
            const QPainterPath path = rounded(inner(QRect(r.left(), r.top(), filled, r.height())), qMin(r.height() / 2.0, 4.0));
            glowStroke(*p, path, s.accent, 4);
            p->fillPath(path, s.selection);
            p->restore();
        }
        return true;
    case CE_ProgressBarLabel:
        return true;
    case CE_ScrollBarSlider:
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->fillPath(rounded(QRectF(r).adjusted(2, 2, -2, -2), 3), o->state & (State_MouseOver | State_Sunken) ? s.shadow : s.dark);
        p->restore();
        return true;
    case CE_ScrollBarAddLine:
    case CE_ScrollBarSubLine:
    case CE_ScrollBarAddPage:
    case CE_ScrollBarSubPage:
    case CE_ScrollBarFirst:
    case CE_ScrollBarLast:
        p->fillRect(r, s.face);
        return true;
    case CE_MenuBarEmptyArea:
    case CE_MenuEmptyArea:
    case CE_ToolBar:
        return true;
    case CE_MenuBarItem:
        if (const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(o)) {
            if ((o->state & State_Selected) && (o->state & State_Enabled)) {
                p->save();
                p->setRenderHint(QPainter::Antialiasing, true);
                p->fillPath(rounded(QRectF(r).adjusted(1, 2, -1, -2), 4), s.hover);
                p->restore();
            }
            p->setPen(pal.color(QPalette::ButtonText));
            p->drawText(r, Qt::AlignCenter | Qt::TextShowMnemonic | Qt::TextDontClip, item->text);
        }
        return true;
    case CE_MenuItem:
        if (const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(o)) {
            if (item->menuItemType == QStyleOptionMenuItem::Separator) {
                const int y = r.center().y();
                p->setPen(s.border);
                p->drawLine(r.left() + 6, y, r.right() - 6, y);
                return true;
            }
            const bool active = (o->state & State_Selected) && (o->state & State_Enabled);
            p->save();
            p->setRenderHint(QPainter::Antialiasing, true);
            if (active)
                p->fillPath(rounded(QRectF(r).adjusted(3, 1, -3, -1), 4), s.selection);
            const QColor ink = active ? s.selectionText : pal.color(QPalette::ButtonText);
            const int gutter = scaled(24);
            if (item->checkType != QStyleOptionMenuItem::NotCheckable && item->checked) {
                const QPointF c(r.left() + gutter / 2.0, r.center().y());
                const double d = scaled(4);
                if (item->checkType == QStyleOptionMenuItem::Exclusive) {
                    p->setPen(Qt::NoPen);
                    p->setBrush(ink);
                    p->drawEllipse(c, d * 0.7, d * 0.7);
                } else {
                    QPainterPath tick;
                    tick.moveTo(c + QPointF(-d, 0));
                    tick.lineTo(c + QPointF(-d * 0.25, d * 0.75));
                    tick.lineTo(c + QPointF(d, -d * 0.75));
                    p->setBrush(Qt::NoBrush);
                    p->setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    p->drawPath(tick);
                }
            }
            p->setPen(ink);
            const QRect text = r.adjusted(gutter, 0, -gutter, 0);
            QString label = item->text;
            const int tab = label.indexOf(QLatin1Char('\t'));
            if (tab >= 0) {
                p->drawText(text, Qt::AlignRight | Qt::AlignVCenter | Qt::TextDontClip, label.mid(tab + 1));
                label.truncate(tab);
            }
            p->drawText(text, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextShowMnemonic | Qt::TextDontClip, label);
            p->restore();
            if (item->menuItemType == QStyleOptionMenuItem::SubMenu)
                chevron(p, QRect(r.right() - gutter, r.top(), gutter, r.height()), Qt::RightArrow, ink);
        }
        return true;
    default:
        Q_UNUSED(w);
        return false;
    }
}

bool Style::drawModernComplex(ComplexControl control, const QStyleOptionComplex* o, QPainter* p, const QWidget* w) const
{
    const Scheme& s = m_scheme;
    if (control == CC_ComboBox) {
        if (const auto* combo = qstyleoption_cast<const QStyleOptionComboBox*>(o)) {
            p->save();
            p->setRenderHint(QPainter::Antialiasing, true);
            const bool hover = (o->state & State_Enabled) && (o->state & State_MouseOver);
            const bool focus = o->state & State_HasFocus;
            const QPainterPath path = rounded(inner(o->rect));
            p->fillPath(path, hover && !combo->editable ? s.hover : s.base);
            if (hover || focus)
                glowStroke(*p, path, s.accent, 3);
            p->setPen(QPen(focus || hover ? s.accent : s.border, 1));
            p->setBrush(Qt::NoBrush);
            p->drawPath(path);
            p->restore();
            chevron(p, subControlRect(CC_ComboBox, o, SC_ComboBoxArrow, w), Qt::DownArrow, o->palette.color(QPalette::ButtonText));
            return true;
        }
    }
    if (control == CC_Slider) {
        if (const auto* slider = qstyleoption_cast<const QStyleOptionSlider*>(o)) {
            const QRect groove = subControlRect(CC_Slider, o, SC_SliderGroove, w);
            const QRect handle = subControlRect(CC_Slider, o, SC_SliderHandle, w);
            p->save();
            p->setRenderHint(QPainter::Antialiasing, true);
            const bool flat = slider->orientation == Qt::Horizontal;
            const QRectF track = flat ? QRectF(groove.left(), groove.center().y() - 1.5, groove.width(), 3) : QRectF(groove.center().x() - 1.5, groove.top(), 3, groove.height());
            p->fillPath(rounded(track, 1.5), s.dark);
            if (o->state & State_Enabled) {
                const QRectF done = flat ? QRectF(track.left(), track.top(), handle.center().x() - track.left(), track.height()) : QRectF(track.left(), handle.center().y(), track.width(), track.bottom() - handle.center().y());
                p->fillPath(rounded(done, 1.5), s.accent);
            }
            const double side = qMin(handle.width(), handle.height());
            const QRectF knob(QRectF(handle).center() - QPointF(side / 2, side / 2), QSizeF(side, side));
            QPainterPath circle;
            circle.addEllipse(knob.adjusted(1, 1, -1, -1));
            glowStroke(*p, circle, s.accent, 4);
            p->fillPath(circle, (o->state & State_Enabled) ? s.selection : s.dark);
            if (o->state & State_HasFocus) {
                p->setPen(QPen(s.text, 1.5));
                p->setBrush(Qt::NoBrush);
                p->drawEllipse(knob);
            }
            p->restore();
            return true;
        }
    }
    return false;
}

} // namespace fg
