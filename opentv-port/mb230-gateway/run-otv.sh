#!/bin/sh
# run-otv.sh — launch the Open TV port on the MB230.
#
# Binds any plugged-in USB stick onto the fixed external-storage path
# (/buffer/opentv-port/usb) so device.conf never changes, then runs the
# launcher. Usage: sh run-otv.sh [extra otv-launcher args...]
set -u

ROOT=/buffer/opentv-port
EXT="$ROOT/usb"

# 1) find the firmware's real USB mount (any removable filesystem in
#    /proc/mounts: vfat/exfat/ntfs/fuse/ext* on an sd* device), else unset
usb_dev=""
while read -r dev mp fs rest; do
    case "$dev" in
        /dev/sd*|/dev/mmcblk*)
            case "$fs" in vfat|exfat|ntfs|fuseblk|ext2|ext3|ext4|udf)
                usb_dev="$mp"; break ;;
            esac ;;
    esac
done < /proc/mounts

if [ -n "$usb_dev" ] && [ "$usb_dev" != "$EXT" ]; then
    mkdir -p "$EXT"
    # refresh the bind in case the stick was re-plugged
    grep -q " $EXT " /proc/mounts || mount --bind "$usb_dev" "$EXT" 2>/dev/null
    echo "[otv] USB: bind $usb_dev -> $EXT"
else
    mkdir -p "$EXT"
    echo "[otv] USB: none plugged (external storage empty)"
fi

# 2) run the launcher
cd "$ROOT" || exit 1
export OTV_DEVICE_CONF="$ROOT/device.conf"
export OTV_FONTS="$ROOT/assets/fonts"
export OTV_ICONS="$ROOT/assets/icons"
exec "$ROOT/otv-launcher" "$@"
