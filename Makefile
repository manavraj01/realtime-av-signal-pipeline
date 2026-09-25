# Build for Linux (primary target): requires libavformat/libavcodec/
# libavutil/libswresample dev packages, e.g.:
#   sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswresample-dev
#
# On systems without pkg-config entries for FFmpeg (e.g. a vendored
# Windows/MSYS2 build), point FFMPEG_DEV at the SDK root (containing
# include/ and lib/) instead:
#   make FFMPEG_DEV=/path/to/ffmpeg-dev

CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Iinclude
LDFLAGS ?=

SRC := src/main.cpp src/fft.cpp src/audio_level.cpp src/video_effect.cpp
BIN := av_pipeline

ifdef FFMPEG_DEV
CXXFLAGS += -I$(FFMPEG_DEV)/include
LDFLAGS  += -L$(FFMPEG_DEV)/lib -lavformat -lavcodec -lavutil -lswresample
else
CXXFLAGS += $(shell pkg-config --cflags libavformat libavcodec libavutil libswresample)
LDFLAGS  += $(shell pkg-config --libs libavformat libavcodec libavutil libswresample)
endif

.PHONY: all clean testdata run

all: $(BIN)

$(BIN): $(SRC) include/*.hpp
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN) $(LDFLAGS)

# Generates a short synthetic test clip (color bars + sine tone) so the
# pipeline can be exercised without needing external sample media.
testdata:
	ffmpeg -y -f lavfi -i testsrc=size=1280x720:rate=30:duration=10 \
	       -f lavfi -i sine=frequency=440:duration=10 \
	       -c:v libx264 -pix_fmt yuv420p -c:a aac testdata/sample.mp4

run: $(BIN)
	./$(BIN) testdata/sample.mp4 testdata/output.yuv

clean:
	rm -f $(BIN) $(BIN).exe
