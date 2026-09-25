# Real-Time Audio/Video Signal Processing Pipeline

A C++ pipeline that decodes video with FFmpeg (libavformat/libavcodec),
applies a per-frame signal-level (gain) effect to the luma plane, and runs
an FFT-based audio level meter over the decoded audio track. Per-frame
latency is measured with `clock_gettime` to report sustained FPS and the
slowest pipeline stage.

## What it does

1. Demuxes and decodes the best video + audio streams from an input file.
2. For each video frame: applies a gain adjustment to the Y (luma) plane,
   then writes the frame out as tightly-packed planar YUV.
3. For the audio track: resamples to mono float via `libswresample`, then
   feeds fixed-size windows (Hann-windowed, 50% overlap) through a
   hand-written radix-2 Cooley-Tukey FFT to compute an RMS-equivalent
   level in dBFS (via Parseval's theorem — energy is conserved between
   time and frequency domain).
4. Times every stage (`decode_video`, `apply_effect`, `write_output`,
   `decode_audio`, `resample_audio`, `meter_audio`) with
   `clock_gettime(CLOCK_MONOTONIC)` and reports sustained FPS plus the
   slowest stage at the end of the run.

## Build

Primary target is Linux:

```sh
sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswresample-dev
make
```

On a machine without pkg-config entries for FFmpeg (e.g. a vendored
Windows/MSYS2 SDK), point `FFMPEG_DEV` at the SDK root instead:

```sh
make FFMPEG_DEV=/path/to/ffmpeg-dev
```

## Run

```sh
scripts/gen_testdata.sh          # generates testdata/sample.mp4 (and an NV12 clip)
./av_pipeline testdata/sample.mp4 testdata/output.yuv 1.2
```

Sample output from a 1280x720/30fps, 10s synthetic clip (color bars + a
440Hz tone), measured on this machine:

```
== av_pipeline report ==
input:            testdata/sample.mp4
video frames:     298
audio windows:    431
last audio level: -25.34 dBFS
elapsed:          1.292 s
sustained FPS:    230.60
avg decode_video: 0.662 ms/frame
avg apply_effect: 1.190 ms/frame
avg write_output: 2.370 ms/frame
slowest stage:    write_output (2.370 ms/frame avg)
```

The slowest stage is disk I/O for the raw YUV output, not decode or the
per-pixel effect — as expected, since `write_output` does an uncompressed
frame write while `apply_effect` only touches the Y plane in place. On
Linux this is where `perf record`/`perf report` would point next: sampling
`write_frame_planes` and the underlying `fwrite`/page-cache path to decide
whether to buffer writes or switch to `pwritev`.

## A real bug, found with gdb

`write_frame_planes` originally iterated `desc->nb_components` physical
planes assuming one plane per color component. That holds for fully
planar formats (e.g. `yuv420p`, 3 separate Y/U/V planes) but not for
semi-planar formats like `nv12`, where U and V are interleaved into a
single second plane.

Reproduced by decoding an NV12 clip and breaking on `write_frame_planes`:

```
(gdb) break write_frame_planes
(gdb) run testdata/sample_nv12.mkv testdata/output_nv12.yuv 1.0 3
(gdb) print frame->data[2]
$1 = (uint8_t *) 0x0
(gdb) print frame->linesize[2]
$2 = 0
```

`frame->data[2]`/`linesize[2]` are null/zero for NV12 — the loop's
component count (3) doesn't match its physical plane count (2). The fix
(`src/main.cpp`, `write_frame_planes`) guards each plane on both
`frame->data[plane]` and `frame->linesize[plane]` before touching it, so
semi-planar formats degrade to writing only their real planes instead of
dereferencing a null pointer.

## Layout

```
include/   fft.hpp, audio_level.hpp, video_effect.hpp, latency_timer.hpp
src/       fft.cpp, audio_level.cpp, video_effect.cpp, main.cpp
scripts/   gen_testdata.sh — regenerates synthetic test clips
testdata/  sample.mp4 (checked in; NV12 clip regenerated on demand)
```
