#include "format.h"

#include "settings.h"

#include <QLocale>

namespace fg {

QString Format::why(Availability state)
{
    switch (state) {
    case Availability::NotYetSampled: return tr("Measuring...");
    case Availability::Unsupported: return tr("Not supported");
    case Availability::AccessDenied: return tr("Access denied");
    default: return {};
    }
}

QString Format::bytes(std::uint64_t value)
{
    static const char* const units[] = {QT_TR_NOOP("%1 B"), QT_TR_NOOP("%1 KB"), QT_TR_NOOP("%1 MB"), QT_TR_NOOP("%1 GB"), QT_TR_NOOP("%1 TB"), QT_TR_NOOP("%1 PB")};
    double scaled = static_cast<double>(value);
    int unit = 0;
    while (scaled >= 1024.0 && unit < 5) {
        scaled /= 1024.0;
        ++unit;
    }
    const int decimals = unit == 0 ? 0 : (scaled < 10 ? 2 : (scaled < 100 ? 1 : 0));
    return tr(units[unit]).arg(QLocale().toString(scaled, 'f', decimals));
}

QString Format::rate(double bytesPerSecond)
{
    if (!Settings::instance().networkBits())
        return diskRate(bytesPerSecond);
    static const char* const units[] = {QT_TR_NOOP("%1 bit/s"), QT_TR_NOOP("%1 kbit/s"), QT_TR_NOOP("%1 Mbit/s"), QT_TR_NOOP("%1 Gbit/s")};
    double scaled = bytesPerSecond * 8.0;
    int unit = 0;
    while (scaled >= 1000.0 && unit < 3) {
        scaled /= 1000.0;
        ++unit;
    }
    return tr(units[unit]).arg(QLocale().toString(scaled, 'f', unit == 0 ? 0 : 1));
}

QString Format::diskRate(double bytesPerSecond)
{
    return tr("%1/s").arg(bytes(static_cast<std::uint64_t>(bytesPerSecond < 0 ? 0 : bytesPerSecond)));
}

QString Format::percent(double value)
{
    return tr("%1%").arg(QLocale().toString(value, 'f', value < 10 ? 1 : 0));
}

QString Format::temperature(double celsius)
{
    if (Settings::instance().fahrenheit())
        return tr("%1 °F").arg(QLocale().toString(celsius * 9.0 / 5.0 + 32.0, 'f', 0));
    return tr("%1 °C").arg(QLocale().toString(celsius, 'f', 0));
}

QString Format::temperatureDifference(double celsius)
{
    if (Settings::instance().fahrenheit())
        return tr("%1 °F").arg(QLocale().toString(celsius * 9.0 / 5.0, 'f', 0));
    return tr("%1 °C").arg(QLocale().toString(celsius, 'f', 0));
}

QString Format::megahertz(double value)
{
    if (value >= 1000.0)
        return tr("%1 GHz").arg(QLocale().toString(value / 1000.0, 'f', 2));
    return tr("%1 MHz").arg(QLocale().toString(value, 'f', 0));
}

QString Format::watts(double value)
{
    return tr("%1 W").arg(QLocale().toString(value, 'f', 1));
}

} // namespace fg
