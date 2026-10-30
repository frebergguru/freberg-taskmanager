#include "style.h"

#include <QAbstractButton>
#include <QAbstractSlider>
#include <QApplication>
#include <QComboBox>
#include <QPainter>
#include <QPainterPath>
#include <QPolygon>
#include <QStyleOption>
#include <QToolButton>
#include <QWidget>
#include <qdrawutil.h>

namespace fg {

namespace {

void raised(QPainter* p, const QRect& r, const QPalette& pal, const QBrush* fill = nullptr)
{
    qDrawWinButton(p, r, pal, false, fill);
}

void sunken(QPainter* p, const QRect& r, const QPalette& pal, const QBrush* fill = nullptr)
{
    qDrawWinPanel(p, r, pal, true, fill);
}

void arrow(QPainter* p, const QRect& r, Qt::ArrowType type, const QColor& color)
{
    const int size = qMax(3, qMin(r.width(), r.height()) * 3 / 10);
    const QPoint c = r.center();
    QPolygon shape;
    switch (type) {
    case Qt::UpArrow: shape << QPoint(c.x(), c.y() - size) << QPoint(c.x() - size, c.y()) << QPoint(c.x() + size, c.y()); break;
    case Qt::DownArrow: shape << QPoint(c.x(), c.y() + size) << QPoint(c.x() - size, c.y()) << QPoint(c.x() + size, c.y()); break;
    case Qt::LeftArrow: shape << QPoint(c.x() - size, c.y()) << QPoint(c.x(), c.y() - size) << QPoint(c.x(), c.y() + size); break;
    default: shape << QPoint(c.x() + size, c.y()) << QPoint(c.x(), c.y() - size) << QPoint(c.x(), c.y() + size); break;
    }
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(Qt::NoPen);
    p->setBrush(color);
    p->drawPolygon(shape);
    p->restore();
}

} // namespace

const Scheme& currentScheme()
{
    return static_cast<const Style*>(QApplication::style())->scheme();
}

void raisedPanel(QPainter* p, const QRect& r, const QPalette& pal, bool down, const QBrush* fill)
{
    const Scheme& s = currentScheme();
    if (s.design == Design::Classic) {
        qDrawWinButton(p, r, pal, down, fill);
        return;
    }
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    if (fill)
        p->fillPath(path, *fill);
    p->setPen(QPen(s.border, 1));
    p->setBrush(Qt::NoBrush);
    p->drawPath(path);
    p->restore();
}

void sunkenPanel(QPainter* p, const QRect& r, const QPalette& pal, const QBrush* fill)
{
    if (currentScheme().design == Design::Classic)
        qDrawWinPanel(p, r, pal, true, fill);
    else
        raisedPanel(p, r, pal, false, fill);
}

void glowStroke(QPainter& p, const QPainterPath& path, const QColor& color, double radius)
{
    if (currentScheme().design != Design::Neon || radius <= 0)
        return;
    // A few wide, faint strokes stand in for a blur, at a fraction of the cost.
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Qt::NoBrush);
    QColor halo = color;
    for (const double share : {1.0, 0.6, 0.3}) {
        halo.setAlphaF(0.16);
        p.setPen(QPen(halo, radius * 2 * share, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
    }
    p.restore();
}

void strokeSeries(QPainter& p, const QPainterPath& line, const QColor& color, double baseline)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Qt::NoBrush);
    if (currentScheme().design == Design::Modern) {
        // Each unbroken stretch gets its own fill, so a gap stays empty all the way down.
        QColor fill = color;
        fill.setAlphaF(0.14);
        for (const QPolygonF& stretch : line.toSubpathPolygons()) {
            if (stretch.size() < 2)
                continue;
            QPolygonF area = stretch;
            area << QPointF(stretch.last().x(), baseline) << QPointF(stretch.first().x(), baseline);
            QPainterPath shape;
            shape.addPolygon(area);
            p.fillPath(shape, fill);
        }
    }
    glowStroke(p, line, color, 4);
    p.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(line);
    p.restore();
}

Style::Style(const Scheme& scheme, double scale) : m_scheme(scheme), m_scale(scale) {}

void Style::polish(QWidget* widget)
{
    QCommonStyle::polish(widget);
    // These react to the mouse. Each repaints only when it is entered and left.
    if (qobject_cast<QAbstractButton*>(widget) || qobject_cast<QComboBox*>(widget) || qobject_cast<QAbstractSlider*>(widget))
        widget->setAttribute(Qt::WA_Hover, true);
}

