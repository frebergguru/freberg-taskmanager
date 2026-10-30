#include "screensaver.h"

#include "../format.h"
#include "../style/style.h"

#include <QApplication>
#include <QKeyEvent>
#include <QPainter>
#include <cmath>

namespace fg {

namespace {

void drawHistory(QPainter& p, const QRect& area, const History& history, const QColor& line, const QColor& grid)
{
    p.setPen(grid);
    for (int x = area.right(); x >= area.left(); x -= 40)
        p.drawLine(x, area.top(), x, area.bottom());
    for (int y = area.bottom(); y >= area.top(); y -= 40)
        p.drawLine(area.left(), y, area.right(), y);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(line, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const std::size_t size = history.size();
    bool have = false;
    QPoint previous;
    for (std::size_t back = 0; back < size && static_cast<int>(back) * 4 < area.width(); ++back) {
        const float value = history.at(size - 1 - back);
        if (std::isnan(value)) {
            have = false;
            continue;
        }
        const QPoint point(area.right() - static_cast<int>(back) * 4, area.bottom() - static_cast<int>(std::min(1.0, static_cast<double>(value) / 100.0) * (area.height() - 1)));
        if (have)
            p.drawLine(previous, point);
        previous = point;
        have = true;
    }
    p.setRenderHint(QPainter::Antialiasing, false);
}

} // namespace

Screensaver::Screensaver(SamplerHub& hub) : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint), m_hub(hub)
{
    setWindowTitle(tr("System load"));
    setCursor(Qt::BlankCursor);
    setAccessibleName(tr("System load. Press Escape to return."));
    setAttribute(Qt::WA_OpaquePaintEvent);
    connect(&m_hub, &SamplerHub::sampled, this, &Screensaver::onSampled);
}

Screensaver::~Screensaver()
{
    m_hub.clearWanted(this);
}

void Screensaver::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_hub.setWanted(this, SubCpu | SubMemory | SubThermal | SubPower);
    setFocus();
}

void Screensaver::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_hub.clearWanted(this);
}

void Screensaver::onSampled(const SnapshotPtr& snapshot)
{
    if (!isVisible())
        return;
    const Subsystems got = snapshot->collected;
    if (got & SubCpu) {
        m_cpu.push(snapshot->cpu.usagePct);
        m_cpuText = tr("CPU %1").arg(Format::percent(snapshot->cpu.usagePct));
    }
    if (got & SubMemory) {
        const MemorySample& m = snapshot->memory;
        const bool known = m.used.known() && m.total.known() && m.total.value > 0;
        m_memory.push(known ? 100.0f * static_cast<float>(m.used.value) / static_cast<float>(m.total.value) : History::gap());
        m_memoryText = tr("Memory %1 of %2").arg(Format::bytes(m.used), Format::bytes(m.total));
    }
    if (got & SubThermal)
        m_thermalText = tr("Temperature %1").arg(Format::temperature(snapshot->thermal.cpuHotspotC));
    if (got & SubPower)
        m_powerText = tr("Power %1").arg(Format::watts(snapshot->power.cpuPackageW));
    update();
}

void Screensaver::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const Scheme& scheme = static_cast<const Style*>(QApplication::style())->scheme();
    p.fillRect(rect(), scheme.graphBack);

    QFont big = font();
    big.setPixelSize(std::max(20, height() / 22));
    big.setBold(true);
    p.setFont(big);
    const int line = QFontMetrics(big).height();
    const int margin = line;
    const QRect upper(margin, margin + line, width() - 2 * margin, height() / 2 - 2 * margin - line);
    const QRect lower(margin, height() / 2 + margin + line, width() - 2 * margin, height() / 2 - 2 * margin - line - line);

    drawHistory(p, upper, m_cpu, scheme.graphLine, scheme.graphGrid);
    drawHistory(p, lower, m_memory, scheme.graphLine2, scheme.graphGrid);

    p.setPen(scheme.graphLine);
    p.drawText(QRect(margin, margin, width() - 2 * margin, line), Qt::AlignLeft | Qt::AlignVCenter, m_cpuText);
    p.drawText(QRect(margin, margin, width() - 2 * margin, line), Qt::AlignRight | Qt::AlignVCenter, m_thermalText);
    p.setPen(scheme.graphLine2);
    p.drawText(QRect(margin, height() / 2 + margin, width() - 2 * margin, line), Qt::AlignLeft | Qt::AlignVCenter, m_memoryText);
    p.drawText(QRect(margin, height() / 2 + margin, width() - 2 * margin, line), Qt::AlignRight | Qt::AlignVCenter, m_powerText);
}

void Screensaver::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_F12)
        hide();
    else
        QWidget::keyPressEvent(event);
}

void Screensaver::mousePressEvent(QMouseEvent*)
{
    hide();
}

} // namespace fg
