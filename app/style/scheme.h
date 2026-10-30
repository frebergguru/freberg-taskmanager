// Colour schemes, in the spirit of Display Properties > Appearance. Only colours
// change between schemes; bevels and layout stay the same.
#pragma once

#include <QColor>
#include <QList>
#include <QPalette>
#include <QString>
#include <QStringList>

namespace fg {

// How controls are shaped. Classic has bevels and stacked tabs, Modern is flat and
// rounded with a sidebar, and Neon is Modern's shapes with a glow. Only Neon glows.
enum class Design { Classic, Modern, Neon };

// Every theme comes as a family with a light and a dark member, such as modern-light.
struct Scheme {
    QString id;
    QString family;
    const char* name; // the family's name, translated with tr() where shown
    bool isDark = false;
    Design design = Design::Classic;
    QColor accent, border, hover; // used by Modern and Neon only
    QColor face, light, midlight, dark, shadow;
    QColor base, text, disabledText;
    QColor selection, selectionText;
    QColor titleStart, titleEnd, titleText, titleInactive;
    QColor graphBack, graphGrid, graphLine, graphLine2;
};

const QList<Scheme>& schemes();
QStringList schemeFamilies();
const Scheme& schemeById(const QString& id);
const Scheme& schemeFor(const QString& family, bool isDark);

// saturation runs from 0 (grey) to 10 (as designed).
Scheme withSaturation(const Scheme& scheme, int saturation);
QPalette paletteFor(const Scheme& scheme);

double contrastRatio(const QColor& a, const QColor& b);

} // namespace fg