int Style::pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const
{
    if (!isClassic()) {
        switch (metric) {
        case PM_DefaultFrameWidth: return 1;
        case PM_ScrollBarExtent: return scaled(11);
        case PM_ScrollBarSliderMin: return scaled(24);
        case PM_IndicatorWidth:
        case PM_IndicatorHeight:
        case PM_ExclusiveIndicatorWidth:
        case PM_ExclusiveIndicatorHeight: return scaled(14);
        case PM_ButtonShiftHorizontal:
        case PM_ButtonShiftVertical:
        case PM_ButtonDefaultIndicator: return 0;
        case PM_MenuPanelWidth: return 1;
        case PM_MenuVMargin: return 4;
        case PM_MenuBarItemSpacing: return 2;
        case PM_MenuBarHMargin: return 4;
        case PM_ToolBarItemSpacing: return 4;
        case PM_ToolBarItemMargin: return 3;
        case PM_HeaderMargin: return scaled(6);
        case PM_SliderThickness:
        case PM_SliderLength:
        case PM_SliderControlThickness: return scaled(14);
        default: break;
        }
    }
    switch (metric) {
    case PM_DefaultFrameWidth: return 2;
    case PM_ButtonMargin: return scaled(6);
    case PM_ButtonDefaultIndicator: return 1;
    case PM_ButtonShiftHorizontal:
    case PM_ButtonShiftVertical: return 1;
    case PM_ScrollBarExtent: return scaled(16);
    case PM_ScrollBarSliderMin: return scaled(8);
    case PM_IndicatorWidth:
    case PM_IndicatorHeight: return scaled(13);
    case PM_ExclusiveIndicatorWidth:
    case PM_ExclusiveIndicatorHeight: return scaled(12);
    case PM_SmallIconSize:
    case PM_ToolBarIconSize:
    case PM_ButtonIconSize:
    case PM_ListViewIconSize:
    case PM_TabBarIconSize: return scaled(16);
    case PM_MenuBarPanelWidth:
    case PM_MenuBarItemSpacing:
    case PM_MenuBarVMargin:
    case PM_MenuBarHMargin: return 0;
    case PM_MenuPanelWidth: return 2;
    case PM_MenuHMargin:
    case PM_MenuVMargin: return 1;
    case PM_ToolBarFrameWidth:
    case PM_ToolBarItemMargin: return 1;
    case PM_ToolBarItemSpacing: return 1;
    case PM_ToolBarHandleExtent: return 0;
    case PM_ProgressBarChunkWidth: return scaled(9);
    case PM_SplitterWidth: return scaled(4);
    case PM_HeaderMargin: return scaled(4);
    case PM_FocusFrameVMargin:
    case PM_FocusFrameHMargin: return 2;
    default: return QCommonStyle::pixelMetric(metric, option, widget);
    }
}

int Style::styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* result) const
{
    switch (hint) {
    // Menus follow the pointer. A submenu opens with no click, and passing across a
    // parent menu's items on the way into a submenu doesn't close it.
    case SH_Menu_SubMenuPopupDelay: return 80;
    case SH_Menu_MouseTracking:
    case SH_MenuBar_MouseTracking:
    case SH_Menu_SloppySubMenus: return 1;
    case SH_Menu_SubMenuSloppyCloseTimeout: return 300;
    case SH_Menu_FlashTriggeredItem: return 0;
    case SH_EtchDisabledText:
    case SH_DitherDisabledText:
    case SH_Widget_Animation_Duration:
    case SH_Menu_FadeOutOnHide:
    case SH_ScrollBar_LeftClickAbsolutePosition:
    case SH_DialogButtonBox_ButtonsHaveIcons:
    case SH_Menu_Scrollable:
        return 0;
    case SH_UnderlineShortcut:
    case SH_MenuBar_AltKeyNavigation:
    case SH_Menu_SpaceActivatesItem:
    case SH_ScrollBar_ContextMenu:
        return 1;
    case SH_TabBar_Alignment: return Qt::AlignLeft;
    case SH_Table_GridLineColor: return m_scheme.dark.rgb();
    default: return QCommonStyle::styleHint(hint, option, widget, result);
    }
}

