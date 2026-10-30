#include "popout.h"

#include "../format.h"
#include "../settings.h"

#include <QAccessible>
#include <QActionGroup>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QUrl>
#include <QWindow>
#include <cmath>

namespace fg {

namespace {

constexpr int kBaseWidth = 220;

void fillSurface(QPainter& p, const QRect& area, const SkinSurface& surface, int radius = 0)
{
    QBrush brush(surface.top);
    QLinearGradient gradient(area.topLeft(), area.bottomLeft());
    if (surface.top != surface.bottom) {
        gradient.setColorAt(0, surface.top);
        gradient.setColorAt(1, surface.bottom);
        brush = QBrush(gradient);
    }
    if (radius > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(brush);
        p.drawRoundedRect(area, radius, radius);
    } else {
        p.fillRect(area, brush);
    }
}

void drawImage(QPainter& p, const QRect& area, const SkinImage& image)
{
    if (image.image.isNull())
        return;
    p.save();
    p.setOpacity(image.opacity);
    p.setClipRect(area);
    switch (image.fit) {
    case SkinImage::Stretch:
        p.drawImage(area, image.image);
        break;
    case SkinImage::Tile:
        p.drawTiledPixmap(area, QPixmap::fromImage(image.image));
        break;
    case SkinImage::Fit: {
        const QSize size = image.image.size().scaled(area.size(), Qt::KeepAspectRatio);
        p.drawImage(QRect(area.center() - QPoint(size.width() / 2, size.height() / 2), size), image.image);
        break;
    }
    case SkinImage::Fill: {
        const QSize size = image.image.size().scaled(area.size(), Qt::KeepAspectRatioByExpanding);
        p.drawImage(QRect(area.center() - QPoint(size.width() / 2, size.height() / 2), size), image.image);
        break;
    }
    }
    p.restore();
}

double fractionOf(const Reading<double>& reading, double top)
{
    return reading.known() ? std::clamp(reading.value / top, 0.0, 1.0) : -1.0;
}

} // namespace

Popout::Popout(SamplerHub& hub) : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint), m_hub(hub)
{
    setWindowTitle(tr("Freberg.guru - Task manager readout"));
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("System readout"));
    connect(&m_hub, &SamplerHub::sampled, this, &Popout::onSampled);
    setPinned(Settings::instance().value("popout/pinned", false).toBool());
    reloadSkins();
}

Popout::~Popout()
{
    m_hub.clearWanted(this);
}

void Popout::reloadSkins()
{
    QDir().mkpath(skinsFolder());
    m_library = loadSkins(skinsFolder());
    const QString wanted = Settings::instance().value("popout/skin", QStringLiteral("default")).toString();
    for (const Skin& skin : std::as_const(m_library.skins))
        if (skin.id == wanted) {
            applySkin(skin);
            return;
        }
    // The chosen skin is gone, so fall back without forgetting the choice.
    applySkin(m_library.skins.first());
}

void Popout::applySkin(const Skin& skin)
{
    m_skin = skin;
    m_channels.clear();
    for (const QString& key : skin.quadrants) {
        Channel& channel = m_channels.emplace_back();
        channel.key = key;
        channel.label = key == QLatin1String("cpu")       ? tr("CPU")
                      : key == QLatin1String("memory")    ? tr("Memory")
                      : key == QLatin1String("gpu")       ? tr("GPU")
                      : key == QLatin1String("thermal")   ? tr("Temp")
                      : key == QLatin1String("disk")      ? tr("Disk")
                      : key == QLatin1String("network")   ? tr("Net")
                                                          : tr("Power");
        channel.valueText = Format::why(Availability::NotYetSampled);
    }
    QFont skinFont = QApplication::font();
    skinFont.setPointSizeF(skin.fontSize);
    skinFont.setBold(skin.fontBold);
    if (!skin.fontFamily.isEmpty())
        skinFont.setFamily(skin.fontFamily);
    setFont(skinFont);
    setToolTip(skin.description);
    resize(sizeHint());
    updateShape();
    update();
    if (isVisible())
        m_hub.setWanted(this, subsystemsFor(skin.quadrants));
}

