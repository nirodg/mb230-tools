#!/bin/sh
# OpenTV MB230 port installer (run as root on the TV over telnet/SSH).
#
# CyanogenMod-style goal: keep vendor kernel/drivers/libs as base and run
# OpenTV userspace as a sidecar on top. This installs a first-tryable port
# shell (otv_port) to /conf/opentv-port and can arm a background keeper.
#
# Safe default: SIDE-CAR mode (runs alongside stock aurora; no takeover,
# no killing aurora). Takeover/reboot-bypass experiments are opt-in.
#
# Usage (on TV, from dir containing installer.sh and otv_port):
#   sh installer.sh
#
# Seamless wget install example:
#   mkdir -p /tmp/otv-port-pkg && cd /tmp/otv-port-pkg
#   BASE="https://raw.githubusercontent.com/nirodg/mb230-tools/<tag>/opentv-port"
#   wget -O installer.sh "$BASE/installer.sh"
#   wget -O otv_port     "$BASE/otv_port"
#   chmod +x installer.sh otv_port
#   sh installer.sh
#
# Env vars:
#   INSTALL_DIR=/conf/opentv-port   install target (persistent)
#   AUTOLOAD=1                      append guarded keeper hook to /conf/opentv_payload if script (default: 1)
#   MODE=sidecar|takeover           keeper mode (default: sidecar)
#   KICK_WDT=0|1                    in takeover mode try /dev/watchdog kicking (default: 1)
#   TAKEOVER_DELAY=5                seconds to wait before takeover actions (default: 5)

set -eu
umask 077

INSTALL_DIR="${INSTALL_DIR:-/conf/opentv-port}"
AUTOLOAD="${AUTOLOAD:-1}"
MODE="${MODE:-sidecar}"
KICK_WDT="${KICK_WDT:-1}"
TAKEOVER_DELAY="${TAKEOVER_DELAY:-5}"
SRC_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PAYLOAD="/conf/opentv_payload"
HOOK_BEGIN="# --- OPENTV_PORT_HOOK_BEGIN ---"
HOOK_END="# --- OPENTV_PORT_HOOK_END ---"

say() { printf '%s\n' "$*"; }
warn() { printf '[opentv-port] WARN: %s\n' "$*" >&2; }
err() { printf '[opentv-port] ERROR: %s\n' "$*" >&2; }

need_root() {
  if [ "$(id -u 2>/dev/null || echo 1)" != "0" ]; then
    err "run as root (uid 0)."; exit 1
  fi
}

find_src() {
  name="$1"
  if [ -f "$SRC_DIR/$name" ]; then echo "$SRC_DIR/$name"; return 0; fi
  if [ -f "$SRC_DIR/out/$name" ]; then echo "$SRC_DIR/out/$name"; return 0; fi
  echo ""
}

is_text_script() {
  f="$1"
  [ -f "$f" ] || return 1
  if head -c 2 "$f" 2>/dev/null | grep -q '^#!'; then return 0; fi
  # treat small text without NULs as script-like
  if LC_ALL=C grep -q '[^[:print:][:space:]]' "$f" 2>/dev/null; then return 1; fi
  return 0
}