QSize Style::sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& size, const QWidget* widget) const
{
    QSize out = QCommonStyle::sizeFromContents(type, option, size, widget);
    if (!isClassic()) {
        switch (type) {
        case CT_PushButton:
            out.setWidth(qMax(out.width() + scaled(12), scaled(78)));
            out.setHeight(qMax(out.height(), scaled(26)));
            return out;
        case CT_LineEdit:
        case CT_ComboBox:
        case CT_SpinBox:
            out.setHeight(qMax(out.height(), scaled(25)));
            return out;
        case CT_MenuBarItem:
            return QSize(size.width() + scaled(16), size.height() + scaled(8));
        case CT_MenuItem:
            if (const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option)) {
                if (item->menuItemType == QStyleOptionMenuItem::Separator)
                    return QSize(size.width(), scaled(9));
                return QSize(size.width() + scaled(52), qMax(size.height(), option->fontMetrics.height()) + scaled(8));
            }
            return out;
        case CT_ItemViewItem:
            out.setHeight(qMax(out.height(), option->fontMetrics.height() + scaled(5)));
            return out;
        case CT_HeaderSection:
            out.setHeight(out.height() + scaled(4));
            return out;
        case CT_ProgressBar:
            out.setHeight(scaled(8));
            return out;
        default:
            return out;
        }
    }
    switch (type) {
    case CT_PushButton:
        out.setWidth(qMax(out.width() + scaled(8), scaled(75)));
        out.setHeight(qMax(out.height(), scaled(23)));
        return out;
    case CT_MenuBarItem:
        return QSize(size.width() + scaled(12), size.height() + scaled(4));
    case CT_MenuItem:
        if (const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option)) {
            if (item->menuItemType == QStyleOptionMenuItem::Separator)
                return QSize(size.width(), scaled(8));
            return QSize(size.width() + scaled(36), qMax(size.height(), option->fontMetrics.height()) + scaled(4));
        }
        return out;
    case CT_ItemViewItem:
        out.setHeight(qMax(out.height(), option->fontMetrics.height() + 2));
        return out;
    default:
        return out;
    }
}

