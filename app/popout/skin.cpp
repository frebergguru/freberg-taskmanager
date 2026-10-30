#include "skin.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace fg {

namespace {

constexpr qint64 kManifestLimit = 256 * 1024;
constexpr qint64 kImageFileLimit = 8 * 1024 * 1024;
constexpr int kImageSideLimit = 4096;
constexpr qint64 kImagePixelLimit = 4 * 1000 * 1000;
constexpr int kEntryLimit = 64;

const char* const kBuiltIn[] = {
    R"({"format":1,"id":"default","name":"Default"})",
    R"({"format":1,"id":"console","name":"Console",
        "surfaces":{"window":"#000000","tile":"#101010","header":"#202020"},
        "colours":{"ink":"#33ff66","inkDim":"#1f9f40","unlit":"#0f3f18","outline":"#1f9f40"},
        "fonts":{"body":{"size":9,"weight":"bold"}},
        "meters":{"style":"rows","segments":24},
        "accent":{"cpu":"#33ff66","memory":"#33ff66","gpu":"#33ff66","thermal":"#ffb000","disk":"#33ff66","network":"#33ff66","power":"#ffb000","alert":"#ff5040"},
        "quadrants":["cpu","memory","gpu","thermal","disk","network"]})",
    R"({"format":1,"id":"traces","name":"Traces",
        "surfaces":{"window":"#000000","tile":"#000000","header":"#000080"},
        "colours":{"ink":"#00ff00","inkDim":"#008040","unlit":"#004020","outline":"#008040"},
        "meters":{"style":"graph"},
        "quadrants":["cpu","memory","gpu","thermal"]})",
    R"({"format":1,"id":"column","name":"Classic Column",
        "surfaces":{"window":"#000000","tile":"#000000"},
        "colours":{"ink":"#00ff00","inkDim":"#00a000","unlit":"#004000","outline":"#808080"},
        "meters":{"style":"vertical","segments":20},
        "quadrants":["cpu","memory","gpu","thermal"]})",
    R"({"format":1,"id":"cassette","name":"Cassette Deck",
        "surfaces":{"window":{"top":"#3a3a44","bottom":"#16161c"},"tile":"#0a0a10","header":{"top":"#50505c","bottom":"#2a2a32"}},
        "colours":{"ink":"#f0d060","inkDim":"#a08a40","unlit":"#2a2410","outline":"#000000"},
        "metrics":{"corner":3,"padding":5,"gap":2,"border":2},
        "meters":{"style":"bars","segments":16},
        "accent":{"cpu":"#70ff70","memory":"#70e0ff","gpu":"#ffa040","thermal":"#ff6060"}})",
    R"({"format":1,"id":"dash","name":"Dashboard",
        "surfaces":{"window":"#101820","tile":"#182430"},
        "colours":{"ink":"#e8f0f8","inkDim":"#90a8c0","unlit":"#304050","outline":"#405060"},
        "metrics":{"corner":6,"padding":6,"gap":4},
        "meters":{"style":"gauge"},
        "accent":{"cpu":"#60c0ff","memory":"#60ffb0","gpu":"#ffc060","thermal":"#ff7070"}})",
};

