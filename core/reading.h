// A value together with how we came to know it. The UI must show the state, so
// a sensor we could not read never appears as a zero.
#pragma once

#include <utility>

namespace fg {

enum class Availability {
    NotYetSampled, // rates need two samples; nothing is known yet
    Measured,      // read directly from the system
    Estimated,     // derived from other readings, shown as such
    Unsupported,   // this system does not offer the sensor
    AccessDenied,  // the sensor exists but we may not read it
};

template <class T>
struct Reading {
    T value{};
    Availability state = Availability::NotYetSampled;

    static Reading measured(T v) { return {std::move(v), Availability::Measured}; }
    static Reading estimated(T v) { return {std::move(v), Availability::Estimated}; }
    static Reading missing(Availability why) { return {T{}, why}; }

    bool known() const { return state == Availability::Measured || state == Availability::Estimated; }
};

} // namespace fg
