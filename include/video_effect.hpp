#pragma once

extern "C" {
#include <libavutil/frame.h>
}

namespace dsp {

// Applies a simple signal-level gain adjustment to the luma (Y) plane of
// a YUV4:2:0 frame, in place. `gain` > 1.0 brightens, < 1.0 darkens.
// This stands in for the "signal-level effect" applied frame-by-frame
// in the pipeline (e.g. exposure/gain correction on incoming video).
void apply_gain_effect(AVFrame* frame, float gain);

} // namespace dsp