QSize Popout::sizeHint() const
{
    const int line = fontMetrics().height();
    const int count = static_cast<int>(m_channels.size());
    const int header = m_skin.hideTitle ? 0 : line + 6;
    const int outer = 2 * (m_skin.padding + m_skin.border + m_skin.frameInset);
    switch (m_skin.meterStyle) {
    case Skin::Rows:
        return QSize(kBaseWidth + outer, header + outer + count * (line + 8 + m_skin.gap));
    case Skin::Vertical:
        return QSize(std::max(kBaseWidth, count * (fontMetrics().horizontalAdvance(tr("Memory")) + 14 + m_skin.gap)) + outer, header + outer + 9 * fontMetrics().height());
    default: {
        const int columns = count <= 2 ? count : 2;
        const int rows = (count + columns - 1) / std::max(1, columns);
        return QSize(kBaseWidth + outer, header + outer + rows * (5 * fontMetrics().height() + m_skin.gap));
    }
    }
}

void Popout::setPinned(bool pinned)
{
    m_pinned = pinned;
    Settings::instance().setValue("popout/pinned", pinned);
    const bool wasVisible = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, pinned);
    if (wasVisible)
        show();
}

Subsystems Popout::subsystemsFor(const QStringList& channels) const
{
    Subsystems wanted = SubNone;
    for (const QString& key : channels) {
        if (key == QLatin1String("cpu"))
            wanted |= SubCpu;
        else if (key == QLatin1String("memory"))
            wanted |= SubMemory;
        else if (key == QLatin1String("gpu"))
            wanted |= SubGpu;
        else if (key == QLatin1String("thermal"))
            wanted |= SubThermal;
        else if (key == QLatin1String("disk"))
            wanted |= SubDisk;
        else if (key == QLatin1String("network"))
            wanted |= SubNetwork;
        else if (key == QLatin1String("power"))
            wanted |= SubPower;
    }
    return wanted;
}

void Popout::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // Nothing was measured while hidden, so that stretch stays a gap.
    for (Channel& channel : m_channels)
        if (channel.history.size() > 0)
            channel.history.push(History::gap());
    m_hub.setWanted(this, subsystemsFor(m_skin.quadrants));
    emit visibilityChanged(true);
}

void Popout::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_hub.clearWanted(this);
    emit visibilityChanged(false);
}

void Popout::onSampled(const SnapshotPtr& snapshot)
{
    if (!isVisible())
        return;
    const Subsystems got = snapshot->collected;
    for (Channel& channel : m_channels) {
        if (channel.key == QLatin1String("cpu") && (got & SubCpu)) {
            channel.fraction = fractionOf(snapshot->cpu.usagePct, 100);
            channel.valueText = Format::percent(snapshot->cpu.usagePct);
            channel.history.push(snapshot->cpu.usagePct);
        } else if (channel.key == QLatin1String("memory") && (got & SubMemory)) {
            const MemorySample& m = snapshot->memory;
            const bool known = m.used.known() && m.total.known() && m.total.value > 0;
            const auto share = known ? Reading<double>::measured(100.0 * static_cast<double>(m.used.value) / static_cast<double>(m.total.value)) : Reading<double>::missing(m.used.state);
            channel.fraction = fractionOf(share, 100);
            channel.valueText = Format::percent(share);
            channel.history.push(share);
        } else if (channel.key == QLatin1String("gpu") && (got & SubGpu)) {
            const auto busy = snapshot->gpus.empty() ? Reading<double>::missing(Availability::Unsupported) : snapshot->gpus.front().busyPct;
            channel.fraction = fractionOf(busy, 100);
            channel.valueText = Format::percent(busy);
            channel.history.push(busy);
        } else if (channel.key == QLatin1String("thermal") && (got & SubThermal)) {
            channel.fraction = fractionOf(snapshot->thermal.cpuHotspotC, 110);
            channel.valueText = Format::temperature(snapshot->thermal.cpuHotspotC);
            channel.history.push(snapshot->thermal.cpuHotspotC);
        } else if (channel.key == QLatin1String("disk") && (got & SubDisk)) {
            double active = -1;
            for (const DiskSample& disk : snapshot->disks)
                if (disk.activePct.known())
                    active = std::max(active, disk.activePct.value);
            const auto reading = active >= 0 ? Reading<double>::measured(active) : Reading<double>::missing(Availability::NotYetSampled);
            channel.fraction = fractionOf(reading, 100);
            channel.valueText = Format::percent(reading);
            channel.history.push(reading);
        } else if (channel.key == QLatin1String("network") && (got & SubNetwork)) {
            double total = -1;
            for (const NetworkSample& net : snapshot->networks)
                if (countsTowardTotal(net, snapshot->networks) && net.rxBps.known() && net.txBps.known())
                    total = std::max(total, 0.0) + net.rxBps.value + net.txBps.value;
            const auto reading = total >= 0 ? Reading<double>::measured(total) : Reading<double>::missing(Availability::NotYetSampled);
            // No fixed top exists for traffic, so the scale is logarithmic up to 1 GB/s.
            channel.fraction = reading.known() ? std::clamp(std::log10(1.0 + reading.value) / 9.0, 0.0, 1.0) : -1.0;
            channel.valueText = Format::rate(reading);
            channel.history.push(reading.known() ? Reading<double>::measured(channel.fraction * 100.0) : reading);
        } else if (channel.key == QLatin1String("power") && (got & SubPower)) {
            channel.fraction = fractionOf(snapshot->power.cpuPackageW, 65);
            channel.valueText = Format::watts(snapshot->power.cpuPackageW);
            channel.history.push(snapshot->power.cpuPackageW);
        }
    }
    update();
    if (QAccessible::isActive())
        setAccessibleDescription(describe());
}

