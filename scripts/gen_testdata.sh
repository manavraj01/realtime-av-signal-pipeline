#!/usr/bin/env bash
# Regenerates the synthetic test clips used to exercise the pipeline,
# so large media files don't need to live in git.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p testdata

ffmpeg -y -f lavfi -i "testsrc=size=1280x720:rate=30:duration=10" \
       -f lavfi -i "sine=frequency=440:duration=10" \
       -c:v libx264 -pix_fmt yuv420p -c:a aac testdata/sample.mp4

# NV12 (semi-planar) clip: exercises the plane-count edge case documented
# in README.md / src/main.cpp (write_frame_planes).
ffmpeg -y -f lavfi -i "testsrc=size=640x360:rate=30:duration=3" \
       -pix_fmt nv12 -c:v rawvideo testdata/sample_nv12.mkv
