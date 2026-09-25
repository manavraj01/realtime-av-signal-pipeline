// Real-time-style A/V signal processing pipeline.
//
// Decodes a video file frame by frame with FFmpeg (libavformat/libavcodec),
// applies a per-frame signal-level (gain) effect to the luma plane, and
// separately decodes+resamples the audio track to run an FFT-based level
// meter over it. Per-frame latency is measured with clock_gettime and used
// to report sustained FPS and the slowest pipeline stage.
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/pixdesc.h>
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "video_effect.hpp"
#include "audio_level.hpp"
#include "latency_timer.hpp"

namespace {

struct Args {
    std::string input;
    std::string output_yuv = "output.yuv";
    float gain = 1.15f;
    int max_frames = 0; // 0 = no limit
};

Args parse_args(int argc, char** argv) {
    Args a;
    if (argc < 2) {
        std::fprintf(stderr,
            "usage: av_pipeline <input> [output.yuv] [gain] [max_frames]\n");
        std::exit(1);
    }
    a.input = argv[1];
    if (argc > 2) a.output_yuv = argv[2];
    if (argc > 3) a.gain = std::strtof(argv[3], nullptr);
    if (argc > 4) a.max_frames = std::atoi(argv[4]);
    return a;
}

void die_on_error(int err, const char* what) {
    if (err < 0) {
        char buf[256];
        av_strerror(err, buf, sizeof(buf));
        std::fprintf(stderr, "%s: %s\n", what, buf);
        std::exit(1);
    }
}

// Writes the visible (non-padded) region of each plane, so the result is
// a tightly packed planar YUV file regardless of decoder line stride.
void write_frame_planes(std::FILE* out, AVFrame* frame) {
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
    for (int plane = 0; plane < desc->nb_components; ++plane) {
        int plane_height = frame->height;
        int plane_width_bytes = frame->width;
        if (plane == 1 || plane == 2) {
            plane_height = (frame->height + 1) / 2;
            plane_width_bytes = (frame->width + 1) / 2;
        }
        for (int row = 0; row < plane_height; ++row) {
            std::fwrite(frame->data[plane] + static_cast<std::size_t>(row) * frame->linesize[plane],
                        1, plane_width_bytes, out);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    Args args = parse_args(argc, argv);

    AVFormatContext* fmt_ctx = nullptr;
    die_on_error(avformat_open_input(&fmt_ctx, args.input.c_str(), nullptr, nullptr),
                 "avformat_open_input");
    die_on_error(avformat_find_stream_info(fmt_ctx, nullptr), "avformat_find_stream_info");

    int video_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    int audio_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (video_stream_idx < 0) {
        std::fprintf(stderr, "no video stream found\n");
        return 1;
    }

    // --- Video decoder setup ---
    AVStream* vstream = fmt_ctx->streams[video_stream_idx];
    const AVCodec* vcodec = avcodec_find_decoder(vstream->codecpar->codec_id);
    AVCodecContext* vctx = avcodec_alloc_context3(vcodec);
    avcodec_parameters_to_context(vctx, vstream->codecpar);
    die_on_error(avcodec_open2(vctx, vcodec, nullptr), "avcodec_open2 (video)");

    // --- Audio decoder + resampler setup (optional) ---
    AVCodecContext* actx = nullptr;
    SwrContext* swr = nullptr;
    AVChannelLayout mono_layout;
    av_channel_layout_default(&mono_layout, 1);
    if (audio_stream_idx >= 0) {
        AVStream* astream = fmt_ctx->streams[audio_stream_idx];
        const AVCodec* acodec = avcodec_find_decoder(astream->codecpar->codec_id);
        actx = avcodec_alloc_context3(acodec);
        avcodec_parameters_to_context(actx, astream->codecpar);
        die_on_error(avcodec_open2(actx, acodec, nullptr), "avcodec_open2 (audio)");

        swr_alloc_set_opts2(&swr, &mono_layout, AV_SAMPLE_FMT_FLT, actx->sample_rate,
                             &actx->ch_layout, actx->sample_fmt, actx->sample_rate, 0, nullptr);
        die_on_error(swr_init(swr), "swr_init");
    }

    std::FILE* out = std::fopen(args.output_yuv.c_str(), "wb");
    if (!out) {
        std::fprintf(stderr, "failed to open output file %s\n", args.output_yuv.c_str());
        return 1;
    }

    dsp::AudioLevelMeter level_meter(1024);
    dsp::StageProfiler profiler;

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* audio_frame = av_frame_alloc();

    int64_t video_frames = 0;
    int64_t audio_windows = 0;
    float last_db = 0.0f;
    int64_t pipeline_start = dsp::now_ns();

    std::vector<float> resample_buf;

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
            int64_t t0 = dsp::now_ns();
            if (avcodec_send_packet(vctx, pkt) == 0) {
                while (avcodec_receive_frame(vctx, frame) == 0) {
                    int64_t decode_end = dsp::now_ns();
                    profiler.record("decode_video", decode_end - t0);

                    int64_t effect_start = dsp::now_ns();
                    dsp::apply_gain_effect(frame, args.gain);
                    profiler.record("apply_effect", dsp::now_ns() - effect_start);

                    int64_t write_start = dsp::now_ns();
                    write_frame_planes(out, frame);
                    profiler.record("write_output", dsp::now_ns() - write_start);

                    ++video_frames;
                    if (args.max_frames && video_frames >= args.max_frames) break;
                    t0 = dsp::now_ns();
                }
            }
        } else if (audio_stream_idx >= 0 && pkt->stream_index == audio_stream_idx) {
            int64_t t0 = dsp::now_ns();
            if (avcodec_send_packet(actx, pkt) == 0) {
                while (avcodec_receive_frame(actx, audio_frame) == 0) {
                    profiler.record("decode_audio", dsp::now_ns() - t0);

                    int64_t resample_start = dsp::now_ns();
                    int out_samples = static_cast<int>(
                        av_rescale_rnd(swr_get_delay(swr, actx->sample_rate) + audio_frame->nb_samples,
                                       actx->sample_rate, actx->sample_rate, AV_ROUND_UP));
                    resample_buf.resize(out_samples);
                    uint8_t* out_ptr = reinterpret_cast<uint8_t*>(resample_buf.data());
                    int converted = swr_convert(swr, &out_ptr, out_samples,
                                                 const_cast<const uint8_t**>(audio_frame->data),
                                                 audio_frame->nb_samples);
                    profiler.record("resample_audio", dsp::now_ns() - resample_start);

                    int64_t meter_start = dsp::now_ns();
                    if (converted > 0 && level_meter.process(resample_buf.data(), converted, last_db)) {
                        ++audio_windows;
                    }
                    profiler.record("meter_audio", dsp::now_ns() - meter_start);
                    t0 = dsp::now_ns();
                }
            }
        }
        av_packet_unref(pkt);
        if (args.max_frames && video_frames >= args.max_frames) break;
    }

    int64_t pipeline_end = dsp::now_ns();
    double elapsed_s = (pipeline_end - pipeline_start) / 1e9;
    double fps = elapsed_s > 0 ? video_frames / elapsed_s : 0.0;

    std::printf("== av_pipeline report ==\n");
    std::printf("input:            %s\n", args.input.c_str());
    std::printf("video frames:     %lld\n", static_cast<long long>(video_frames));
    std::printf("audio windows:    %lld\n", static_cast<long long>(audio_windows));
    std::printf("last audio level: %.2f dBFS\n", last_db);
    std::printf("elapsed:          %.3f s\n", elapsed_s);
    std::printf("sustained FPS:    %.2f\n", fps);
    std::printf("avg decode_video: %.3f ms/frame\n", profiler.avg_ms("decode_video"));
    std::printf("avg apply_effect: %.3f ms/frame\n", profiler.avg_ms("apply_effect"));
    std::printf("avg write_output: %.3f ms/frame\n", profiler.avg_ms("write_output"));
    std::printf("slowest stage:    %s\n", profiler.slowest_stage_report().c_str());

    av_frame_free(&frame);
    av_frame_free(&audio_frame);
    av_packet_free(&pkt);
    avcodec_free_context(&vctx);
    if (actx) avcodec_free_context(&actx);
    if (swr) swr_free(&swr);
    av_channel_layout_uninit(&mono_layout);
    avformat_close_input(&fmt_ctx);
    std::fclose(out);
    return 0;
}
