# Cross-compile OpenTV MB230 port for ARMv7 (Vestel 17MB230 / Linux 4.9).
# Preferred: Docker build (no host toolchain required).
# Dynamic linking (-ldl) required for DirectFB/NDAL dlopen backends.

CROSS_COMPILE ?= arm-linux-gnueabihf-
CC      := $(CROSS_COMPILE)gcc
ARMFLAGS := -O2 -marm -mfloat-abi=hard
LDFLAGS ?= -ldl

TARGET := otv_port
SRC    := src/otv_port.c
KEEPER := ndal_wdt_keep
KEEPER_SRC := src/ndal_wdt_keep.c

all: $(TARGET) $(KEEPER)

$(TARGET): $(SRC)
	$(CC) $(ARMFLAGS) $(SRC) -o $@ $(LDFLAGS)
	$(CROSS_COMPILE)strip $@ 2>/dev/null || true
	@file $@ | grep -q ARM && echo "[ok] $(TARGET) (ARM dynamic)" || echo "[!] check arch"

$(KEEPER): $(KEEPER_SRC)
	$(CC) $(ARMFLAGS) $(KEEPER_SRC) -o $@ $(LDFLAGS)
	$(CROSS_COMPILE)strip $@ 2>/dev/null || true

docker:
	./build-docker.sh

clean:
	rm -f $(TARGET) $(KEEPER)

.PHONY: all docker clean
