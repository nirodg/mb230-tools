# mb230-gateway — cross-build the Open TV launcher for the Vestel 17MB230.
# The MB230 has no DRM/evdev: this port swaps in its own gateway backends.
# Upstream launcher/toolkit sources are untouched (submodule
# ../open-tv-launcher).
#
# Display backends (device.conf [display] backend):
#   gles2  (default) — Mali-G51 via vendor libmali EGL/GLES (GPU present)
#   dfb              — DirectFB sawman sidecar window (CPU upload)
# Select at build time with DISPLAY=gles2|dfb.
#
# Video: media.c is built against the VENDOR's Opera ffmpeg 4.0
# (headers from ffmpeg 4.0.2 source, libs copied out of the firmware
# /lib/opera). Point FFMPEG_INC/FFMPEG_LIB at them; at runtime the launcher
# resolves them from /lib/opera (rpath).
#
#   make -f Makefile          (host CROSS can be arm-linux-gnueabihf-)

CROSS ?= arm-linux-gnueabihf-
CC := $(CROSS)gcc
OTV := ../open-tv-launcher/os/src

DISPLAY ?= gles2

FFMPEG_INC ?= /ffmpeg/include
FFMPEG_LIB ?= /ffmpeg/lib
MALI_LIB  ?= /ffmpeg/lib

CFLAGS := -std=c11 -O3 -marm -mfloat-abi=hard -mfpu=neon-vfpv4 \
	  -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations \
	  -I$(OTV)/ui -I$(OTV)/launcher -I$(OTV)/gateway/include -I. \
	  -I$(FFMPEG_INC)
LDLIBS := -lm -ldl -lpthread \
	  -L$(FFMPEG_LIB) -lavformat -lavcodec -lswscale -lavutil \
	  -lssl -lcrypto -lswresample \
	  -Wl,-rpath,/lib/opera
ifeq ($(DISPLAY),gles2)
EGL_INC ?= /eglinc
CFLAGS += -I$(EGL_INC)
LDLIBS += -L$(MALI_LIB) -lmali -Wl,-rpath,/vendor/lib:/lib
endif

UI_SRC := $(wildcard $(OTV)/ui/*.c)
LAUNCHER_SRC := $(wildcard $(OTV)/launcher/*.c)
GW_SRC := $(OTV)/gateway/time.c $(OTV)/gateway/device.c \
	  display_$(DISPLAY).c input_uart.c

all: otv-launcher

otv-launcher: $(UI_SRC) $(LAUNCHER_SRC) $(GW_SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

check: otv-launcher

clean:
	rm -f otv-launcher *.o

.PHONY: all check clean
