#include "appearance.h"

#include "../settings.h"
#include "style.h"

#include <QApplication>
#include <QFont>
#include <QGuiApplication>
#include <QStyleHints>
#include <QWidget>

namespace fg {

namespace {

constexpr int kBasePixelSize = 11;

Scheme chosenScheme()
{
    const Settings& settings = Settings::instance();
    QString id = settings.scheme();
    if (settings.followSystem())
        id = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark ? QStringLiteral("dark") : QStringLiteral("standard");
    return withSaturation(schemeById(id), settings.saturation());
}

QFont chosenFont()
{
    QFont font;
    font.setFamilies({QStringLiteral("Liberation Sans"), QStringLiteral("Nimbus Sans"), QStringLiteral("DejaVu Sans"), QStringLiteral("sans-serif")});
    font.setPixelSize(qRound(kBasePixelSize * Settings::instance().textPercent() / 100.0));
    // Crisp, unsmoothed text is part of the look, and it is cheaper to draw.
    font.setStyleStrategy(QFont::NoAntialias);
    font.setHintingPreference(QFont::PreferFullHinting);
    return font;
}

} // namespace

void installAppearance()
{
    QApplication::setStyle(new Style(chosenScheme(), Settings::instance().textPercent() / 100.0));
    applyAppearance();
}

void applyAppearance()
{
    auto* style = static_cast<Style*>(QApplication::style());
    style->setScheme(chosenScheme());
    style->setScale(Settings::instance().textPercent() / 100.0);
    QApplication::setPalette(style->standardPalette());
    QApplication::setFont(chosenFont());
    for (QWidget* widget : QApplication::allWidgets()) {
        widget->updateGeometry();
        widget->update();
    }
}

} // namespace fg
