#include "options.h"

#include "../settings.h"
#include "../style/scheme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QVBoxLayout>

namespace fg {

OptionsDialog::OptionsDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    Settings& settings = Settings::instance();
    auto* layout = new QVBoxLayout(this);

    auto* appearance = new QGroupBox(tr("Appearance"), this);
    auto* appearanceForm = new QFormLayout(appearance);

    const Scheme& current = schemeById(settings.scheme());
    auto* theme = new QComboBox(appearance);
    for (const QString& family : schemeFamilies())
        theme->addItem(QCoreApplication::translate("fg::Scheme", schemeFor(family, false).name), family);
    theme->setCurrentIndex(theme->findData(current.family));
    appearanceForm->addRow(tr("&Theme:"), theme);

    auto* mode = new QComboBox(appearance);
    mode->addItem(tr("Light"), false);
    mode->addItem(tr("Dark"), true);
    mode->setCurrentIndex(current.isDark ? 1 : 0);
    appearanceForm->addRow(tr("&Mode:"), mode);

    const auto saveTheme = [theme, mode, &settings] { settings.setValue("appearance/scheme", schemeFor(theme->currentData().toString(), mode->currentData().toBool()).id); };
    connect(theme, &QComboBox::currentIndexChanged, this, saveTheme);
    connect(mode, &QComboBox::currentIndexChanged, this, saveTheme);

    auto* saturation = new QSpinBox(appearance);
    saturation->setRange(0, 10);
    saturation->setValue(settings.saturation());
    connect(saturation, &QSpinBox::valueChanged, this, [&settings](int value) { settings.setValue("appearance/saturation", value); });
    appearanceForm->addRow(tr("Colour s&aturation:"), saturation);

    auto* text = new QComboBox(appearance);
    for (const int percent : {100, 125, 150, 175, 200})
        text->addItem(tr("%1%").arg(percent), percent);
    text->setCurrentIndex(text->findData(settings.textPercent()));
    connect(text, &QComboBox::currentIndexChanged, this, [text, &settings] { settings.setValue("appearance/textPercent", text->currentData()); });
    appearanceForm->addRow(tr("&Text size:"), text);

    auto* follow = new QCheckBox(tr("&Follow the system's light, dark and high contrast settings"), appearance);
    follow->setChecked(settings.followSystem());
    connect(follow, &QCheckBox::toggled, this, [&settings](bool on) { settings.setValue("appearance/followSystem", on); });
    appearanceForm->addRow(follow);
    layout->addWidget(appearance);

    auto* general = new QGroupBox(tr("General"), this);
    auto* generalForm = new QFormLayout(general);

    auto* language = new QComboBox(general);
    language->addItem(tr("System default"), QString());
    language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    language->addItem(QStringLiteral("Norsk"), QStringLiteral("nb"));
    language->setCurrentIndex(language->findData(settings.language()));
    connect(language, &QComboBox::currentIndexChanged, this, [language, &settings] { settings.setValue("app/language", language->currentData()); });
    generalForm->addRow(tr("&Language (after restart):"), language);

    auto* units = new QComboBox(general);
    units->addItem(tr("Celsius"), false);
    units->addItem(tr("Fahrenheit"), true);
    units->setCurrentIndex(settings.fahrenheit() ? 1 : 0);
    connect(units, &QComboBox::currentIndexChanged, this, [units, &settings] { settings.setValue("app/fahrenheit", units->currentData()); });
    generalForm->addRow(tr("Te&mperature unit:"), units);

    auto* rates = new QComboBox(general);
    rates->addItem(tr("Bytes per second"), false);
    rates->addItem(tr("Bits per second"), true);
    rates->setCurrentIndex(settings.networkBits() ? 1 : 0);
    connect(rates, &QComboBox::currentIndexChanged, this, [rates, &settings] { settings.setValue("app/networkBits", rates->currentData()); });
    generalForm->addRow(tr("&Network rates:"), rates);

    const auto addCheck = [&](const QString& label, const char* key, bool value) {
        auto* box = new QCheckBox(label, general);
        box->setChecked(value);
        connect(box, &QCheckBox::toggled, this, [key, &settings](bool on) { settings.setValue(key, on); });
        generalForm->addRow(box);
    };
    addCheck(tr("&Highlight process rows"), "app/rowHighlight", settings.rowHighlight());
    addCheck(tr("&Close to the notification area"), "app/closeToTray", settings.closeToTray());
    addCheck(tr("Show this app's &own cost"), "app/selfMonitor", settings.selfMonitor());
    addCheck(tr("Show a CPU &meter in the notification area"), "app/trayMeter", settings.value("app/trayMeter", false).toBool());
    layout->addWidget(general);

    auto* alerts = new QGroupBox(tr("Alerts"), this);
    auto* alertsForm = new QFormLayout(alerts);
    const auto addAlert = [&](const QString& label, const char* key, const QString& suffix, int top) {
        auto* box = new QSpinBox(alerts);
        box->setRange(0, top);
        box->setSuffix(suffix);
        box->setSpecialValueText(tr("Off"));
        box->setValue(settings.value(QLatin1String(key), 0).toInt());
        connect(box, &QSpinBox::valueChanged, this, [key, &settings](int value) { settings.setValue(QLatin1String(key), value); });
        alertsForm->addRow(label, box);
    };
    addAlert(tr("CPU &use above:"), "alerts/cpuPercent", tr("%"), 100);
    addAlert(tr("Memory us&e above:"), "alerts/memoryPercent", tr("%"), 100);
    addAlert(tr("Tem&perature above:"), "alerts/temperatureC", tr(" \u00b0C"), 150);
    layout->addWidget(alerts);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

} // namespace fg
