#include "icons.h"

#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <cmath>

namespace fg::Icons {

namespace {

// Draws on request at whatever size the screen needs, so icons stay sharp at any
// scale and only the sizes really used are ever made. Shapes use a 16 by 16 grid.
class VectorIcon final : public QIconEngine {
public:
    using Draw = void (*)(QPainter&);
    explicit VectorIcon(Draw draw) : m_draw(draw) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->translate(rect.topLeft());
        painter->scale(rect.width() / 16.0, rect.height() / 16.0);
        if (mode == QIcon::Disabled)
            painter->setOpacity(0.45);
        m_draw(*painter);
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap out(size);
        out.fill(Qt::transparent);
        QPainter painter(&out);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return out;
    }

    QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override { return size; }
    QIconEngine* clone() const override { return new VectorIcon(m_draw); }

private:
    Draw m_draw;
};

const QColor kInk(0, 0, 0);
const QColor kFace(192, 192, 192);
const QColor kLight(255, 255, 255);
const QColor kShade(128, 128, 128);
const QColor kNavy(0, 0, 128);
const QColor kScreen(0, 40, 0);
const QColor kGreen(0, 230, 0);
const QColor kDarkGreen(0, 128, 0);

QPen ink(const QColor& color, double width = 0.5)
{
    return QPen(color, width, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
}

void bevelBox(QPainter& p, const QRectF& box)
{
    p.setPen(ink(kInk));
    p.setBrush(kFace);
    p.drawRect(box);
    p.setPen(ink(kLight));
    p.drawLine(QPointF(box.left() + 0.5, box.bottom() - 0.5), QPointF(box.left() + 0.5, box.top() + 0.5));
    p.drawLine(QPointF(box.left() + 0.5, box.top() + 0.5), QPointF(box.right() - 0.5, box.top() + 0.5));
    p.setPen(ink(kShade));
    p.drawLine(QPointF(box.right() - 0.5, box.top() + 0.5), QPointF(box.right() - 0.5, box.bottom() - 0.5));
    p.drawLine(QPointF(box.right() - 0.5, box.bottom() - 0.5), QPointF(box.left() + 0.5, box.bottom() - 0.5));
}

void drawApp(QPainter& p)
{
    // A desktop monitor on its stand, with a load graph on the screen.
    p.setPen(ink(kInk));
    p.setBrush(kShade);
    p.drawRect(QRectF(6, 12, 4, 1.5));
    p.setBrush(kFace);
    p.drawRect(QRectF(3.5, 13.5, 9, 1.5));
    bevelBox(p, QRectF(1, 1.5, 14, 10.5));

    p.setPen(ink(kInk));
    p.setBrush(kScreen);
    p.drawRect(QRectF(2.5, 3, 11, 7.5));
    p.setPen(ink(kDarkGreen, 0.3));
    for (double x = 5.25; x < 13; x += 2.75)
        p.drawLine(QPointF(x, 3), QPointF(x, 10.5));
    p.setPen(QPen(kGreen, 0.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const QPointF line[] = {{3, 9}, {5, 7.5}, {6.5, 8.5}, {8.5, 4.5}, {10, 6.5}, {11.5, 5.5}, {13, 7}};
    p.drawPolyline(line, 7);
}

void drawRefresh(QPainter& p)
{
    // Two green arrows chasing each other around a circle.
    const auto arc = [&p](int startDegrees, const QPointF& tip, const QPointF& a, const QPointF& b) {
        p.setPen(QPen(kDarkGreen, 2.0, Qt::SolidLine, Qt::FlatCap));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(3, 3, 10, 10), startDegrees * 16, 130 * 16);
        p.setPen(Qt::NoPen);
        p.setBrush(kDarkGreen);
        const QPointF head[] = {tip, a, b};
        p.drawPolygon(head, 3);
    };
    arc(40, QPointF(3.2, 9.8), QPointF(0.6, 6.4), QPointF(5.4, 6.4));
    arc(220, QPointF(12.8, 6.2), QPointF(10.6, 9.6), QPointF(15.4, 9.6));
}

void drawSettings(QPainter& p)
{
    // A grey gear with eight teeth.
    QPainterPath gear;
    const int teeth = 8;
    const QPointF middle(8, 8);
    for (int i = 0; i < teeth * 4; ++i) {
        const double angle = (i - 0.5) * 2.0 * M_PI / (teeth * 4);
        const double radius = (i % 4 == 0 || i % 4 == 1) ? 7.2 : 5.2;
        const QPointF point = middle + QPointF(std::cos(angle) * radius, std::sin(angle) * radius);
        if (i == 0)
            gear.moveTo(point);
        else
            gear.lineTo(point);
    }
    gear.closeSubpath();
    gear.addEllipse(middle, 2.2, 2.2);
    gear.setFillRule(Qt::OddEvenFill);
    p.setPen(ink(kInk));
    p.setBrush(kFace);
    p.drawPath(gear);
}

void drawPopout(QPainter& p)
{
    // A big window with a small readout floating above its corner.
    bevelBox(p, QRectF(0.75, 1.25, 10.5, 9));
    p.setPen(Qt::NoPen);
    p.setBrush(kNavy);
    p.drawRect(QRectF(1.75, 2.25, 8.5, 1.75));

    p.setPen(ink(kInk));
    p.setBrush(kScreen);
    p.drawRect(QRectF(7, 7.5, 8.25, 7.25));
    p.setPen(QPen(kGreen, 0.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const QPointF line[] = {{8, 12.5}, {9.5, 11}, {11, 12}, {12.5, 9.5}, {14.25, 10.5}};
    p.drawPolyline(line, 5);
}

} // namespace

QIcon app() { return QIcon(new VectorIcon(drawApp)); }
QIcon refresh() { return QIcon(new VectorIcon(drawRefresh)); }
QIcon settings() { return QIcon(new VectorIcon(drawSettings)); }
QIcon popout() { return QIcon(new VectorIcon(drawPopout)); }

} // namespace fg::Icons