void Style::drawPrimitive(PrimitiveElement element, const QStyleOption* o, QPainter* p, const QWidget* w) const
{
    if (!isClassic() && drawModernPrimitive(element, o, p, w))
        return;
    const QPalette& pal = o->palette;
    const QRect& r = o->rect;
    const bool down = o->state & (State_Sunken | State_On);

    switch (element) {
    case PE_PanelButtonCommand:
    case PE_PanelButtonBevel: {
        const QBrush fill = pal.brush(QPalette::Button);
        qDrawWinButton(p, r, pal, down, &fill);
        return;
    }
    case PE_FrameDefaultButton:
        p->setPen(pal.color(QPalette::Shadow));
        p->setBrush(Qt::NoBrush);
        p->drawRect(r.adjusted(0, 0, -1, -1));
        return;
    case PE_PanelButtonTool:
        if (down)
            qDrawShadePanel(p, r, pal, true, 1, nullptr);
        else if ((o->state & State_MouseOver) && (o->state & State_Enabled))
            qDrawShadePanel(p, r, pal, false, 1, nullptr);
        return;
    case PE_Frame:
    case PE_FrameLineEdit:
    case PE_FrameWindow:
        sunken(p, r, pal);
        return;
    case PE_PanelLineEdit: {
        const QBrush fill = pal.brush(QPalette::Base);
        sunken(p, r, pal, &fill);
        return;
    }
    case PE_FrameStatusBarItem:
        qDrawShadePanel(p, r, pal, true, 1, nullptr);
        return;
    case PE_FrameGroupBox:
        qDrawShadeRect(p, r, pal, true, 1, 0, nullptr);
        return;
    case PE_FrameTabWidget:
    case PE_FrameTabBarBase:
    case PE_FrameMenu:
    case PE_PanelMenu: {
        const QBrush fill = pal.brush(QPalette::Button);
        raised(p, r, pal, &fill);
        return;
    }
    case PE_PanelMenuBar:
    case PE_PanelToolBar:
    case PE_PanelStatusBar:
        return;
    case PE_IndicatorToolBarSeparator: {
        const QPoint c = r.center();
        p->setPen(pal.color(QPalette::Dark));
        p->drawLine(c.x(), r.top() + 1, c.x(), r.bottom() - 1);
        p->setPen(pal.color(QPalette::Light));
        p->drawLine(c.x() + 1, r.top() + 1, c.x() + 1, r.bottom() - 1);
        return;
    }
    case PE_IndicatorDockWidgetResizeHandle:
        return;
    case PE_FrameFocusRect: {
        // Thicker than the original's one pixel, so it is clearly visible.
        QColor color = pal.color(QPalette::WindowText);
        if (const auto* focus = qstyleoption_cast<const QStyleOptionFocusRect*>(o))
            if (focus->state & State_FocusAtBorder && focus->backgroundColor.isValid())
                color = pal.color(QPalette::HighlightedText);
        QPen pen(color, 1, Qt::CustomDashLine);
        pen.setDashPattern({2, 2});
        p->save();
        p->setRenderHint(QPainter::Antialiasing, false);
        p->setBrush(Qt::NoBrush);
        p->setPen(pen);
        p->drawRect(r.adjusted(0, 0, -1, -1));
        p->drawRect(r.adjusted(1, 1, -2, -2));
        p->restore();
        return;
    }
    case PE_IndicatorCheckBox:
    case PE_IndicatorItemViewItemCheck: {
        const QBrush fill = pal.brush((o->state & State_Enabled) ? QPalette::Base : QPalette::Button);
        qDrawWinPanel(p, r, pal, true, &fill);
        if (o->state & (State_On | State_NoChange)) {
            const QRect in = r.adjusted(3, 3, -3, -3);
            p->save();
            p->setRenderHint(QPainter::Antialiasing, true);
            p->setPen(QPen(pal.color(QPalette::Text), qMax(2, scaled(2))));
            const QPoint a(in.left(), in.center().y());
            const QPoint b(in.left() + in.width() / 3, in.bottom() - 1);
            const QPoint c(in.right(), in.top() + 1);
            p->drawLine(a, b);
            p->drawLine(b, c);
            p->restore();
        }
        return;
    }
    case PE_IndicatorRadioButton: {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        const QRect outer = r.adjusted(0, 0, -1, -1);
        p->setBrush(pal.brush((o->state & State_Enabled) ? QPalette::Base : QPalette::Button));
        p->setPen(Qt::NoPen);
        p->drawEllipse(outer);
        p->setBrush(Qt::NoBrush);
        p->setPen(pal.color(QPalette::Dark));
        p->drawArc(outer, 45 * 16, 180 * 16);
        p->setPen(pal.color(QPalette::Light));
        p->drawArc(outer, 225 * 16, 180 * 16);
        const QRect inner = outer.adjusted(1, 1, -1, -1);
        p->setPen(pal.color(QPalette::Shadow));
        p->drawArc(inner, 45 * 16, 180 * 16);
        p->setPen(pal.color(QPalette::Midlight));
        p->drawArc(inner, 225 * 16, 180 * 16);
        if (o->state & State_On) {
            const int inset = qMax(3, r.width() / 3);
            p->setPen(Qt::NoPen);
            p->setBrush(pal.color(QPalette::Text));
            p->drawEllipse(outer.adjusted(inset, inset, -inset, -inset));
        }
        p->restore();
        return;
    }
    case PE_IndicatorArrowUp: arrow(p, r, Qt::UpArrow, pal.color(QPalette::ButtonText)); return;
    case PE_IndicatorArrowDown: arrow(p, r, Qt::DownArrow, pal.color(QPalette::ButtonText)); return;
    case PE_IndicatorArrowLeft: arrow(p, r, Qt::LeftArrow, pal.color(QPalette::ButtonText)); return;
    case PE_IndicatorArrowRight: arrow(p, r, Qt::RightArrow, pal.color(QPalette::ButtonText)); return;
    case PE_IndicatorSpinUp: arrow(p, r, Qt::UpArrow, pal.color(QPalette::ButtonText)); return;
    case PE_IndicatorSpinDown: arrow(p, r, Qt::DownArrow, pal.color(QPalette::ButtonText)); return;
    case PE_IndicatorHeaderArrow:
        if (const auto* header = qstyleoption_cast<const QStyleOptionHeader*>(o))
            arrow(p, r, header->sortIndicator & QStyleOptionHeader::SortUp ? Qt::UpArrow : Qt::DownArrow, pal.color(QPalette::ButtonText));
        return;
    case PE_PanelTipLabel:
        p->fillRect(r, pal.brush(QPalette::ToolTipBase));
        p->setPen(pal.color(QPalette::Shadow));
        p->drawRect(r.adjusted(0, 0, -1, -1));
        return;
    default:
        QCommonStyle::drawPrimitive(element, o, p, w);
    }
}

