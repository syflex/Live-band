#include "audio/Fft.h"

#include <cmath>
#include <numbers>
#include <utility>

namespace lb
{

void Fft::prepare(int size)
{
    size_ = size;

    int bits = 0;
    while ((1 << bits) < size)
        ++bits;

    bitReversed_.resize(static_cast<std::size_t>(size));
    for (int i = 0; i < size; ++i)
    {
        int reversed = 0;
        for (int b = 0; b < bits; ++b)
            if ((i >> b) & 1)
                reversed |= 1 << (bits - 1 - b);
        bitReversed_[static_cast<std::size_t>(i)] = reversed;
    }

    twiddles_.resize(static_cast<std::size_t>(size / 2));
    for (int k = 0; k < size / 2; ++k)
    {
        const double angle = -2.0 * std::numbers::pi * k / size;
        twiddles_[static_cast<std::size_t>(k)] = { static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)) };
    }
}

void Fft::forward(std::complex<float>* data) const noexcept
{
    const int n = size_;

    for (int i = 0; i < n; ++i)
    {
        const int j = bitReversed_[static_cast<std::size_t>(i)];
        if (j > i)
            std::swap(data[i], data[j]);
    }

    for (int length = 2; length <= n; length <<= 1)
    {
        const int half = length / 2;
        const int step = n / length;
        for (int start = 0; start < n; start += length)
        {
            for (int k = 0; k < half; ++k)
            {
                const auto w = twiddles_[static_cast<std::size_t>(k * step)];
                const auto u = data[start + k];
                const auto v = data[start + k + half];
                // Plain multiply, avoiding the NaN-checking path of std::complex operator*.
                const std::complex<float> t { v.real() * w.real() - v.imag() * w.imag(),
                                              v.real() * w.imag() + v.imag() * w.real() };
                data[start + k] = u + t;
                data[start + k + half] = u - t;
            }
        }
    }
}

} // namespace lb
