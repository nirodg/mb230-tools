#!/bin/sh
# Convenience wrapper for OpenTV MB230 port.
# Usage:
#   ./run-on-tv.sh [sidecar|takeover] [-- otv_port args...]
#   ./run-on-tv.sh [-- otv_port args...]            # defaults to sidecar
#
# Notes:
# - sidecar runs in foreground (like ./otv_port). If installer keeper already
#   started otv_port in background, you may see a second instance; check:
#     ps | grep otv_port; tail -f /tmp/otv_port.log
# - takeover is EXPERIMENTAL: waits, stops aurora/idtv/appmgr, then runs UI.
#   Device may reboot ~20-30s if platform (MCU/STBC) watchdog is not satisfied.
set -eu

DIR="${INSTALL_DIR:-/conf/opentv-port}"
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

# If this is the repo copy and an installed runner exists elsewhere, prefer it.
if [ "$HERE" != "$DIR" ] && [ -x "$DIR/run-on-tv.sh" ]; then
  exec "$DIR/run-on-tv.sh" "$@"
fi

BIN="$HERE/otv_port"
[ -x "$BIN" ] || BIN="$DIR/otv_port"
if [ ! -x "$BIN" ]; then
  echo "otv_port not found. Run installer.sh first." >&2; exit 1
fi

MODE="sidecar"
if [ "${1:-}" = "sidecar" ] || [ "${1:-}" = "takeover" ]; then
  MODE="$1"; shift
elif [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  echo "usage: $0 [sidecar|takeover] [-- otv_port args...]"; exit 0
elif [ "${1:-}" != "" ] && [ "${1#-}" = "${1}" ]; then
  # unknown non-flag token -> treat as mode for backward compat error
  echo "usage: $0 [sidecar|takeover] [-- otv_port args...]" >&2; exit 2
fi
# if first arg is a flag (e.g. --kick-wdt), keep MODE=sidecar and pass through

case "$MODE" in
  sidecar)
    echo "[opentv-port] sidecar: starting $BIN $*" >&2
    exec "$BIN" "$@"
    ;;
  takeover)
    KICK_WDT="${KICK_WDT:-1}"
    TAKEOVER_DELAY="${TAKEOVER_DELAY:-5}"
    ARGS=""
    [ "$KICK_WDT" = "1" ] && ARGS="--kick-wdt"
    echo "[opentv-port] EXPERIMENTAL TAKEOVER in ${TAKEOVER_DELAY}s (Ctrl-C to abort)" >&2
    echo "[opentv-port] will stop aurora/idtv/appmgr, then exec: $BIN $ARGS $*" >&2
    echo "[opentv-port] WARNING: may reboot in ~20-30s if MCU/STBC watchdog not satisfied" >&2
    sleep "$TAKEOVER_DELAY"
    # stop keeper-managed otv_port if running to avoid duplicates
    if [ -f "$DIR/otv_port.pid" ]; then
      kill "$(cat "$DIR/otv_port.pid" 2>/dev/null)" 2>/dev/null || true
    fi
    killall aurora.elf 2>/dev/null || true
    killall idtv 2>/dev/null || true
    killall com.nvt.idtv 2>/dev/null || true
    killall appmgr 2>/dev/null || true
    sleep 2
    echo "[opentv-port] launching takeover UI now..." >&2
    exec "$BIN" $ARGS "$@"
    ;;
esac