bool readColour(const QJsonValue& value, QColor& out)
{
    if (!value.isString())
        return false;
    const QString text = value.toString();
    if (text == QLatin1String("clear") || text == QLatin1String("none")) {
        out = Qt::transparent;
        return true;
    }
    // CSS order, #rrggbbaa, which is not the order Qt reads.
    if (text.startsWith(QLatin1Char('#')) && text.size() == 9) {
        bool ok = false;
        const uint v = QStringView(text).mid(1).toUInt(&ok, 16);
        if (!ok)
            return false;
        out = QColor((v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
        return true;
    }
    if (!text.startsWith(QLatin1Char('#')))
        return false;
    const QColor parsed = QColor::fromString(text);
    if (!parsed.isValid())
        return false;
    out = parsed;
    return true;
}

void readSurface(const QJsonValue& value, SkinSurface& out)
{
    QColor solid;
    if (readColour(value, solid)) {
        out.top = out.bottom = solid;
    } else if (value.isObject()) {
        const QJsonObject o = value.toObject();
        QColor top, bottom;
        if (readColour(o.value(QStringLiteral("top")), top) && readColour(o.value(QStringLiteral("bottom")), bottom)) {
            out.top = top;
            out.bottom = bottom;
        }
    }
}

// File names only: a skin can't reach outside its own folder.
bool safeImageName(const QString& name)
{
    return !name.isEmpty() && !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\')) && name != QLatin1String("..") && name != QLatin1String(".") && !name.contains(QLatin1Char(':'));
}

QImage readImage(const QString& folder, const QString& name)
{
    if (folder.isEmpty() || !safeImageName(name))
        return {};
    const QFileInfo info(QDir(folder), name);
    if (!info.isFile() || info.size() > kImageFileLimit)
        return {};
    QImageReader reader(info.filePath());
    const QSize size = reader.size();
    if (size.width() > kImageSideLimit || size.height() > kImageSideLimit || static_cast<qint64>(size.width()) * size.height() > kImagePixelLimit)
        return {};
    return reader.read();
}

void readSkinImage(const QJsonValue& value, const QString& folder, SkinImage& out)
{
    if (!value.isObject())
        return;
    const QJsonObject o = value.toObject();
    out.image = readImage(folder, o.value(QStringLiteral("file")).toString());
    out.opacity = qBound(0.0, o.value(QStringLiteral("opacity")).toDouble(1.0), 1.0);
    const QString fit = o.value(QStringLiteral("fit")).toString();
    out.fit = fit == QLatin1String("fit") ? SkinImage::Fit : fit == QLatin1String("stretch") ? SkinImage::Stretch : fit == QLatin1String("tile") ? SkinImage::Tile : SkinImage::Fill;
}

int clampedInt(const QJsonValue& value, int low, int high, int fallback)
{
    return value.isDouble() ? qBound(low, value.toInt(fallback), high) : fallback;
}

} // namespace

const QStringList& skinChannels()
{
    static const QStringList channels = {QStringLiteral("cpu"), QStringLiteral("memory"), QStringLiteral("gpu"), QStringLiteral("thermal"), QStringLiteral("disk"), QStringLiteral("network"), QStringLiteral("power"), QStringLiteral("alert")};
    return channels;
}

Skin defaultSkin()
{
    Skin s;
    s.window = {QColor(0xc0, 0xc0, 0xc0), QColor(0xc0, 0xc0, 0xc0)};
    s.header = {QColor(0, 0, 0x80), QColor(0x10, 0x84, 0xd0)};
    s.tile = {QColor(0, 0, 0), QColor(0, 0, 0)};
    s.ink = QColor(0, 0xff, 0);
    s.inkDim = QColor(0, 0xa0, 0);
    s.outline = QColor(0x80, 0x80, 0x80);
    s.unlit = QColor(0, 0x40, 0);
    s.quadrants = {QStringLiteral("cpu"), QStringLiteral("memory"), QStringLiteral("gpu"), QStringLiteral("thermal")};
    return s;
}

QString skinsFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Skins");
}

