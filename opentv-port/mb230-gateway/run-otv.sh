#!/bin/sh
# run-otv.sh — launch the Open TV port on the MB230.
#
# Starts the USB hot-plug watcher (bind-mounts the firmware's USB mount onto
# the fixed external path /buffer/opentv-port/usb), then runs the launcher
# and cleans the watcher up when the launcher exits.
set -u

ROOT=/buffer/opentv-port

cd "$ROOT" || exit 1

# USB hot-plug watcher in the background (both ports; firmware does the real
# mount, we keep the fixed path bound to it)
WATCHER=""
if [ -x "$ROOT/usb-watch.sh" ]; then
    "$ROOT/usb-watch.sh" >> "$ROOT/launcher.log" 2>&1 &
    WATCHER=$!
fi

export OTV_DEVICE_CONF="$ROOT/device.conf"
export OTV_FONTS="$ROOT/assets/fonts"
export OTV_ICONS="$ROOT/assets/icons"
"$ROOT/otv-launcher" "$@"
rc=$?
[ -n "$WATCHER" ] && kill "$WATCHER" 2>/dev/null
exit $rc
