#!/usr/bin/env bash
set -euo pipefail

# Build OpenTV MB230 port binaries (ARMv7 hard-float, dynamic) using Docker.
# Dynamic linking is required for dlopen of vendor libs (DirectFB/NDAL).

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="${MB230_DOCKER_IMAGE:-ubuntu:16.04}"
CROSS_CC="${CROSS_CC:-arm-linux-gnueabihf-gcc}"
CROSS_STRIP="${CROSS_STRIP:-arm-linux-gnueabihf-strip}"

cd "$SCRIPT_DIR"

echo "[opentv-port] building in Docker ($IMAGE)..."
docker run --rm \
  -v "$SCRIPT_DIR":/w \
  -w /w \
  "$IMAGE" \
  sh -c "apt-get update -qq && \
         apt-get install -y -qq gcc-arm-linux-gnueabihf file >/dev/null 2>&1 && \
         $CROSS_CC -O2 -marm -mfloat-abi=hard src/otv_port.c -o otv_port -ldl && \
         $CROSS_CC -O2 -marm -mfloat-abi=hard src/ndal_wdt_keep.c -o ndal_wdt_keep -ldl 2>/dev/null || true && \
         $CROSS_STRIP otv_port ndal_wdt_keep 2>/dev/null || true && \
         file otv_port ndal_wdt_keep 2>/dev/null || file otv_port"

chmod +x otv_port ndal_wdt_keep 2>/dev/null || chmod +x otv_port 2>/dev/null || true
echo "[opentv-port] done: $SCRIPT_DIR/otv_port"
