# mb230-gateway — cross-build the Open TV launcher for the Vestel 17MB230.
# The MB230 has no DRM/evdev: this port swaps in its own gateway backends
# (DirectFB display + UART-IR input) and links against the vendor glibc 2.23.
# Upstream launcher/toolkit sources are untouched (submodule
# ../open-tv-launcher).
#
# Video: media.c is built against the VENDOR's Opera ffmpeg 4.0
# (libavcodec.so.58.18.100 — headers from ffmpeg 4.0.2 source, libs copied
# out of the firmware /lib/opera). Point FFMPEG_INC/FFMPEG_LIB at them;
# at runtime the launcher resolves them from /lib/opera (rpath).
#
#   make -f Makefile          (host CROSS can be arm-linux-gnueabihf-)

CROSS ?= arm-linux-gnueabihf-
CC := $(CROSS)gcc
OTV := ../open-tv-launcher/os/src

FFMPEG_INC ?= /ffmpeg/include
FFMPEG_LIB ?= /ffmpeg/lib

CFLAGS := -std=c11 -O3 -marm -mfloat-abi=hard -mfpu=neon-vfpv4 \
	  -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations \
	  -I$(OTV)/ui -I$(OTV)/launcher -I$(OTV)/gateway/include -I. \
	  -I$(FFMPEG_INC)
LDLIBS := -lm -ldl -lpthread \
	  -L$(FFMPEG_LIB) -lavformat -lavcodec -lswscale -lavutil -lssl -lcrypto -lswresample \
	  -Wl,-rpath,/lib/opera

UI_SRC := $(wildcard $(OTV)/ui/*.c)
LAUNCHER_SRC := $(wildcard $(OTV)/launcher/*.c)
GW_SRC := $(OTV)/gateway/time.c $(OTV)/gateway/device.c \
	  display_dfb.c input_uart.c

all: otv-launcher

otv-launcher: $(UI_SRC) $(LAUNCHER_SRC) $(GW_SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

check: otv-launcher

clean:
	rm -f otv-launcher *.o

.PHONY: all check clean
