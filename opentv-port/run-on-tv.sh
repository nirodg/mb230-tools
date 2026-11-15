#!/bin/sh
# Convenience wrapper: run installed port from /conf/opentv-port, or local copy.
# Usage: ./run-on-tv.sh [sidecar|takeover] [-- otv_port args]
set -eu
DIR="${INSTALL_DIR:-/conf/opentv-port}"
if [ -x "$DIR/run-on-tv.sh" ]; then
  exec "$DIR/run-on-tv.sh" "$@"
fi
# fallback: local
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
if [ -x "$HERE/otv_port" ]; then
  MODE="${1:-sidecar}"; [ "$#" -gt 0 ] && shift || true
  if [ "$MODE" = "takeover" ]; then
    echo "[opentv-port] takeover: stopping aurora/idtv/appmgr in 5s (EXPERIMENTAL)"; sleep 5
    killall aurora.elf 2>/dev/null || true; killall idtv 2>/dev/null || true; killall appmgr 2>/dev/null || true; sleep 2
    exec "$HERE/otv_port" --kick-wdt "$@"
  else
    exec "$HERE/otv_port" "$@"
  fi
fi
echo "otv_port not found. Run installer.sh first." >&2; exit 1
