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
MEMPATCH := mempatch
MEMPATCH_SRC := src/mempatch.c
SEND_CMD := send_cmd
SEND_CMD_SRC := src/send_cmd.c
SEND_KEY := send_key
SEND_KEY_SRC := src/send_key.c

all: $(TARGET) $(KEEPER) $(MEMPATCH) $(SEND_CMD) $(SEND_KEY)

$(TARGET): $(SRC)
	$(CC) $(ARMFLAGS) $(SRC) -o $@ $(LDFLAGS)
	$(CROSS_COMPILE)strip $@ 2>/dev/null || true
	@file $@ | grep -q ARM && echo "[ok] $(TARGET) (ARM dynamic)" || echo "[!] check arch"

$(KEEPER): $(KEEPER_SRC)
	$(CC) $(ARMFLAGS) $(KEEPER_SRC) -o $@ $(LDFLAGS)
	$(CROSS_COMPILE)strip $@ 2>/dev/null || true

$(MEMPATCH): $(MEMPATCH_SRC)
	$(CC) $(ARMFLAGS) -static $(MEMPATCH_SRC) -o $@
	$(CROSS_COMPILE)strip $@ 2>/dev/null || true

$(SEND_CMD): $(SEND_CMD_SRC)
	$(CC) $(ARMFLAGS) $(SEND_CMD_SRC) -o $@
	$(CROSS_COMPILE)strip $@ 2>/dev/null || true

$(SEND_KEY): $(SEND_KEY_SRC)
	$(CC) $(ARMFLAGS) $(SEND_KEY_SRC) -o $@
	$(CROSS_COMPILE)strip $@ 2>/dev/null || true

docker:
	./build-docker.sh

clean:
	rm -f $(TARGET) $(KEEPER) $(MEMPATCH) $(SEND_CMD) $(SEND_KEY)

.PHONY: all docker clean
