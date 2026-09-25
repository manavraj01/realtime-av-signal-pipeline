#include "fft.hpp"
#include <cmath>

namespace dsp {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

std::size_t next_pow2(std::size_t n) {
    std::size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

static void fft_impl(std::vector<Complex>& a, bool inverse) {
    const std::size_t n = a.size();
    if (n <= 1) return;

    // Bit-reversal permutation.
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }

    // Butterfly stages.
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const float ang = (inverse ? 2.0f : -2.0f) * kPi / static_cast<float>(len);
        const Complex wlen(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            Complex w(1.0f, 0.0f);
            for (std::size_t k = 0; k < len / 2; ++k) {
                Complex u = a[i + k];
                Complex v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse) {
        for (auto& x : a) x /= static_cast<float>(n);
    }
}

void fft(std::vector<Complex>& data) { fft_impl(data, false); }

void ifft(std::vector<Complex>& data) { fft_impl(data, true); }

} // namespace dsp