bool parseSkin(const QByteArray& json, const QString& imageFolder, Skin& out, QString& reason)
{
    if (json.size() > kManifestLimit) {
        reason = QCoreApplication::translate("fg::Skin", "The manifest is larger than 256 KB.");
        return false;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        reason = QCoreApplication::translate("fg::Skin", "The manifest is not valid JSON.");
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toInt(-1) != 1) {
        reason = QCoreApplication::translate("fg::Skin", "The format value must be 1.");
        return false;
    }
    const QString id = root.value(QStringLiteral("id")).toString();
    static const QRegularExpression validId(QStringLiteral("^[A-Za-z0-9._-]{1,64}$"));
    if (!validId.match(id).hasMatch()) {
        reason = QCoreApplication::translate("fg::Skin", "The id is missing or has characters other than letters, digits, dot, dash and underscore.");
        return false;
    }
    const QString name = root.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
        reason = QCoreApplication::translate("fg::Skin", "The name is missing.");
        return false;
    }

    Skin s = defaultSkin();
    s.id = id;
    s.name = name;
    s.author = root.value(QStringLiteral("author")).toString();
    s.description = root.value(QStringLiteral("description")).toString();

    const QJsonObject surfaces = root.value(QStringLiteral("surfaces")).toObject();
    readSurface(surfaces.value(QStringLiteral("window")), s.window);
    readSurface(surfaces.value(QStringLiteral("header")), s.header);
    readSurface(surfaces.value(QStringLiteral("tile")), s.tile);
    readSkinImage(surfaces.value(QStringLiteral("windowImage")), imageFolder, s.windowImage);
    readSkinImage(surfaces.value(QStringLiteral("headerImage")), imageFolder, s.headerImage);

    const QJsonObject colours = root.value(QStringLiteral("colours")).toObject();
    readColour(colours.value(QStringLiteral("ink")), s.ink);
    readColour(colours.value(QStringLiteral("inkDim")), s.inkDim);
    readColour(colours.value(QStringLiteral("outline")), s.outline);
    readColour(colours.value(QStringLiteral("unlit")), s.unlit);

    const QJsonObject accent = root.value(QStringLiteral("accent")).toObject();
    for (const QString& channel : skinChannels()) {
        QColor colour;
        if (readColour(accent.value(channel), colour))
            s.accent.insert(channel, colour);
    }

    const QJsonObject font = root.value(QStringLiteral("fonts")).toObject().value(QStringLiteral("body")).toObject();
    if (font.contains(QStringLiteral("size")))
        s.fontSize = qBound(5.0, font.value(QStringLiteral("size")).toDouble(s.fontSize), 24.0);
    s.fontBold = font.value(QStringLiteral("weight")).toString() == QLatin1String("bold");
    s.fontFamily = font.value(QStringLiteral("family")).toString();

    const QJsonObject metrics = root.value(QStringLiteral("metrics")).toObject();
    s.cornerRadius = clampedInt(metrics.value(QStringLiteral("corner")), 0, 200, s.cornerRadius);
    s.border = clampedInt(metrics.value(QStringLiteral("border")), 0, 16, s.border);
    s.padding = clampedInt(metrics.value(QStringLiteral("padding")), 0, 64, s.padding);
    s.gap = clampedInt(metrics.value(QStringLiteral("gap")), 0, 32, s.gap);

    const QJsonObject header = root.value(QStringLiteral("header")).toObject();
    s.headerTitle = header.value(QStringLiteral("title")).toString();
    s.hideTitle = header.value(QStringLiteral("hideTitle")).toBool(false);

    const QJsonObject meters = root.value(QStringLiteral("meters")).toObject();
    const QString style = meters.value(QStringLiteral("style")).toString();
    if (style == QLatin1String("bars"))
        s.meterStyle = Skin::Bars;
    else if (style == QLatin1String("gauge"))
        s.meterStyle = Skin::Gauge;
    else if (style == QLatin1String("graph"))
        s.meterStyle = Skin::Graph;
    else if (style == QLatin1String("vertical"))
        s.meterStyle = Skin::Vertical;
    else if (style == QLatin1String("rows"))
        s.meterStyle = Skin::Rows;
    s.segments = clampedInt(meters.value(QStringLiteral("segments")), 1, 64, s.segments);
    s.flat = meters.value(QStringLiteral("flat")).toBool(false);

    const QJsonArray quadrants = root.value(QStringLiteral("quadrants")).toArray();
    QStringList chosen;
    for (const QJsonValue& value : quadrants)
        if (skinChannels().contains(value.toString()) && value.toString() != QLatin1String("alert"))
            chosen << value.toString();
    if (!chosen.isEmpty())
        s.quadrants = chosen; // none valid leaves the default four

    s.glow = qBound(0.0, root.value(QStringLiteral("glow")).toDouble(0), 24.0);

    const QJsonObject shape = root.value(QStringLiteral("shape")).toObject();
    const QString kind = shape.value(QStringLiteral("kind")).toString();
    s.frameInset = clampedInt(shape.value(QStringLiteral("frameInset")), 0, 200, 0);
    if (kind == QLatin1String("circle")) {
        s.shape = Skin::Circle;
    } else if (kind == QLatin1String("ellipse")) {
        s.shape = Skin::Ellipse;
    } else if (kind == QLatin1String("capsule")) {
        s.shape = Skin::Capsule;
    } else if (kind == QLatin1String("polygon")) {
        const QJsonArray points = shape.value(QStringLiteral("points")).toArray();
        for (const QJsonValue& point : points) {
            const QJsonArray xy = point.toArray();
            if (xy.size() == 2)
                s.polygon << QPointF(qBound(0.0, xy[0].toDouble(), 1.0), qBound(0.0, xy[1].toDouble(), 1.0));
        }
        if (s.polygon.size() >= 3)
            s.shape = Skin::Polygon;
    } else if (kind == QLatin1String("mask")) {
        s.maskImage = readImage(imageFolder, shape.value(QStringLiteral("file")).toString());
        if (!s.maskImage.isNull())
            s.shape = Skin::Mask;
    }

    out = std::move(s);
    return true;
}

