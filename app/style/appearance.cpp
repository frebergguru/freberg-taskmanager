#include "appearance.h"

#include "../settings.h"
#include "style.h"

#include <QApplication>
#include <QEvent>
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
    const Scheme* scheme = &schemeById(settings.scheme());
    if (settings.followSystem()) {
        // The system decides light or dark, and the chosen theme stays.
        scheme = &schemeFor(scheme->family, QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        if (QGuiApplication::styleHints()->accessibility()->contrastPreference() == Qt::ContrastPreference::HighContrast)
            scheme = &schemeFor(QStringLiteral("contrast"), scheme->isDark);
#endif
    }
    return withSaturation(*scheme, settings.saturation());
}

QFont chosenFont()
{
    QFont font;
    if (chosenScheme().design == Design::Classic)
        font.setFamilies({QStringLiteral("Liberation Sans"), QStringLiteral("Nimbus Sans"), QStringLiteral("DejaVu Sans"), QStringLiteral("sans-serif")});
    else
        font.setFamilies({QStringLiteral("Inter"), QStringLiteral("Noto Sans"), QStringLiteral("Roboto"), QStringLiteral("DejaVu Sans"), QStringLiteral("sans-serif")});
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
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, qApp, [] {
        if (Settings::instance().followSystem())
            applyAppearance();
    });
}

void applyAppearance()
{
    auto* style = static_cast<Style*>(QApplication::style());
    const double oldScale = style->scale();
    const Design oldDesign = style->scheme().design;
    const double newScale = metricScale();
    style->setScheme(chosenScheme());
    style->setScale(newScale);
    QApplication::setPalette(style->standardPalette());
    QApplication::setFont(chosenFont());
    const bool designChanged = oldDesign != style->scheme().design;
    for (QWidget* widget : QApplication::allWidgets()) {
        // A different design has different control sizes, and pages may need a new layout.
        if (designChanged) {
            QEvent changed(QEvent::StyleChange);
            QApplication::sendEvent(widget, &changed);
        }
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