QString Popout::describe() const
{
    QStringList parts;
    for (const Channel& channel : m_channels)
        parts << tr("%1 %2").arg(channel.label, channel.valueText);
    return parts.join(QStringLiteral(", "));
}

QColor Popout::accentFor(const QString& key) const
{
    return m_skin.accent.value(key, m_skin.ink);
}

void Popout::updateShape()
{
    const QRect all = rect();
    QPainterPath path;
    switch (m_skin.shape) {
    case Skin::Rectangle:
        clearMask();
        return;
    case Skin::Circle: {
        const int side = std::min(all.width(), all.height());
        path.addEllipse(QRectF(all.center().x() - side / 2.0, all.center().y() - side / 2.0, side, side));
        break;
    }
    case Skin::Ellipse:
        path.addEllipse(all);
        break;
    case Skin::Capsule: {
        const double radius = std::min(all.width(), all.height()) / 2.0;
        path.addRoundedRect(all, radius, radius);
        break;
    }
    case Skin::Polygon: {
        QPolygonF points;
        for (const QPointF& point : m_skin.polygon)
            points << QPointF(point.x() * all.width(), point.y() * all.height());
        path.addPolygon(points);
        break;
    }
    case Skin::Mask:
        setMask(QPixmap::fromImage(m_skin.maskImage.scaled(all.size())).mask());
        return;
    }
    setMask(QRegion(path.toFillPolygon().toPolygon()));
}

void Popout::resizeEvent(QResizeEvent*)
{
    updateShape();
}

