#pragma once
#include <vector>
#include <cstddef>
#include "fft.hpp"

namespace dsp {

// Computes an FFT-based audio level (dBFS) over fixed-size windows of
// mono float samples. Applies a Hann window before transforming, then
// sums bin energy to get an RMS-equivalent level in the frequency domain
// (Parseval's theorem: energy is conserved between time and frequency
// domain, so this agrees with a direct time-domain RMS calculation).
class AudioLevelMeter {
public:
    explicit AudioLevelMeter(std::size_t window_size = 1024);

    // Feed interleaved-or-mono float samples; internally buffers until a
    // full window is available, then returns true and fills out `db`
    // with the metered level for that window (dBFS, <= 0).
    bool process(const float* samples, std::size_t count, float& db_out);

    std::size_t window_size() const { return window_size_; }

private:
    std::size_t window_size_;
    std::vector<float> buffer_;
    std::vector<float> hann_;
};

} // namespace dsp
