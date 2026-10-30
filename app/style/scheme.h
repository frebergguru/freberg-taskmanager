// Colour schemes, in the spirit of Display Properties > Appearance. Only colours
// change between schemes; bevels and layout stay the same.
#pragma once

#include <QColor>
#include <QList>
#include <QPalette>
#include <QString>

namespace fg {

struct Scheme {
    QString id;
    const char* name; // translated with tr() where shown
    QColor face, light, midlight, dark, shadow;
    QColor base, text, disabledText;
    QColor selection, selectionText;
    QColor titleStart, titleEnd, titleText, titleInactive;
    QColor graphBack, graphGrid, graphLine, graphLine2;
};

const QList<Scheme>& schemes();
const Scheme& schemeById(const QString& id);

// saturation runs from 0 (grey) to 10 (as designed).
Scheme withSaturation(const Scheme& scheme, int saturation);
QPalette paletteFor(const Scheme& scheme);

double contrastRatio(const QColor& a, const QColor& b);

} // namespace fg
