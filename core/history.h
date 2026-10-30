// Fixed-size history of samples. Memory is allocated once. A sample we do not
// have is stored as NaN and drawn as a gap, never interpolated.
#pragma once

#include "reading.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace fg {

class History {
public:
    explicit History(std::size_t capacity = 600) : m_samples(capacity, gap()) {}

    static constexpr float gap() { return std::numeric_limits<float>::quiet_NaN(); }

    void push(float value)
    {
        m_samples[m_next] = value;
        m_next = (m_next + 1) % m_samples.size();
        if (m_size < m_samples.size())
            ++m_size;
    }

    template <class T>
    void push(const Reading<T>& r) { push(r.known() ? static_cast<float>(r.value) : gap()); }

    std::size_t capacity() const { return m_samples.size(); }
    std::size_t size() const { return m_size; }

    // 0 is the oldest sample held, size() - 1 the newest.
    float at(std::size_t index) const
    {
        const std::size_t oldest = (m_next + m_samples.size() - m_size) % m_samples.size();
        return m_samples[(oldest + index) % m_samples.size()];
    }

    float latest() const { return m_size ? at(m_size - 1) : gap(); }

private:
    std::vector<float> m_samples;
    std::size_t m_next = 0;
    std::size_t m_size = 0;
};

} // namespace fg
