#include "audio_level.hpp"
#include <cmath>
#include <algorithm>

namespace dsp {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

AudioLevelMeter::AudioLevelMeter(std::size_t window_size)
    : window_size_(next_pow2(window_size)), hann_(window_size_) {
    buffer_.reserve(window_size_);
    for (std::size_t i = 0; i < window_size_; ++i) {
        hann_[i] = 0.5f * (1.0f - std::cos(2.0f * kPi * i /
                                            static_cast<float>(window_size_ - 1)));
    }
}

bool AudioLevelMeter::process(const float* samples, std::size_t count, float& db_out) {
    buffer_.insert(buffer_.end(), samples, samples + count);
    if (buffer_.size() < window_size_) return false;

    std::vector<Complex> spectrum(window_size_);
    for (std::size_t i = 0; i < window_size_; ++i) {
        spectrum[i] = Complex(buffer_[i] * hann_[i], 0.0f);
    }
    fft(spectrum);

    double energy = 0.0;
    for (std::size_t i = 0; i < window_size_; ++i) {
        energy += std::norm(static_cast<std::complex<double>>(spectrum[i]));
    }
    // Parseval: mean-square value in time domain == sum(|X[k]|^2) / N^2.
    double mean_square = energy / (static_cast<double>(window_size_) * window_size_);
    double rms = std::sqrt(std::max(mean_square, 1e-12));
    db_out = static_cast<float>(20.0 * std::log10(rms));

    // Slide the window forward by half its length (50% overlap) so we
    // still get a metering update every ~half-window of new audio.
    std::size_t hop = window_size_ / 2;
    buffer_.erase(buffer_.begin(), buffer_.begin() + hop);
    return true;
}

} // namespace dsp