void Style::drawControl(ControlElement element, const QStyleOption* o, QPainter* p, const QWidget* w) const
{
    if (!isClassic() && drawModernControl(element, o, p, w))
        return;
    const QPalette& pal = o->palette;
    const QRect& r = o->rect;

    switch (element) {
    case CE_HeaderSection: {
        const QBrush fill = pal.brush(QPalette::Button);
        qDrawWinButton(p, r, pal, o->state & State_Sunken, &fill);
        return;
    }
    case CE_ProgressBarGroove: {
        const QBrush fill = pal.brush(QPalette::Base);
        sunken(p, r, pal, &fill);
        return;
    }
    case CE_ProgressBarContents:
        if (const auto* bar = qstyleoption_cast<const QStyleOptionProgressBar*>(o)) {
            if (bar->maximum <= bar->minimum)
                return;
            const QRect in = r.adjusted(2, 2, -2, -2);
            const double fraction = double(bar->progress - bar->minimum) / (bar->maximum - bar->minimum);
            const int chunk = pixelMetric(PM_ProgressBarChunkWidth, o, w);
            const int gap = scaled(2);
            const int filled = qRound(in.width() * fraction);
            for (int x = 0; x < filled; x += chunk + gap)
                p->fillRect(in.left() + x, in.top(), qMin(chunk, filled - x), in.height(), pal.brush(QPalette::Highlight));
        }
        return;
    case CE_ScrollBarSlider: {
        const QBrush fill = pal.brush(QPalette::Button);
        raised(p, r, pal, &fill);
        return;
    }
    case CE_ScrollBarAddLine:
    case CE_ScrollBarSubLine: {
        const QBrush fill = pal.brush(QPalette::Button);
        const bool pressed = o->state & State_Sunken;
        qDrawWinButton(p, r, pal, pressed, &fill);
        const bool horizontal = o->state & State_Horizontal;
        const bool add = element == CE_ScrollBarAddLine;
        const Qt::ArrowType type = horizontal ? (add ? Qt::RightArrow : Qt::LeftArrow) : (add ? Qt::DownArrow : Qt::UpArrow);
        arrow(p, pressed ? r.translated(1, 1) : r, type, pal.color(QPalette::ButtonText));
        return;
    }
    case CE_ScrollBarFirst:
    case CE_ScrollBarLast:
        return;
    case CE_ScrollBarAddPage:
    case CE_ScrollBarSubPage:
        // The classic checkered trough.
        p->fillRect(r, pal.brush(QPalette::Light));
        p->fillRect(r, QBrush(pal.color(QPalette::Button), Qt::Dense4Pattern));
        return;
    case CE_MenuBarEmptyArea:
        p->fillRect(r, pal.brush(QPalette::Window));
        return;
    case CE_MenuBarItem:
        if (const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(o)) {
            const bool active = (o->state & State_Selected) && (o->state & State_Enabled);
            const bool open = o->state & State_Sunken;
            p->fillRect(r, active ? pal.brush(QPalette::Highlight) : pal.brush(QPalette::Window));
            p->setPen(pal.color(active || open ? QPalette::HighlightedText : QPalette::ButtonText));
            p->drawText(r, Qt::AlignCenter | Qt::TextShowMnemonic | Qt::TextDontClip, item->text);
        }
        return;
    case CE_MenuEmptyArea:
        p->fillRect(r, pal.brush(QPalette::Button));
        return;
    case CE_MenuItem:
        if (const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(o)) {
            if (item->menuItemType == QStyleOptionMenuItem::Separator) {
                const int y = r.center().y();
                p->setPen(pal.color(QPalette::Dark));
                p->drawLine(r.left() + 1, y, r.right() - 1, y);
                p->setPen(pal.color(QPalette::Light));
                p->drawLine(r.left() + 1, y + 1, r.right() - 1, y + 1);
                return;
            }
            const bool active = (o->state & State_Selected) && (o->state & State_Enabled);
            p->fillRect(r, pal.brush(active ? QPalette::Highlight : QPalette::Button));
            const QColor ink = pal.color(active ? QPalette::HighlightedText : QPalette::ButtonText);
            p->setPen(ink);

            const int gutter = scaled(20);
            if (item->checkType != QStyleOptionMenuItem::NotCheckable && item->checked) {
                const QPoint c(r.left() + gutter / 2, r.center().y());
                const int s = scaled(3);
                p->save();
                p->setRenderHint(QPainter::Antialiasing, true);
                if (item->checkType == QStyleOptionMenuItem::Exclusive) {
                    p->setBrush(ink);
                    p->drawEllipse(c, s - 1, s - 1);
                } else {
                    p->setPen(QPen(ink, qMax(2, scaled(2))));
                    p->drawLine(c.x() - s, c.y(), c.x() - 1, c.y() + s - 1);
                    p->drawLine(c.x() - 1, c.y() + s - 1, c.x() + s, c.y() - s);
                }
                p->restore();
            }

            const QRect text = r.adjusted(gutter, 0, -gutter, 0);
            QString label = item->text;
            const int tab = label.indexOf(QLatin1Char('\t'));
            if (tab >= 0) {
                p->drawText(text, Qt::AlignRight | Qt::AlignVCenter | Qt::TextDontClip, label.mid(tab + 1));
                label.truncate(tab);
            }
            p->drawText(text, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextShowMnemonic | Qt::TextDontClip, label);
            if (item->menuItemType == QStyleOptionMenuItem::SubMenu)
                arrow(p, QRect(r.right() - gutter, r.top(), gutter, r.height()), Qt::RightArrow, ink);
        }
        return;
    case CE_ToolBar:
        return;
    default:
        QCommonStyle::drawControl(element, o, p, w);
    }
}