SkinLibrary loadSkins(const QString& folder)
{
    SkinLibrary library;
    QSet<QString> ids, names;

    for (const char* const manifest : kBuiltIn) {
        Skin skin;
        QString reason;
        if (parseSkin(QByteArray(manifest), QString(), skin, reason)) {
            skin.builtIn = true;
            ids.insert(skin.id);
            names.insert(skin.name);
            library.skins << skin;
        } else {
            library.problems.append({skin.id, reason}); // a mistake of ours, shown like any other
        }
    }

    const QFileInfoList entries = QDir(folder).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    int seen = 0;
    for (const QFileInfo& entry : entries) {
        if (++seen > kEntryLimit) {
            library.problems.append({entry.fileName(), QCoreApplication::translate("fg::Skin", "Only the first 64 entries are read.")});
            break;
        }
        QString manifestPath, imageFolder;
        if (entry.isDir()) {
            if (entry.suffix() != QLatin1String("fgskin"))
                continue;
            manifestPath = entry.filePath() + QStringLiteral("/skin.json");
            imageFolder = entry.filePath();
        } else if (entry.suffix() == QLatin1String("fgskin") || entry.suffix() == QLatin1String("json")) {
            manifestPath = entry.filePath();
        } else {
            continue;
        }

        QFile file(manifestPath);
        if (!file.open(QIODevice::ReadOnly)) {
            library.problems.append({entry.fileName(), QCoreApplication::translate("fg::Skin", "The manifest can't be read.")});
            continue;
        }
        if (file.size() > kManifestLimit) {
            library.problems.append({entry.fileName(), QCoreApplication::translate("fg::Skin", "The manifest is larger than 256 KB.")});
            continue;
        }
        Skin skin;
        QString reason;
        if (!parseSkin(file.readAll(), imageFolder, skin, reason)) {
            library.problems.append({entry.fileName(), reason});
            continue;
        }
        if (ids.contains(skin.id)) {
            library.problems.append({entry.fileName(), QCoreApplication::translate("fg::Skin", "This id is already taken.")});
            continue;
        }
        if (names.contains(skin.name)) {
            library.problems.append({entry.fileName(), QCoreApplication::translate("fg::Skin", "This name is already taken.")});
            continue;
        }
        skin.builtIn = false;
        ids.insert(skin.id);
        names.insert(skin.name);
        library.skins << skin;
    }
    return library;
}

} // namespace fg