void Popout::paintChannel(QPainter& p, const QRect& area, const Channel& channel) const
{
    const QColor lit = accentFor(channel.key);
    const QColor unlit = m_skin.unlit;
    const int line = fontMetrics().height();
    const bool known = channel.fraction >= 0;
    const int segments = std::max(1, m_skin.segments);

    fillSurface(p, area, m_skin.tile);
    if (!m_skin.flat) {
        p.setPen(m_skin.outline);
        p.setBrush(Qt::NoBrush);
        p.drawRect(area.adjusted(0, 0, -1, -1));
    }
    const QRect in = area.adjusted(3, 2, -3, -2);

    // One segment means a plain solid bar.
    const auto drawHorizontalMeter = [&](QPainter& painter, const QRect& bar) {
        if (segments == 1) {
            const QRect thin(bar.left(), bar.center().y() - 2, bar.width(), 5);
            painter.fillRect(thin, unlit);
            if (known)
                painter.fillRect(QRectF(thin.left(), thin.top(), thin.width() * channel.fraction, thin.height()), lit);
            return;
        }
        const double step = static_cast<double>(bar.width()) / segments;
        for (int i = 0; i < segments; ++i) {
            const bool on = known && (i + 0.5) / segments <= channel.fraction;
            painter.fillRect(QRectF(bar.left() + i * step, bar.top(), std::max(1.0, step - 1), bar.height()), on ? lit : unlit);
        }
    };

    const auto drawText = [&](const QRect& where) {
        p.setPen(m_skin.ink);
        p.drawText(where, Qt::AlignLeft | Qt::AlignVCenter, channel.label);
        p.setPen(known ? m_skin.ink : m_skin.inkDim);
        p.drawText(where, Qt::AlignRight | Qt::AlignVCenter, channel.valueText);
    };

    switch (m_skin.meterStyle) {
    case Skin::Rows: {
        const int labelWidth = fontMetrics().horizontalAdvance(QStringLiteral("Memory "));
        const int valueWidth = fontMetrics().horizontalAdvance(QStringLiteral("100.0 MB/s "));
        p.setPen(m_skin.ink);
        p.drawText(QRect(in.left(), in.top(), labelWidth, in.height()), Qt::AlignLeft | Qt::AlignVCenter, channel.label);
        p.setPen(known ? m_skin.ink : m_skin.inkDim);
        p.drawText(QRect(in.right() - valueWidth, in.top(), valueWidth, in.height()), Qt::AlignRight | Qt::AlignVCenter, channel.valueText);
        const QRect bar(in.left() + labelWidth, in.top() + 2, in.width() - labelWidth - valueWidth - 2, in.height() - 4);
        if (bar.width() > 2)
            drawHorizontalMeter(p, bar);
        break;
    }
    case Skin::Bars: {
        drawText(QRect(in.left(), in.top(), in.width(), line));
        const QRect bar(in.left(), in.top() + line + 2, in.width(), in.height() - line - 2);
        drawHorizontalMeter(p, bar);
        break;
    }
    case Skin::Vertical: {
        const QRect bar(in.left() + 2, in.top(), in.width() - 4, in.height() - 2 * line - 2);
        const double step = static_cast<double>(bar.height()) / segments;
        for (int i = 0; i < segments; ++i) {
            const bool on = known && (i + 0.5) / segments <= channel.fraction;
            p.fillRect(QRectF(bar.left(), bar.bottom() - (i + 1) * step + 1, bar.width(), std::max(1.0, step - 1)), on ? lit : unlit);
        }
        p.setPen(m_skin.ink);
        p.drawText(QRect(in.left(), in.bottom() - 2 * line, in.width(), line), Qt::AlignCenter, channel.valueText);
        p.drawText(QRect(in.left(), in.bottom() - line, in.width(), line), Qt::AlignCenter, channel.label);
        break;
    }
    case Skin::Gauge: {
        const int side = std::min(in.width(), in.height() - line);
        const QRect dial(in.center().x() - side / 2, in.top(), side, side);
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true); // a dial is jagged without it
        p.setBrush(Qt::NoBrush);
        const int thickness = std::max(3, side / 10);
        p.setPen(QPen(unlit, thickness, Qt::SolidLine, Qt::FlatCap));
        const QRect ring = dial.adjusted(thickness, thickness, -thickness, -thickness);
        p.drawArc(ring, -45 * 16, 270 * 16);
        if (known) {
            p.setPen(QPen(lit, thickness, Qt::SolidLine, Qt::FlatCap));
            p.drawArc(ring, 225 * 16, static_cast<int>(-270 * 16 * channel.fraction));
        }
        p.restore();
        p.setPen(known ? m_skin.ink : m_skin.inkDim);
        p.drawText(dial, Qt::AlignCenter, channel.valueText);
        p.setPen(m_skin.ink);
        p.drawText(QRect(in.left(), in.bottom() - line, in.width(), line), Qt::AlignCenter, channel.label);
        break;
    }
    case Skin::Graph: {
        drawText(QRect(in.left(), in.top(), in.width(), line));
        const QRect plot(in.left(), in.top() + line + 1, in.width(), in.height() - line - 1);
        p.setPen(unlit);
        for (int x = plot.right(); x >= plot.left(); x -= 10)
            p.drawLine(x, plot.top(), x, plot.bottom());
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(lit, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const std::size_t size = channel.history.size();
        double top = 100.0;
        if (channel.key == QLatin1String("thermal"))
            top = 110.0;
        else if (channel.key == QLatin1String("power"))
            top = 65.0;
        bool have = false;
        QPoint previous;
        for (std::size_t back = 0; back < size && static_cast<int>(back) * 2 < plot.width(); ++back) {
            const float value = channel.history.at(size - 1 - back);
            if (std::isnan(value)) {
                have = false;
                continue;
            }
            const QPoint point(plot.right() - static_cast<int>(back) * 2, plot.bottom() - static_cast<int>(std::min(1.0, static_cast<double>(value) / top) * (plot.height() - 1)));
            if (have)
                p.drawLine(previous, point);
            previous = point;
            have = true;
        }
        p.restore();
        break;
    }
    }
}

