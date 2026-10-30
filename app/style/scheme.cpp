#include "scheme.h"

#include <QCoreApplication>
#include <cmath>

namespace fg {

namespace {

QColor c(const char* hex) { return QColor::fromString(QLatin1String(hex)); }

// A dark scheme with one phosphor colour, like an old monitor.
Scheme phosphor(const char* id, const char* name, const char* bright, const char* mid, const char* low)
{
    Scheme s;
    s.id = QLatin1String(id);
    s.name = name;
    s.face = QColor(16, 16, 16);
    s.light = c(mid);
    s.midlight = c(low);
    s.dark = c(low).darker(170);
    s.shadow = QColor(0, 0, 0);
    s.base = QColor(0, 0, 0);
    s.text = c(bright);
    s.disabledText = c(mid);
    s.selection = c(bright);
    s.selectionText = QColor(0, 0, 0);
    s.titleStart = c(low);
    s.titleEnd = c(mid);
    s.titleText = QColor(255, 255, 255);
    s.titleInactive = QColor(40, 40, 40);
    s.graphBack = QColor(0, 0, 0);
    s.graphGrid = c(low);
    s.graphLine = c(bright);
    s.graphLine2 = c(mid);
    return s;
}

QList<Scheme> build()
{
    QList<Scheme> list;

    Scheme standard;
    standard.id = QStringLiteral("standard");
    standard.name = QT_TRANSLATE_NOOP("fg::Scheme", "Standard");
    standard.face = c("#c0c0c0");
    standard.light = c("#ffffff");
    standard.midlight = c("#dfdfdf");
    standard.dark = c("#808080");
    standard.shadow = c("#000000");
    standard.base = c("#ffffff");
    standard.text = c("#000000");
    standard.disabledText = c("#4a4a4a"); // the classic #808080 misses WCAG AA on silver
    standard.selection = c("#000080");
    standard.selectionText = c("#ffffff");
    standard.titleStart = c("#000080");
    standard.titleEnd = c("#1084d0");
    standard.titleText = c("#ffffff");
    standard.titleInactive = c("#686868");
    standard.graphBack = c("#000000");
    standard.graphGrid = c("#008040");
    standard.graphLine = c("#00ff00");
    standard.graphLine2 = c("#ffff00");
    list << standard;

    Scheme light = standard;
    light.id = QStringLiteral("light");
    light.name = QT_TRANSLATE_NOOP("fg::Scheme", "Light");
    light.face = c("#ece9d8");
    light.midlight = c("#f5f3e8");
    light.dark = c("#9d9a87");
    light.selection = c("#2f5d8c");
    light.titleStart = c("#2f5d8c");
    light.titleEnd = c("#8db4dc");
    light.disabledText = c("#55534a");
    list << light;

    Scheme dark;
    dark.id = QStringLiteral("dark");
    dark.name = QT_TRANSLATE_NOOP("fg::Scheme", "Dark");
    dark.face = c("#2e2e2e");
    dark.light = c("#6a6a6a");
    dark.midlight = c("#484848");
    dark.dark = c("#191919");
    dark.shadow = c("#000000");
    dark.base = c("#1a1a1a");
    dark.text = c("#f0f0f0");
    dark.disabledText = c("#a8a8a8");
    dark.selection = c("#3a6ea5");
    dark.selectionText = c("#ffffff");
    dark.titleStart = c("#1f3f66");
    dark.titleEnd = c("#3a6ea5");
    dark.titleText = c("#ffffff");
    dark.titleInactive = c("#444444");
    dark.graphBack = c("#000000");
    dark.graphGrid = c("#2f5f6f");
    dark.graphLine = c("#5fd0ff");
    dark.graphLine2 = c("#ffd060");
    list << dark;

    list << phosphor("green", QT_TRANSLATE_NOOP("fg::Scheme", "Green Phosphor"), "#33ff66", "#1f9f40", "#0f5f25");
    list << phosphor("amber", QT_TRANSLATE_NOOP("fg::Scheme", "Amber Phosphor"), "#ffb000", "#a87400", "#5f4200");
    list << phosphor("blue", QT_TRANSLATE_NOOP("fg::Scheme", "Blue Phosphor"), "#66c8ff", "#4a94c0", "#1f4a62");
    list << phosphor("mono", QT_TRANSLATE_NOOP("fg::Scheme", "Mono"), "#e6e6e6", "#909090", "#505050");
    list << phosphor("red", QT_TRANSLATE_NOOP("fg::Scheme", "Red Phosphor"), "#ff6a5a", "#d0584a", "#62261f");
    list << phosphor("purple", QT_TRANSLATE_NOOP("fg::Scheme", "Purple Phosphor"), "#d29aff", "#a47ccc", "#503864");

    Scheme contrast;
    contrast.id = QStringLiteral("contrast");
    contrast.name = QT_TRANSLATE_NOOP("fg::Scheme", "High Contrast");
    contrast.face = c("#000000");
    contrast.light = c("#ffffff");
    contrast.midlight = c("#000000");
    contrast.dark = c("#ffffff");
    contrast.shadow = c("#ffffff");
    contrast.base = c("#000000");
    contrast.text = c("#ffffff");
    contrast.disabledText = c("#00ff00");
    contrast.selection = c("#ffff00");
    contrast.selectionText = c("#000000");
    contrast.titleStart = contrast.titleEnd = c("#800080");
    contrast.titleText = c("#ffffff");
    contrast.titleInactive = c("#000000");
    contrast.graphBack = c("#000000");
    contrast.graphGrid = c("#808080");
    contrast.graphLine = c("#ffff00");
    contrast.graphLine2 = c("#00ffff");
    list << contrast;

    return list;
}

double channel(double value)
{
    value /= 255.0;
    return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double luminance(const QColor& color)
{
    return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green()) + 0.0722 * channel(color.blue());
}

QColor desaturated(const QColor& color, double factor)
{
    int h, s, l, a;
    color.getHsl(&h, &s, &l, &a);
    return QColor::fromHsl(h, static_cast<int>(s * factor), l, a);
}

} // namespace

const QList<Scheme>& schemes()
{
    static const QList<Scheme> list = build();
    return list;
}

const Scheme& schemeById(const QString& id)
{
    for (const Scheme& scheme : schemes())
        if (scheme.id == id)
            return scheme;
    return schemes().first();
}

Scheme withSaturation(const Scheme& scheme, int saturation)
{
    if (saturation >= 10)
        return scheme;
    const double factor = qBound(0, saturation, 10) / 10.0;
    Scheme s = scheme;
    for (QColor* color : {&s.face, &s.light, &s.midlight, &s.dark, &s.shadow, &s.base, &s.text, &s.disabledText, &s.selection, &s.selectionText,
                          &s.titleStart, &s.titleEnd, &s.titleText, &s.titleInactive, &s.graphBack, &s.graphGrid, &s.graphLine, &s.graphLine2})
        *color = desaturated(*color, factor);
    return s;
}

QPalette paletteFor(const Scheme& s)
{
    QPalette p;
    for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const bool off = group == QPalette::Disabled;
        p.setColor(group, QPalette::Window, s.face);
        p.setColor(group, QPalette::WindowText, off ? s.disabledText : s.text);
        p.setColor(group, QPalette::Base, s.base);
        p.setColor(group, QPalette::AlternateBase, s.base);
        p.setColor(group, QPalette::Text, off ? s.disabledText : s.text);
        p.setColor(group, QPalette::PlaceholderText, s.disabledText);
        p.setColor(group, QPalette::Button, s.face);
        p.setColor(group, QPalette::ButtonText, off ? s.disabledText : s.text);
        p.setColor(group, QPalette::BrightText, s.text);
        p.setColor(group, QPalette::Light, s.light);
        p.setColor(group, QPalette::Midlight, s.midlight);
        p.setColor(group, QPalette::Mid, s.dark);
        p.setColor(group, QPalette::Dark, s.dark);
        p.setColor(group, QPalette::Shadow, s.shadow);
        p.setColor(group, QPalette::Highlight, s.selection);
        p.setColor(group, QPalette::HighlightedText, s.selectionText);
        p.setColor(group, QPalette::ToolTipBase, s.face == QColor(0xc0, 0xc0, 0xc0) ? QColor(0xff, 0xff, 0xe1) : s.base);
        p.setColor(group, QPalette::ToolTipText, s.face == QColor(0xc0, 0xc0, 0xc0) ? QColor(0, 0, 0) : s.text);
        p.setColor(group, QPalette::Link, s.selection);
        p.setColor(group, QPalette::LinkVisited, s.selection);
    }
    return p;
}

double contrastRatio(const QColor& a, const QColor& b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

} // namespace fg
