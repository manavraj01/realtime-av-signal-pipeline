#pragma once
#include <complex>
#include <vector>
#include <cstdint>

namespace dsp {

using Complex = std::complex<float>;

// Iterative in-place radix-2 Cooley-Tukey FFT (decimation-in-time).
// `data.size()` must be a power of two. This is the standard textbook
// algorithm: bit-reversal permutation followed by log2(N) butterfly stages.
void fft(std::vector<Complex>& data);

// Inverse FFT via conjugate trick: conj(FFT(conj(x))) / N.
void ifft(std::vector<Complex>& data);

// Returns the next power of two >= n.
std::size_t next_pow2(std::size_t n);

} // namespace dsp
