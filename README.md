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

Or build and run it in a real Linux container (this is what actually
produced the `gdb`/`perf` sections below — no local FFmpeg dev packages,
`gdb`, or `perf` needed on the host):

```sh
docker build -t av-pipeline-linux .
docker run --rm -it --cap-add=SYS_ADMIN --security-opt seccomp=unconfined av-pipeline-linux bash
```
(`SYS_ADMIN` + unconfined seccomp are only needed for `perf record` inside
the container — plain `docker run --rm -it av-pipeline-linux bash` is
enough for building/running/`gdb`.)

## Run

```sh
scripts/gen_testdata.sh          # generates testdata/sample.mp4 (and an NV12 clip)
./av_pipeline testdata/sample.mp4 testdata/output.yuv 1.2
```

Sample output from a 1280x720/30fps, 10s synthetic clip (color bars + a
440Hz tone), measured on this machine (Windows/MinGW build):

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

Same clip, same code, on real Linux (Debian in Docker — see below):

```
== av_pipeline report ==
video frames:     298
audio windows:    431
last audio level: -25.38 dBFS
elapsed:          1.608 s
sustained FPS:    185.35
avg decode_video: 1.379 ms/frame
avg apply_effect: 1.494 ms/frame
avg write_output: 2.262 ms/frame
slowest stage:    write_output (2.262 ms/frame avg)
```

Same slowest stage on both, similar per-stage costs — the container's
shared/virtualized CPU explains the lower FPS, not a platform-specific
code path.

The slowest *wall-clock* stage is disk I/O for the raw YUV output, not
decode or the per-pixel effect — since `write_output` does an
uncompressed frame write while `apply_effect` only touches the Y plane in
place.

## Real `perf` profiling (Linux, in Docker)

Wall-clock time per stage (above) and CPU time per function are different
questions — `write_output` can be the slowest *stage* while spending
almost none of that time actually using the CPU (it's mostly waiting on
the write syscall). Running the real profiler settles which one it is:

```
$ perf record -o perf.data -- ./av_pipeline testdata/sample.mp4 testdata/output.yuv 1.2
$ perf report -i perf.data --stdio
# Overhead  Command      Shared Object            Symbol
    33.59%  av_pipeline  av_pipeline              [.] dsp::apply_gain_effect
     1.46%  av_pipeline  libc.so.6                [.] _IO_fwrite
     1.25%  av_pipeline  libavcodec.so.59.37.100  [.] 0x0000000000a077d3
     0.82%  av_pipeline  av_pipeline              [.] dsp::fft_impl
     0.48%  av_pipeline  libc.so.6                [.] _IO_file_write
```

`apply_gain_effect` — the simple per-pixel gain loop — is the actual CPU
hotspot at ~34% of sampled cycles, an order of magnitude above `fwrite`
itself. The two profiling methods agree, they're just answering different
questions: `write_output` is the slowest *stage* end-to-end (it blocks on
I/O), while `apply_gain_effect` is where the CPU *cycles* actually go.
That's a real, useful distinction `perf`'s cycle-sampling gives you that a
wall-clock stage timer can't — and the fix it points to is different too:
`write_output`'s cost is addressed by buffering/`pwritev` (an I/O change),
`apply_gain_effect`'s is addressed by vectorizing the pixel loop (a CPU
change). (Kernel symbols show as `[unknown]` above — this container's
kernel address maps are restricted, `/proc/sys/kernel/kptr_restrict` — but
every userspace frame that matters resolved fine.)

## A real bug, found with gdb

`write_frame_planes` originally iterated `desc->nb_components` physical
planes assuming one plane per color component. That holds for fully
planar formats (e.g. `yuv420p`, 3 separate Y/U/V planes) but not for
semi-planar formats like `nv12`, where U and V are interleaved into a
single second plane.

Reproduced by decoding an NV12 clip and breaking on `write_frame_planes`,
on real Linux (in the Docker container above — the plane pointers are
identical on Windows/MinGW, but only Linux actually segfaults on it, see
below):

```
(gdb) break write_frame_planes
(gdb) run testdata/sample_nv12.mkv testdata/output_nv12.yuv 1.0 3
(gdb) print frame->data[2]
$1 = (uint8_t *) 0x0
(gdb) print frame->linesize[2]
$2 = 0
(gdb) continue
Program received signal SIGSEGV, Segmentation fault.
(gdb) bt
#0  0x00007ffff6354f09 in ?? () from /lib/x86_64-linux-gnu/libc.so.6
#1  0x00007ffff627258c in _IO_file_xsputn () from /lib/x86_64-linux-gnu/libc.so.6
#2  0x00007ffff6267b63 in fwrite () from /lib/x86_64-linux-gnu/libc.so.6
#3  0x0000555555557991 in write_frame_planes (...) at src/main.cpp:77
#4  0x0000555555557f4d in main (...) at src/main.cpp:157
```

`frame->data[2]`/`linesize[2]` are null/zero for NV12 — the loop's
component count (3) doesn't match its physical plane count (2), and
`fwrite`ing from that null pointer is a real, confirmed segfault (the
backtrace above), not a theoretical one. The fix (`src/main.cpp`,
`write_frame_planes`) guards each plane on both `frame->data[plane]` and
`frame->linesize[plane]` before touching it, so semi-planar formats
degrade to writing only their real planes instead of dereferencing a null
pointer.

## Layout

```
include/   fft.hpp, audio_level.hpp, video_effect.hpp, latency_timer.hpp
src/       fft.cpp, audio_level.cpp, video_effect.cpp, main.cpp
scripts/   gen_testdata.sh — regenerates synthetic test clips
testdata/  sample.mp4 (checked in; NV12 clip regenerated on demand)
```
