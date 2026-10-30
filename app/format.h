// Turns readings into text. Anything we do not know says why, so an unmeasured
// value can never look like a zero.
#pragma once

#include "core/reading.h"

#include <QCoreApplication>
#include <QString>
#include <cstdint>

namespace fg {

class Format {
    Q_DECLARE_TR_FUNCTIONS(fg::Format)
public:
    static QString why(Availability state);
    static QString bytes(std::uint64_t value);
    static QString rate(double bytesPerSecond); // honours the bytes/bits setting
    static QString diskRate(double bytesPerSecond);
    static QString percent(double value);
    static QString temperature(double celsius); // honours the unit setting
    static QString megahertz(double value);
    static QString watts(double value);

    static QString bytes(const Reading<std::uint64_t>& r) { return mark(r, r.known() ? bytes(r.value) : QString()); }
    static QString rate(const Reading<double>& r) { return mark(r, r.known() ? rate(r.value) : QString()); }
    static QString diskRate(const Reading<double>& r) { return mark(r, r.known() ? diskRate(r.value) : QString()); }
    static QString percent(const Reading<double>& r) { return mark(r, r.known() ? percent(r.value) : QString()); }
    static QString temperature(const Reading<double>& r) { return mark(r, r.known() ? temperature(r.value) : QString()); }
    static QString megahertz(const Reading<double>& r) { return mark(r, r.known() ? megahertz(r.value) : QString()); }
    static QString watts(const Reading<double>& r) { return mark(r, r.known() ? watts(r.value) : QString()); }
    static QString text(const Reading<std::string>& r) { return mark(r, r.known() ? QString::fromStdString(r.value) : QString()); }

private:
    template <class T>
    static QString mark(const Reading<T>& r, const QString& shown)
    {
        if (!r.known())
            return why(r.state);
        return r.state == Availability::Estimated ? tr("%1 (estimated)").arg(shown) : shown;
    }
};

} // namespace fg