install_files() {
  mkdir -p "$INSTALL_DIR"
  BIN_SRC="$(find_src otv_port)"
  if [ -z "$BIN_SRC" ]; then
    err "otv_port binary not found next to installer. Build it (build-docker.sh) or include it in the package."
    exit 1
  fi
  say "[opentv-port] Installing to $INSTALL_DIR"
  cp "$BIN_SRC" "$INSTALL_DIR/otv_port"
  chmod 755 "$INSTALL_DIR/otv_port" 2>/dev/null || true

  # runtime config
  cat > "$INSTALL_DIR/mode.env" <<EOF
# OpenTV port keeper configuration (edit as needed)
MODE=$MODE
KICK_WDT=$KICK_WDT
TAKEOVER_DELAY=$TAKEOVER_DELAY
# Extra args for otv_port in sidecar mode, e.g. "--no-ui"
OTV_ARGS=""
EOF
  chmod 600 "$INSTALL_DIR/mode.env" 2>/dev/null || true

  cat > "$INSTALL_DIR/run-on-tv.sh" <<'RUN'
#!/bin/sh
# Manual runner for OpenTV MB230 port.
#   sidecar:  ./run-on-tv.sh sidecar [-- args]
#   takeover: ./run-on-tv.sh takeover (EXPERIMENTAL: may reboot if WDT not satisfied)
set -eu
DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
BIN="$DIR/otv_port"
MODE="${1:-sidecar}"
[ "$#" -gt 0 ] && shift || true
KICK_WDT="${KICK_WDT:-1}"
TAKEOVER_DELAY="${TAKEOVER_DELAY:-5}"

case "$MODE" in
  sidecar)
    echo "[otv-port] starting sidecar (no aurora kill). args: $*"
    exec "$BIN" "$@"
    ;;
  takeover)
    echo "[otv-port] EXPERIMENTAL TAKEOVER in ${TAKEOVER_DELAY}s (Ctrl-C to abort)"
    echo "[otv-port] will stop aurora/idtv/appmgr and run UI; reboot possible if watchdog not kicked"
    sleep "$TAKEOVER_DELAY"
    ARGS=""
    if [ "$KICK_WDT" = "1" ]; then ARGS="--kick-wdt"; fi
    # best-effort: stop stock UI owners
    killall aurora.elf 2>/dev/null || true
    killall idtv 2>/dev/null || true
    killall com.nvt.idtv 2>/dev/null || true
    killall appmgr 2>/dev/null || true
    sleep 2
    exec "$BIN" $ARGS "$@"
    ;;
  *)
    echo "usage: $0 sidecar|takeover [-- args]" >&2; exit 2
    ;;
esac
RUN
  chmod 755 "$INSTALL_DIR/run-on-tv.sh" 2>/dev/null || true

  cat > "$INSTALL_DIR/keeper.sh" <<'KEEPER'
#!/bin/sh
# OpenTV port keeper (background). Default: sidecar, no disruption.
# Reads /conf/opentv-port/mode.env when present.
INSTALL_DIR="${INSTALL_DIR:-/conf/opentv-port}"
BIN="$INSTALL_DIR/otv_port"
LOG="$INSTALL_DIR/keeper.log"
PIDFILE="$INSTALL_DIR/otv_port.pid"
MODE="sidecar"; KICK_WDT="1"; TAKEOVER_DELAY="5"; OTV_ARGS=""
[ -f "$INSTALL_DIR/mode.env" ] && . "$INSTALL_DIR/mode.env" 2>/dev/null || true

log() { printf '[%s] %s\n' "$(date 2>/dev/null || echo now)" "$*" >>"$LOG" 2>/dev/null || true; }

running() {
  [ -f "$PIDFILE" ] || return 1
  pid="$(cat "$PIDFILE" 2>/dev/null || echo)"
  [ -n "$pid" ] || return 1
  kill -0 "$pid" 2>/dev/null
}

start_sidecar() {
  if running; then return 0; fi
  [ -x "$BIN" ] || { log "binary missing: $BIN"; return 1; }
  log "starting sidecar otv_port $OTV_ARGS"
  "$BIN" $OTV_ARGS >>"$INSTALL_DIR/otv_port.stdout.log" 2>&1 &
  echo $! > "$PIDFILE" 2>/dev/null || true
}

# takeover is intentionally NOT auto-looped aggressively to avoid reboot loops
start_takeover_once() {
  [ -x "$BIN" ] || { log "binary missing: $BIN"; return 1; }
  ARGS=""
  [ "$KICK_WDT" = "1" ] && ARGS="--kick-wdt"
  log "EXPERIMENTAL takeover in ${TAKEOVER_DELAY}s (args: $ARGS $OTV_ARGS)"
  sleep "$TAKEOVER_DELAY"
  killall aurora.elf 2>/dev/null || true
  killall idtv 2>/dev/null || true
  killall com.nvt.idtv 2>/dev/null || true
  killall appmgr 2>/dev/null || true
  sleep 2
  "$BIN" $ARGS $OTV_ARGS >>"$INSTALL_DIR/otv_port.stdout.log" 2>&1 &
  echo $! > "$PIDFILE" 2>/dev/null || true
}

