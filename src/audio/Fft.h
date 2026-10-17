#pragma once

#include <complex>
#include <vector>

namespace lb
{

// In-place radix-2 complex FFT. Small and dependency-free; fast enough for the
// 512-point frames the onset detector uses.
class Fft
{
public:
    // Not real-time safe. size must be a power of two.
    void prepare(int size);
    int size() const noexcept { return size_; }

    // Real-time safe. Forward transform of `size()` complex values, in place.
    void forward(std::complex<float>* data) const noexcept;

private:
    int size_ = 0;
    std::vector<std::complex<float>> twiddles_;
    std::vector<int> bitReversed_;
};

} // namespace lb
