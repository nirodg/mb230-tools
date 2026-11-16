# mb230-gateway — cross-build the Open TV launcher for the Vestel 17MB230.
# The MB230 has no DRM/evdev: this port swaps in its own gateway backends
# (DirectFB display + UART-IR input) and links against the vendor glibc 2.23.
# Upstream launcher/toolkit sources are untouched (submodule
# ../open-tv-launcher); ui/media.c is replaced at BUILD time by media_stub.c
# until R4 wires the vendor ffmpeg.
#
#   make -f Makefile          (host CROSS can be arm-linux-gnueabihf-)

CROSS ?= arm-linux-gnueabihf-
CC := $(CROSS)gcc
OTV := ../open-tv-launcher/os/src

CFLAGS := -std=c11 -O3 -marm -mfloat-abi=hard -mfpu=neon-vfpv4 \
	  -Wall -Wextra -Wno-unused-parameter \
	  -I$(OTV)/ui -I$(OTV)/launcher -I$(OTV)/gateway/include -I.
LDLIBS := -lm -ldl -lpthread

UI_SRC := $(filter-out $(OTV)/ui/media.c, $(wildcard $(OTV)/ui/*.c))
LAUNCHER_SRC := $(wildcard $(OTV)/launcher/*.c)
GW_SRC := $(OTV)/gateway/time.c $(OTV)/gateway/device.c \
	  display_dfb.c input_uart.c media_stub.c

all: otv-launcher

otv-launcher: $(UI_SRC) $(LAUNCHER_SRC) $(GW_SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

check: otv-launcher

clean:
	rm -f otv-launcher *.o

.PHONY: all check clean
