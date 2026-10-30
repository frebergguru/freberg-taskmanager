// A skin is data, never code: a JSON manifest with optional images. Anything left
// out is inherited from the default skin. See Docs/skin-format.md.
#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QPolygonF>
#include <QString>
#include <QStringList>

namespace fg {

struct SkinSurface {
    QColor top = Qt::transparent;
    QColor bottom = Qt::transparent; // the same as top for a solid colour
};

struct SkinImage {
    QImage image;
    enum Fit { Fill, Fit, Stretch, Tile } fit = Fill;
    double opacity = 1.0;
};

struct Skin {
    QString id = QStringLiteral("default");
    QString name = QStringLiteral("Default");
    QString author, description;

    SkinSurface window, header, tile;
    QColor ink, inkDim, outline;
    SkinImage windowImage, headerImage;

    double fontSize = 10;
    bool fontBold = false;
    QString fontFamily;

    int cornerRadius = 0, border = 2, padding = 4, gap = 2;
    QString headerTitle;
    bool hideTitle = false;

    enum MeterStyle { Bars, Gauge, Graph, Vertical, Rows } meterStyle = Rows;
    int segments = 12;
    bool flat = false;
    QStringList quadrants; // which readings show, in order
    QHash<QString, QColor> accent; // channel -> lit colour
    QColor unlit;

    double glow = 0;

    enum Shape { Rectangle, Circle, Ellipse, Capsule, Polygon, Mask } shape = Rectangle;
    QPolygonF polygon;   // in 0-1 units
    QImage maskImage;
    int frameInset = 0;

    bool builtIn = true;
};

struct SkinProblem {
    QString file;
    QString reason;
};

struct SkinLibrary {
    QList<Skin> skins;
    QList<SkinProblem> problems;
};

const QStringList& skinChannels();
Skin defaultSkin();
QString skinsFolder();

// Reads the built-in skins, then every entry of folder. Bad ones are listed with a
// reason and skipped; one bad skin never stops the rest.
SkinLibrary loadSkins(const QString& folder);

// Applies a manifest over the default. Empty result and a reason on failure.
bool parseSkin(const QByteArray& json, const QString& imageFolder, Skin& out, QString& reason);

} // namespace fg
