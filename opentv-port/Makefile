# Cross-compile OpenTV MB230 port for ARMv7 (Vestel 17MB230 / Linux 4.9).
# Preferred: Docker build (no host toolchain required).

CROSS_COMPILE ?= arm-linux-gnueabihf-
CC      := $(CROSS_COMPILE)gcc
ARMFLAGS := -O2 -marm -mfloat-abi=hard
STATIC  ?= -static

TARGET := otv_port
SRC    := src/otv_port.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(ARMFLAGS) $(STATIC) $(SRC) -o $@
	$(CROSS_COMPILE)strip $@ 2>/dev/null || true
	@file $@ | grep -q ARM && echo "[ok] $(TARGET) (ARM$(if $(STATIC), static,))" || echo "[!] check arch"

docker:
	./build-docker.sh

clean:
	rm -f $(TARGET)

.PHONY: all docker clean
