#!/bin/sh
# usb-watch.sh — USB hot-plug watcher for the Open TV port (MB230).
#
# The TV's firmware mounts USB sticks itself (both ports); this loop just
# keeps the fixed external-storage path /buffer/opentv-port/usb bound to
# the firmware's current USB mount. New stick -> bind; stick removed ->
# drop the bind. Runs forever; started by run-otv.sh.
set -u

ROOT=/buffer/opentv-port
EXT="$ROOT/usb"

# echo the first removable-filesystem mount from /proc/mounts ("" if none)
find_usb() {
    local dev mp fs
    while read -r dev mp fs rest; do
        case "$dev" in
            /dev/sd*|/dev/mmcblk*)
                case "$fs" in vfat|exfat|ntfs|fuseblk|ext2|ext3|ext4|udf)
                    echo "$mp"; return 0 ;;
                esac ;;
        esac
    done < /proc/mounts
    return 1
}

last=""
echo "[otv] usb-watch: started (external = $EXT)"
while :; do
    cur=""
    if m=$(find_usb 2>/dev/null); then cur="$m"; fi
    if [ "$cur" != "$last" ]; then
        if [ -n "$cur" ] && [ "$cur" != "$EXT" ]; then
            mkdir -p "$EXT"
            grep -q " $EXT " /proc/mounts 2>/dev/null || mount --bind "$cur" "$EXT" 2>/dev/null
            echo "[otv] USB: plugged — bind $cur -> $EXT"
        elif [ -z "$cur" ]; then
            if grep -q " $EXT " /proc/mounts 2>/dev/null; then
                umount "$EXT" 2>/dev/null
                echo "[otv] USB: removed — bind dropped"
            fi
        fi
        last="$cur"
    fi
    sleep 2
done
