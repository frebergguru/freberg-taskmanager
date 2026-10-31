#include "scheme.h"

#include <QCoreApplication>
#include <QHash>
#include <cmath>

namespace fg {

namespace {

QColor c(const char* hex) { return QColor::fromString(QLatin1String(hex)); }

void setIdentity(Scheme& s, const char* family, const char* name, bool isDark)
{
    s.family = QLatin1String(family);
    s.id = s.family + (isDark ? QStringLiteral("-dark") : QStringLiteral("-light"));
    s.name = name;
    s.isDark = isDark;
}

// One colour on black, like an old monitor.
Scheme phosphorDark(const char* family, const char* name, const char* bright, const char* mid, const char* low)
{
    Scheme s;
    setIdentity(s, family, name, true);
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

// The same colour as ink on a tinted page, like a printout.
Scheme phosphorLight(const char* family, const char* name, const char* ink, const char* strong, const char* face, const char* base, const char* disabled, const char* grid, const char* line2)
{
    Scheme s;
    setIdentity(s, family, name, false);
    s.face = c(face);
    s.light = c(base);
    s.midlight = c(face).lighter(104);
    s.dark = c(face).darker(150);
    s.shadow = c(ink);
    s.base = c(base);
    s.text = c(ink);
    s.disabledText = c(disabled);
    s.selection = c(strong);
    s.selectionText = QColor(255, 255, 255);
    s.titleStart = c(strong);
    s.titleEnd = c(strong).lighter(135);
    s.titleText = QColor(255, 255, 255);
    s.titleInactive = c(face).darker(125);
    s.graphBack = c(base);
    s.graphGrid = c(grid);
    s.graphLine = c(strong);
    s.graphLine2 = c(line2);
    return s;
}

struct FlatColours {
    const char* face, *base, *text, *disabled, *selection, *selectionText, *accent, *border, *hover, *strong, *grid, *line, *line2;
};

Scheme flat(const char* family, const char* name, Design design, bool isDark, const FlatColours& k)
{
    Scheme f;
    setIdentity(f, family, name, isDark);
    f.design = design;
    f.face = c(k.face);
    f.light = c(k.base);
    f.midlight = c(k.hover);
    f.dark = c(k.strong);
    f.shadow = isDark ? c(k.strong).lighter(130) : c(k.strong).darker(130);
    f.base = c(k.base);
    f.text = c(k.text);
    f.disabledText = c(k.disabled);
    f.selection = c(k.selection);
    f.selectionText = c(k.selectionText);
    f.accent = c(k.accent);
    f.border = c(k.border);
    f.hover = c(k.hover);
    f.titleStart = f.titleEnd = f.titleInactive = c(k.face);
    f.titleText = c(k.text);
    f.graphBack = c(k.base);
    f.graphGrid = c(k.grid);
    f.graphLine = c(k.line);
    f.graphLine2 = c(k.line2);
    return f;
}

QList<Scheme> build()
{
    QList<Scheme> list;

    const char* const classicName = QT_TRANSLATE_NOOP("fg::Scheme", "Classic");
    Scheme classicLight;
    setIdentity(classicLight, "classic", classicName, false);
    classicLight.face = c("#c0c0c0");
    classicLight.light = c("#ffffff");
    classicLight.midlight = c("#dfdfdf");
    classicLight.dark = c("#808080");
    classicLight.shadow = c("#000000");
    classicLight.base = c("#ffffff");
    classicLight.text = c("#000000");
    classicLight.disabledText = c("#4a4a4a"); // the old #808080 misses WCAG AA on silver
    classicLight.selection = c("#000080");
    classicLight.selectionText = c("#ffffff");
    classicLight.titleStart = c("#000080");
    classicLight.titleEnd = c("#1084d0");
    classicLight.titleText = c("#ffffff");
    classicLight.titleInactive = c("#686868");
    classicLight.graphBack = c("#000000");
    classicLight.graphGrid = c("#008040");
    classicLight.graphLine = c("#00ff00");
    classicLight.graphLine2 = c("#ffff00");
    list << classicLight;

    Scheme classicDark;
    setIdentity(classicDark, "classic", classicName, true);
    classicDark.face = c("#2e2e2e");
    classicDark.light = c("#6a6a6a");
    classicDark.midlight = c("#484848");
    classicDark.dark = c("#191919");
    classicDark.shadow = c("#000000");
    classicDark.base = c("#1a1a1a");
    classicDark.text = c("#f0f0f0");
    classicDark.disabledText = c("#a8a8a8");
    classicDark.selection = c("#3a6ea5");
    classicDark.selectionText = c("#ffffff");
    classicDark.titleStart = c("#1f3f66");
    classicDark.titleEnd = c("#3a6ea5");
    classicDark.titleText = c("#ffffff");
    classicDark.titleInactive = c("#444444");
    classicDark.graphBack = c("#000000");
    classicDark.graphGrid = c("#2f5f6f");
    classicDark.graphLine = c("#5fd0ff");
    classicDark.graphLine2 = c("#ffd060");
    list << classicDark;

    const char* const sandName = QT_TRANSLATE_NOOP("fg::Scheme", "Sand");
    Scheme sandLight = classicLight;
    setIdentity(sandLight, "sand", sandName, false);
    sandLight.face = c("#ece9d8");
    sandLight.midlight = c("#f5f3e8");
    sandLight.dark = c("#9d9a87");
    sandLight.selection = c("#2f5d8c");
    sandLight.titleStart = c("#2f5d8c");
    sandLight.titleEnd = c("#8db4dc");
    sandLight.disabledText = c("#55534a");
    list << sandLight;

    Scheme sandDark = classicDark;
    setIdentity(sandDark, "sand", sandName, true);
    sandDark.face = c("#38342a");
    sandDark.light = c("#6e6854");
    sandDark.midlight = c("#4f4a3c");
    sandDark.dark = c("#201d16");
    sandDark.base = c("#26231b");
    sandDark.text = c("#f2eee0");
    sandDark.disabledText = c("#b4ae9a");
    sandDark.selection = c("#2f5d8c");
    sandDark.titleStart = c("#2a4a6e");
    sandDark.titleEnd = c("#4f7aa8");
    sandDark.titleInactive = c("#4f4a3c");
    sandDark.graphGrid = c("#4f4a3c");
    sandDark.graphLine = c("#e8c878");
    sandDark.graphLine2 = c("#8db4dc");
    list << sandDark;

    const char* const green = QT_TRANSLATE_NOOP("fg::Scheme", "Green Phosphor");
    list << phosphorLight("green", green, "#0b3d1c", "#0f6b32", "#dcebe0", "#f4faf6", "#335c41", "#c9dfd0", "#7a5a00");
    list << phosphorDark("green", green, "#33ff66", "#1f9f40", "#0f5f25");
    const char* const amber = QT_TRANSLATE_NOOP("fg::Scheme", "Amber Phosphor");
    list << phosphorLight("amber", amber, "#4a3000", "#8a5a00", "#f3e7cf", "#fdf8ec", "#6b5223", "#e6d6b4", "#0f5c8a");
    list << phosphorDark("amber", amber, "#ffb000", "#a87400", "#5f4200");
    const char* const blue = QT_TRANSLATE_NOOP("fg::Scheme", "Blue Phosphor");
    list << phosphorLight("blue", blue, "#0c3350", "#14608f", "#dbe9f3", "#f3f9fd", "#2f5670", "#c6dbea", "#8a3a00");
    list << phosphorDark("blue", blue, "#66c8ff", "#4a94c0", "#1f4a62");
    const char* const mono = QT_TRANSLATE_NOOP("fg::Scheme", "Mono");
    list << phosphorLight("mono", mono, "#1a1a1a", "#4a4a4a", "#e4e4e4", "#fafafa", "#575757", "#d2d2d2", "#7a7a7a");
    list << phosphorDark("mono", mono, "#e6e6e6", "#909090", "#505050");
    const char* const red = QT_TRANSLATE_NOOP("fg::Scheme", "Red Phosphor");
    list << phosphorLight("red", red, "#5a0f08", "#a32a1c", "#f5e0dc", "#fdf5f3", "#7a3a32", "#ebcdc7", "#14608f");
    list << phosphorDark("red", red, "#ff6a5a", "#d0584a", "#62261f");
    const char* const purple = QT_TRANSLATE_NOOP("fg::Scheme", "Purple Phosphor");
    list << phosphorLight("purple", purple, "#33154f", "#6b35a0", "#e9dff3", "#faf6fd", "#574070", "#dcccec", "#8a5a00");
    list << phosphorDark("purple", purple, "#d29aff", "#a47ccc", "#503864");

    const char* const contrastName = QT_TRANSLATE_NOOP("fg::Scheme", "High Contrast");
    Scheme contrastLight;
    setIdentity(contrastLight, "contrast", contrastName, false);
    contrastLight.face = c("#ffffff");
    contrastLight.light = c("#000000");
    contrastLight.midlight = c("#ffffff");
    contrastLight.dark = c("#000000");
    contrastLight.shadow = c("#000000");
    contrastLight.base = c("#ffffff");
    contrastLight.text = c("#000000");
    contrastLight.disabledText = c("#005000");
    contrastLight.selection = c("#000080");
    contrastLight.selectionText = c("#ffffff");
    contrastLight.titleStart = contrastLight.titleEnd = c("#000080");
    contrastLight.titleText = c("#ffffff");
    contrastLight.titleInactive = c("#ffffff");
    contrastLight.graphBack = c("#ffffff");
    contrastLight.graphGrid = c("#808080");
    contrastLight.graphLine = c("#000080");
    contrastLight.graphLine2 = c("#a00000");
    list << contrastLight;

    Scheme contrastDark;
    setIdentity(contrastDark, "contrast", contrastName, true);
    contrastDark.face = c("#000000");
    contrastDark.light = c("#ffffff");
    contrastDark.midlight = c("#000000");
    contrastDark.dark = c("#ffffff");
    contrastDark.shadow = c("#ffffff");
    contrastDark.base = c("#000000");
    contrastDark.text = c("#ffffff");
    contrastDark.disabledText = c("#00ff00");
    contrastDark.selection = c("#ffff00");
    contrastDark.selectionText = c("#000000");
    contrastDark.titleStart = contrastDark.titleEnd = c("#800080");
    contrastDark.titleText = c("#ffffff");
    contrastDark.titleInactive = c("#000000");
    contrastDark.graphBack = c("#000000");
    contrastDark.graphGrid = c("#808080");
    contrastDark.graphLine = c("#ffff00");
    contrastDark.graphLine2 = c("#00ffff");
    list << contrastDark;

    const char* const modern = QT_TRANSLATE_NOOP("fg::Scheme", "Modern");
    list << flat("modern", modern, Design::Modern, false,
                 {"#f3f4f6", "#ffffff", "#1c1f24", "#5f6672", "#2563eb", "#ffffff", "#2563eb", "#d3d7de", "#e7e9ed", "#b4b9c2", "#eceef1", "#2563eb", "#c2410c"});
    list << flat("modern", modern, Design::Modern, true,
                 {"#1e2126", "#16181c", "#e8eaed", "#9aa1ab", "#2563eb", "#ffffff", "#60a5fa", "#33373e", "#2a2e34", "#4a4f58", "#262a30", "#60a5fa", "#fbbf24"});

    const char* const neon = QT_TRANSLATE_NOOP("fg::Scheme", "Neon Glow");
    list << flat("neon", neon, Design::Neon, false,
                 {"#f5f0ff", "#ffffff", "#1d1238", "#5c4f80", "#c4009a", "#ffffff", "#008ca3", "#d5c8f5", "#ece4ff", "#a99ae0", "#ece4ff", "#008ca3", "#c4009a"});
    list << flat("neon", neon, Design::Neon, true,
                 {"#0d0b1a", "#07060f", "#f0eaff", "#a79bc9", "#ff2bd6", "#000000", "#00f0ff", "#3a2f6b", "#1a1633", "#4b3d8a", "#1d1840", "#00f0ff", "#ff2bd6"});

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

QStringList schemeFamilies()
{
    QStringList families;
    for (const Scheme& scheme : schemes())
        if (!families.contains(scheme.family))
            families << scheme.family;
    return families;
}

const Scheme& schemeById(const QString& id)
{
    // Names used by earlier versions of the app, so that a saved choice survives.
    static const QHash<QString, QString> earlier = {
        {QStringLiteral("standard"), QStringLiteral("classic-light")}, {QStringLiteral("dark"), QStringLiteral("classic-dark")}, {QStringLiteral("light"), QStringLiteral("sand-light")},
        {QStringLiteral("green"), QStringLiteral("green-dark")},       {QStringLiteral("amber"), QStringLiteral("amber-dark")},  {QStringLiteral("blue"), QStringLiteral("blue-dark")},
        {QStringLiteral("mono"), QStringLiteral("mono-dark")},         {QStringLiteral("red"), QStringLiteral("red-dark")},      {QStringLiteral("purple"), QStringLiteral("purple-dark")},
        {QStringLiteral("contrast"), QStringLiteral("contrast-dark")}, {QStringLiteral("neon"), QStringLiteral("neon-dark")},
    };
    const QString wanted = earlier.value(id, id);
    for (const Scheme& scheme : schemes())
        if (scheme.id == wanted)
            return scheme;
    return schemes().first();
}

const Scheme& schemeFor(const QString& family, bool isDark)
{
    return schemeById(family + (isDark ? QStringLiteral("-dark") : QStringLiteral("-light")));
}

// Moves front towards black or white, whichever helps, and then back the other way if it must.
static void keepContrast(QColor& front, QColor& back, double need)
{
    constexpr int kSteps = 20;
    const auto mix = [](const QColor& from, const QColor& to, double amount) {
        return QColor::fromRgbF(float(from.redF() + (to.redF() - from.redF()) * amount), float(from.greenF() + (to.greenF() - from.greenF()) * amount), float(from.blueF() + (to.blueF() - from.blueF()) * amount), from.alphaF());
    };
    if (contrastRatio(front, back) >= need)
        return;
    const QColor black(0, 0, 0), white(255, 255, 255);
    const QColor towards = contrastRatio(black, back) > contrastRatio(white, back) ? black : white;
    const QColor startFront = front;
    for (int step = 1; step <= kSteps && contrastRatio(front, back) < need; ++step)
        front = mix(startFront, towards, double(step) / kSteps);
    const QColor away = towards == black ? white : black;
    const QColor startBack = back;
    for (int step = 1; step <= kSteps && contrastRatio(front, back) < need; ++step)
        back = mix(startBack, away, double(step) / kSteps);
}

Scheme withSaturation(const Scheme& scheme, int saturation)
{
    if (saturation >= 10)
        return scheme;
    const double factor = qBound(0, saturation, 10) / 10.0;
    Scheme s = scheme;
    for (QColor* color : {&s.accent, &s.border, &s.hover, &s.face, &s.light, &s.midlight, &s.dark, &s.shadow, &s.base, &s.text, &s.disabledText, &s.selection, &s.selectionText,
                          &s.titleStart, &s.titleEnd, &s.titleText, &s.titleInactive, &s.graphBack, &s.graphGrid, &s.graphLine, &s.graphLine2})
        *color = desaturated(*color, factor);
    // Grey hues can sit closer together than the colours they came from, and AA still has to hold.
    for (int pass = 0; pass < 2; ++pass) {
        keepContrast(s.text, s.face, 4.5);
        keepContrast(s.text, s.base, 4.5);
        keepContrast(s.disabledText, s.face, 4.5);
        keepContrast(s.selectionText, s.selection, 4.5);
        keepContrast(s.titleText, s.titleStart, 4.5);
        keepContrast(s.accent, s.graphBack, 3.0);
        keepContrast(s.hover, s.text, 4.5); // the row's colour gives way, since text must also stay readable elsewhere
        keepContrast(s.graphLine, s.graphBack, 3.0);
        keepContrast(s.graphLine2, s.graphBack, 3.0);
    }
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