void Style::drawComplexControl(ComplexControl control, const QStyleOptionComplex* o, QPainter* p, const QWidget* w) const
{
    if (!isClassic() && drawModernComplex(control, o, p, w))
        return;
    if (control == CC_ComboBox) {
        if (const auto* combo = qstyleoption_cast<const QStyleOptionComboBox*>(o)) {
            const QBrush fill = o->palette.brush(combo->state & State_Enabled ? QPalette::Base : QPalette::Button);
            sunken(p, o->rect, o->palette, &fill);
            const QRect button = subControlRect(CC_ComboBox, o, SC_ComboBoxArrow, w);
            const QBrush face = o->palette.brush(QPalette::Button);
            const bool down = (o->state & State_Sunken) && (o->activeSubControls & SC_ComboBoxArrow);
            qDrawWinButton(p, button, o->palette, down, &face);
            arrow(p, button, Qt::DownArrow, o->palette.color(QPalette::ButtonText));
            if ((o->state & State_HasFocus) && !combo->editable) {
                QStyleOptionFocusRect focus;
                focus.QStyleOption::operator=(*o);
                focus.rect = subControlRect(CC_ComboBox, o, SC_ComboBoxEditField, w);
                drawPrimitive(PE_FrameFocusRect, &focus, p, w);
            }
            return;
        }
    }
    if (control == CC_Slider) {
        if (const auto* slider = qstyleoption_cast<const QStyleOptionSlider*>(o)) {
            // A thin sunken groove and a raised block, with nothing else drawn.
            const QRect groove = subControlRect(CC_Slider, o, SC_SliderGroove, w);
            const QRect handle = subControlRect(CC_Slider, o, SC_SliderHandle, w);
            QRect track = groove;
            if (slider->orientation == Qt::Horizontal)
                track = QRect(groove.left(), groove.center().y() - 2, groove.width(), 4);
            else
                track = QRect(groove.center().x() - 2, groove.top(), 4, groove.height());
            const QBrush base = o->palette.brush(QPalette::Base);
            sunken(p, track, o->palette, &base);
            const QBrush face = o->palette.brush(QPalette::Button);
            raised(p, handle, o->palette, &face);
            if (o->state & State_HasFocus) {
                QStyleOptionFocusRect focus;
                focus.QStyleOption::operator=(*o);
                focus.rect = o->rect;
                drawPrimitive(PE_FrameFocusRect, &focus, p, w);
            }
            return;
        }
    }
    QCommonStyle::drawComplexControl(control, o, p, w);
}

} // namespace fg
