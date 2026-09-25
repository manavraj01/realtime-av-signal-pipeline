# Builds and runs the pipeline on real Linux, matching the resume's
# stated environment — useful on a machine (like Windows) that can't
# install libavformat-dev/gdb/perf natively.
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential pkg-config make \
    libavformat-dev libavcodec-dev libavutil-dev libswresample-dev \
    ffmpeg gdb linux-perf \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
RUN make

CMD ["bash"]
