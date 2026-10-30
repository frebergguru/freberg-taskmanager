// Draws every standard control in the classic desktop look: square corners,
// two-pixel bevels, no gradients except the title bar, no antialiasing, no animation.
#pragma once

#include "scheme.h"

#include <QCommonStyle>

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

    Scheme m_scheme;
    double m_scale;
};

} // namespace fg
