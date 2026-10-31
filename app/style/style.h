// Draws every standard control. The classic design has square corners and two-pixel
// bevels, with no gradients except the title bar and no animation. The flat designs
// are in style_modern.cpp.
#pragma once

#include "scheme.h"

#include <QCommonStyle>

class QPainterPath;

namespace fg {

class Style final : public QCommonStyle {
    Q_OBJECT
public:
    explicit Style(const Scheme& scheme, double scale = 1.0);

    void setScheme(const Scheme& scheme) { m_scheme = scheme; }
    const Scheme& scheme() const { return m_scheme; }
    void setScale(double scale) { m_scale = scale; }
    double scale() const { return m_scale; }

    QPalette standardPalette() const override { return paletteFor(m_scheme); }
    void polish(QWidget* widget) override;
    using QCommonStyle::polish;

    int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr, const QWidget* widget = nullptr) const override;
    int styleHint(StyleHint hint, const QStyleOption* option = nullptr, const QWidget* widget = nullptr, QStyleHintReturn* result = nullptr) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& size, const QWidget* widget) const override;

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget = nullptr) const override;
    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget = nullptr) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget = nullptr) const override;

private:
    int scaled(int value) const { return qMax(1, qRound(value * m_scale)); }
    bool isClassic() const { return m_scheme.design == Design::Classic; }
    bool drawModernPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    bool drawModernControl(ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    bool drawModernComplex(ComplexControl control, const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const;

    Scheme m_scheme;
    double m_scale;
};

const Scheme& currentScheme();

// A raised or sunken panel in the current design, for hand-drawn widgets.
void raisedPanel(QPainter* painter, const QRect& rect, const QPalette& palette, bool down, const QBrush* fill);
void sunkenPanel(QPainter* painter, const QRect& rect, const QPalette& palette, const QBrush* fill);

// Soft light around a shape. It draws nothing unless the theme is Neon Glow.
void glowStroke(QPainter& painter, const QPainterPath& path, const QColor& color, double radius);

// One graph line in the current design: plain in Classic, with a soft fill below in
// Modern, and glowing in Neon Glow. baseline is the y coordinate of the graph's floor.
// dashed marks a graph's second line, so its two lines differ by more than colour.
void strokeSeries(QPainter& painter, const QPainterPath& line, const QColor& color, double baseline, bool dashed = false);

} // namespace fg
