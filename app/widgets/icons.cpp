#include "icons.h"

#include <QHash>
#include <QPainter>
#include <QPixmap>

namespace fg::Icons {

namespace {

// One letter per pixel, looked up in a short colour table. '.' is transparent.
QIcon fromRows(std::initializer_list<const char*> rows)
{
    static const QHash<char, QRgb> colours = {
        {'k', qRgb(0, 0, 0)},    {'w', qRgb(255, 255, 255)}, {'s', qRgb(192, 192, 192)}, {'d', qRgb(128, 128, 128)},
        {'g', qRgb(0, 255, 0)},  {'G', qRgb(0, 128, 0)},     {'b', qRgb(0, 0, 128)},     {'B', qRgb(0, 128, 128)},
        {'y', qRgb(255, 255, 0)},
    };
    const int height = static_cast<int>(rows.size());
    const int width = static_cast<int>(qstrlen(*rows.begin()));
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    int y = 0;
    for (const char* row : rows) {
        for (int x = 0; x < width; ++x)
            if (const auto found = colours.constFind(row[x]); found != colours.constEnd())
                image.setPixel(x, y, 0xff000000u | *found);
        ++y;
    }
    QIcon icon;
    for (const int scale : {1, 2, 3}) // whole multiples keep the pixels square
        icon.addPixmap(QPixmap::fromImage(image.scaled(width * scale, height * scale, Qt::IgnoreAspectRatio, Qt::FastTransformation)));
    return icon;
}

} // namespace

QIcon app()
{
    return fromRows({
        "................",
        ".kkkkkkkkkkkkkk.",
        ".kwwwwwwwwwwwsk.",
        ".kwkkkkkkkkkkdk.",
        ".kwkGGGGGGGGkdk.",
        ".kwkGGGGGgGGkdk.",
        ".kwkGGGGgGgGkdk.",
        ".kwkGgGgGGGgkdk.",
        ".kwkgGgGGGGGkdk.",
        ".kwkkkkkkkkkkdk.",
        ".ksddddddddddsk.",
        ".kkkkkkkkkkkkkk.",
        "......kssk......",
        "....kkkkkkkk....",
        "....kssssssk....",
        "....kkkkkkkk....",
    });
}

QIcon refresh()
{
    return fromRows({
        "................",
        ".....GGGGG...G..",
        "...GGgggggGG.GG.",
        "..GggGGGGGggGgG.",
        "..GgG.....GgggG.",
        ".GgG.....GGGGGG.",
        ".GgG............",
        ".GGG............",
        "............GGG.",
        "............GgG.",
        ".GGGGGG.....GgG.",
        ".GgggG.....GgG..",
        ".GgGggGGGGGggG..",
        ".GG.GGgggggGG...",
        "..G...GGGGG.....",
        "................",
    });
}

QIcon settings()
{
    return fromRows({
        "................",
        "......kkkk......",
        "..kk..kssk..kk..",
        ".kssk.kssk.kssk.",
        ".ksssskssksssk..",
        "..kssssssssssk..",
        "...kssskkssssk..",
        ".kkkssk..ksskkk.",
        ".kssssk..kssssk.",
        ".kkksssskssskkk.",
        "...ksssssssssk..",
        "..kssssssssssk..",
        ".kssskssksssssk.",
        ".kssk.kssk.kssk.",
        "..kk..kkkk..kk..",
        "................",
    });
}

} // namespace fg::Icons
