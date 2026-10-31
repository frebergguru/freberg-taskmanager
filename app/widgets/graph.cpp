#include "graph.h"

#include "../style/style.h"

#include <QAccessible>
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <qdrawutil.h>

namespace fg {

namespace {
constexpr int kGridStep = 12;
constexpr int kPixelsPerSample = 2;
}

Graph::Graph(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void Graph::addSeries(const History* history, bool secondary)
{
    m_series.append({history, secondary});
}

void Graph::clearSeries()
{
    m_series.clear();
}

void Graph::sampled(const QString& description)
{
    m_scroll = (m_scroll + kPixelsPerSample) % kGridStep;
    if (QAccessible::isActive())
        setAccessibleDescription(description);
    else
        setToolTip(description);
    if (isVisible())
        update();
}

void Graph::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const Scheme& scheme = static_cast<const Style*>(QApplication::style())->scheme();
    const QBrush back(scheme.graphBack);
    sunkenPanel(&p, rect(), palette(), &back);
    const QRect area = rect().adjusted(2, 2, -2, -2);
    p.setClipRect(area);

    p.setPen(scheme.graphGrid);
    for (int x = area.right() - m_scroll; x >= area.left(); x -= kGridStep)
        p.drawLine(x, area.top(), x, area.bottom());
    for (int y = area.bottom(); y >= area.top(); y -= kGridStep)
        p.drawLine(area.left(), y, area.right(), y);

    double top = m_fixedMax;
    const int visible = area.width() / kPixelsPerSample + 2;
    if (top <= 0) {
        for (const Series& series : std::as_const(m_series)) {
            const std::size_t size = series.history->size();
            const std::size_t count = std::min<std::size_t>(size, static_cast<std::size_t>(visible));
            for (std::size_t i = size - count; i < size; ++i) {
                const float value = series.history->at(i);
                if (!std::isnan(value))
                    top = std::max(top, static_cast<double>(value));
            }
        }
        top = top > 0 ? top * 1.15 : 1.0;
    }

    const int height = area.height() - 1;
    for (const Series& series : std::as_const(m_series)) {
        const std::size_t size = series.history->size();
        const std::size_t count = std::min<std::size_t>(size, static_cast<std::size_t>(visible));
        QPainterPath line;
        bool havePrevious = false;
        for (std::size_t back = 0; back < count; ++back) {
            const float value = series.history->at(size - 1 - back);
            if (std::isnan(value)) {
                havePrevious = false; // a gap is drawn as a gap
                continue;
            }
            const double fraction = std::min(1.0, static_cast<double>(value) / top);
            const QPointF point(area.right() - static_cast<double>(back) * kPixelsPerSample, area.bottom() - fraction * height);
            if (havePrevious)
                line.lineTo(point);
            else
                line.moveTo(point);
            havePrevious = true;
        }
        strokeSeries(p, line, series.secondary ? scheme.graphLine2 : scheme.graphLine, area.bottom(), series.secondary);
    }
}

} // namespace fg