void Popout::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRect all = rect();
    fillSurface(p, all, m_skin.window, m_skin.cornerRadius);
    drawImage(p, all, m_skin.windowImage);

    if (m_skin.glow > 0) {
        // A few fading outlines give a soft edge for little cost.
        p.setBrush(Qt::NoBrush);
        QColor edge = accentFor(QStringLiteral("cpu"));
        const int rings = std::min(6, static_cast<int>(m_skin.glow));
        for (int i = 0; i < rings; ++i) {
            edge.setAlphaF(0.5 * (1.0 - static_cast<double>(i) / rings));
            p.setPen(edge);
            p.drawRoundedRect(all.adjusted(i, i, -i - 1, -i - 1), m_skin.cornerRadius, m_skin.cornerRadius);
        }
    }
    if (m_skin.border > 0) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(m_skin.outline, m_skin.border));
        const int half = m_skin.border / 2;
        p.drawRoundedRect(all.adjusted(half, half, -half - m_skin.border % 2, -half - m_skin.border % 2), m_skin.cornerRadius, m_skin.cornerRadius);
    }

    const int inset = m_skin.frameInset + m_skin.border;
    QRect body = all.adjusted(inset, inset, -inset, -inset);
    const int line = fontMetrics().height();
    if (!m_skin.hideTitle) {
        const QRect header(body.left(), body.top(), body.width(), line + 4);
        fillSurface(p, header, m_skin.header);
        drawImage(p, header, m_skin.headerImage);
        p.setPen(m_skin.ink);
        const QString title = m_skin.headerTitle.isEmpty() ? tr("System") : m_skin.headerTitle;
        p.drawText(header.adjusted(4, 0, -line - 4, 0), Qt::AlignLeft | Qt::AlignVCenter, title);
        m_closeRect = QRect(header.right() - line, header.top() + 2, line, line);
        p.drawLine(m_closeRect.topLeft() + QPoint(2, 2), m_closeRect.bottomRight() - QPoint(2, 2));
        p.drawLine(m_closeRect.topRight() + QPoint(-2, 2), m_closeRect.bottomLeft() + QPoint(2, -2));
        body.setTop(header.bottom() + 2);
    } else {
        m_closeRect = QRect();
    }
    body = body.adjusted(m_skin.padding, m_skin.padding, -m_skin.padding, -m_skin.padding);

    const int count = static_cast<int>(m_channels.size());
    if (count == 0)
        return;
    int columns = 1, rows = count;
    if (m_skin.meterStyle == Skin::Vertical) {
        columns = count;
        rows = 1;
    } else if (m_skin.meterStyle != Skin::Rows) {
        columns = count <= 2 ? count : 2;
        rows = (count + columns - 1) / columns;
    }
    const int gap = m_skin.gap;
    const int cellWidth = (body.width() - (columns - 1) * gap) / columns;
    const int cellHeight = (body.height() - (rows - 1) * gap) / rows;
    int index = 0;
    for (const Channel& channel : m_channels) {
        const QRect cell(body.left() + (index % columns) * (cellWidth + gap), body.top() + (index / columns) * (cellHeight + gap), cellWidth, cellHeight);
        paintChannel(p, cell, channel);
        ++index;
    }
}

void Popout::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (m_closeRect.contains(event->position().toPoint())) {
        hide();
        return;
    }
    if (windowHandle())
        windowHandle()->startSystemMove();
}

void Popout::contextMenuEvent(QContextMenuEvent* event)
{
    showMenu(event->globalPos());
}

void Popout::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
    } else if (event->key() == Qt::Key_Menu || (event->key() == Qt::Key_F10 && event->modifiers() == Qt::ShiftModifier)) {
        showMenu(mapToGlobal(QPoint(8, 8)));
    } else {
        QWidget::keyPressEvent(event);
    }
}

void Popout::showMenu(const QPoint& where)
{
    QMenu menu(this);
    QMenu* skins = menu.addMenu(tr("&Skin"));
    auto* group = new QActionGroup(&menu);
    for (const Skin& skin : std::as_const(m_library.skins)) {
        QAction* action = skins->addAction(skin.name);
        action->setCheckable(true);
        action->setChecked(skin.id == m_skin.id);
        action->setToolTip(skin.description);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, skin] {
            Settings::instance().setValue("popout/skin", skin.id);
            applySkin(skin);
        });
    }
    skins->addSeparator();
    skins->addAction(tr("&Reload Skins"), this, &Popout::reloadSkins);
    skins->addAction(tr("&Open Skins Folder"), this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(skinsFolder())); });
    if (!m_library.problems.isEmpty())
        skins->addAction(tr("Skin &Problems (%1)...").arg(m_library.problems.size()), this, [this] {
            QStringList lines;
            for (const SkinProblem& problem : std::as_const(m_library.problems))
                lines << tr("%1: %2").arg(problem.file, problem.reason);
            QMessageBox::information(this, tr("Skin Problems"), lines.join(QLatin1Char('\n')));
        });

    QAction* pin = menu.addAction(tr("&Keep Above Other Windows"));
    pin->setCheckable(true);
    pin->setChecked(m_pinned);
    connect(pin, &QAction::toggled, this, &Popout::setPinned);
    menu.addSeparator();
    menu.addAction(tr("&Close"), this, &QWidget::hide);
    menu.exec(where);
}

} // namespace fg