case "$MODE" in
  takeover)
    # one-shot takeover attempt, then monitor; if device reboots this won't loop here
    start_takeover_once
    while :; do
      sleep 15
      if ! running; then
        log "takeover process not running; not auto-restarting (safety)."
        # keep keeper alive but idle to avoid tight restart/reboot loops
      fi
    done
    ;;
  sidecar|*)
    while :; do
      start_sidecar || true
      sleep 10
    done
    ;;
esac
KEEPER
  chmod 755 "$INSTALL_DIR/keeper.sh" 2>/dev/null || true
}

arm_autoload() {
  [ "$AUTOLOAD" = "1" ] || { say "[opentv-port] AUTOLOAD disabled; skipping /conf/opentv_payload hook."; return 0; }
  if [ ! -f "$PAYLOAD" ]; then
    warn "$PAYLOAD not found; cannot auto-load. Start manually: $INSTALL_DIR/keeper.sh &"
    return 0
  fi
  if ! is_text_script "$PAYLOAD"; then
    warn "$PAYLOAD is not a plain shell script (may be binary). Not modifying it."
    warn "Hook keeper manually from your boot payload: $INSTALL_DIR/keeper.sh &"
    return 0
  fi
  if grep -qF "$HOOK_BEGIN" "$PAYLOAD" 2>/dev/null; then
    say "[opentv-port] autoload hook already present in $PAYLOAD"
    return 0
  fi
  cp "$PAYLOAD" "$PAYLOAD.bak.otvport" 2>/dev/null || warn "could not backup $PAYLOAD"
  # Append hook near end in a background-safe way (does not exec-replace; starts once)
  cat >> "$PAYLOAD" <<EOF

$HOOK_BEGIN
# OpenTV port sidecar keeper (auto-added). Safe default: sidecar mode.
# To disable: remove this block or set MODE=disabled in $INSTALL_DIR/mode.env.
if [ -x "$INSTALL_DIR/keeper.sh" ]; then
  if [ -f "$INSTALL_DIR/keeper.pid" ] && kill -0 "\$(cat "$INSTALL_DIR/keeper.pid 2>/dev/null)" 2>/dev/null; then
    :
  else
    INSTALL_DIR="$INSTALL_DIR" sh "$INSTALL_DIR/keeper.sh" >/dev/null 2>&1 &
    echo \$! > "$INSTALL_DIR/keeper.pid" 2>/dev/null || true
  fi
fi
$HOOK_END
EOF
  say "[opentv-port] appended autoload hook to $PAYLOAD (backup: $PAYLOAD.bak.otvport)"
}

start_now() {
  # start keeper now (background) if not running
  if [ -f "$INSTALL_DIR/keeper.pid" ] && kill -0 "$(cat "$INSTALL_DIR/keeper.pid" 2>/dev/null)" 2>/dev/null; then
    say "[opentv-port] keeper already running (pid $(cat "$INSTALL_DIR/keeper.pid" 2>/dev/null))."
    return 0
  fi
  say "[opentv-port] starting keeper now (MODE=$MODE)"
  INSTALL_DIR="$INSTALL_DIR" sh "$INSTALL_DIR/keeper.sh" >/dev/null 2>&1 &
  echo $! > "$INSTALL_DIR/keeper.pid" 2>/dev/null || true
}

main() {
  need_root
  install_files
  arm_autoload
  start_now
  say "[opentv-port] installed. Binary: $INSTALL_DIR/otv_port"
  say "[opentv-port] Logs: $INSTALL_DIR/keeper.log, /tmp/otv_port.log"
  say "[opentv-port] Manual: $INSTALL_DIR/run-on-tv.sh sidecar|takeover"
  say "[opentv-port] NOTE: default is sidecar (safe). Takeover may reboot unless platform WDT is satisfied; see docs/REBOOT_AND_LIVENESS.md"
}

main "$@"
