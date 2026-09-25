#include "video_effect.hpp"
#include <algorithm>
#include <cstdint>

namespace dsp {

void apply_gain_effect(AVFrame* frame, float gain) {
    if (!frame || !frame->data[0]) return;
    const int width = frame->width;
    const int height = frame->height;
    const int stride = frame->linesize[0];
    uint8_t* y_plane = frame->data[0];

    for (int row = 0; row < height; ++row) {
        uint8_t* line = y_plane + static_cast<std::size_t>(row) * stride;
        for (int col = 0; col < width; ++col) {
            float v = static_cast<float>(line[col]) * gain;
            line[col] = static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f));
        }
    }
}

} // namespace dsp
