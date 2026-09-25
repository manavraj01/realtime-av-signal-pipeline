#pragma once
#include <time.h>
#include <cstdint>
#include <vector>
#include <string>
#include <numeric>
#include <algorithm>
#include <unordered_map>

namespace dsp {

inline int64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

// Tracks named pipeline stages' per-frame durations so we can report
// sustained FPS and identify the slowest stage, as measured with
// clock_gettime(CLOCK_MONOTONIC) at nanosecond resolution.
class StageProfiler {
public:
    void record(const std::string& stage, int64_t duration_ns) {
        totals_[stage] += duration_ns;
        counts_[stage] += 1;
    }

    std::string slowest_stage_report() const {
        std::string best_stage;
        double best_avg_ms = -1.0;
        for (auto& [stage, total] : totals_) {
            double avg_ms = (static_cast<double>(total) / counts_.at(stage)) / 1e6;
            if (avg_ms > best_avg_ms) {
                best_avg_ms = avg_ms;
                best_stage = stage;
            }
        }
        return best_stage + " (" + std::to_string(best_avg_ms) + " ms/frame avg)";
    }

    double avg_ms(const std::string& stage) const {
        auto it = totals_.find(stage);
        if (it == totals_.end()) return 0.0;
        return (static_cast<double>(it->second) / counts_.at(stage)) / 1e6;
    }

private:
    std::unordered_map<std::string, int64_t> totals_;
    std::unordered_map<std::string, int64_t> counts_;
};

// RAII scoped timer that records elapsed time into a StageProfiler on
// destruction.
class ScopedStage {
public:
    ScopedStage(StageProfiler& profiler, std::string stage)
        : profiler_(profiler), stage_(std::move(stage)), start_(now_ns()) {}
    ~ScopedStage() { profiler_.record(stage_, now_ns() - start_); }

private:
    StageProfiler& profiler_;
    std::string stage_;
    int64_t start_;
};

} // namespace dsp
