#include "appearance.h"

#include "../settings.h"
#include "style.h"

#include <QApplication>
#include <QFont>
#include <QGuiApplication>
#include <QHeaderView>
#include <QStyleHints>
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
#include <QAccessibilityHints>
#endif
#include <QWidget>

namespace fg {

namespace {

constexpr double kBasePointSize = 10.0;
constexpr double kMetricsAtFullSize = 1.2; // bevels, buttons and bars in step with that text

double metricScale()
{
    return Settings::instance().textPercent() / 100.0 * kMetricsAtFullSize;
}

Scheme chosenScheme()
{
    const Settings& settings = Settings::instance();
    QString id = settings.scheme();
    if (settings.followSystem()) {
        id = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark ? QStringLiteral("dark") : QStringLiteral("standard");
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        if (QGuiApplication::styleHints()->accessibility()->contrastPreference() == Qt::ContrastPreference::HighContrast)
            id = QStringLiteral("contrast");
#endif
    }
    return withSaturation(schemeById(id), settings.saturation());
}

QFont chosenFont()
{
    QFont font;
    font.setFamilies({QStringLiteral("Liberation Sans"), QStringLiteral("Nimbus Sans"), QStringLiteral("DejaVu Sans"), QStringLiteral("sans-serif")});
    // In points, so text keeps its real size on every screen and scale.
    font.setPointSizeF(kBasePointSize * Settings::instance().textPercent() / 100.0);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

} // namespace

void installAppearance()
{
    QApplication::setStyle(new Style(chosenScheme(), metricScale()));
    applyAppearance();
}

void applyAppearance()
{
    auto* style = static_cast<Style*>(QApplication::style());
    const double oldScale = style->scale();
    const double newScale = metricScale();
    style->setScheme(chosenScheme());
    style->setScale(newScale);
    QApplication::setPalette(style->standardPalette());
    QApplication::setFont(chosenFont());
    for (QWidget* widget : QApplication::allWidgets()) {
        // Columns were sized for the old text, and would crop the new.
        if (auto* header = qobject_cast<QHeaderView*>(widget); header && newScale != oldScale && header->orientation() == Qt::Horizontal)
            for (int section = 0; section < header->count(); ++section)
                if (!header->isSectionHidden(section))
                    header->resizeSection(section, qRound(header->sectionSize(section) * newScale / oldScale));
        widget->updateGeometry();
        widget->update();
    }
}

} // namespace fg
