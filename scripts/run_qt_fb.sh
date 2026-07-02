#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJ_DIR=$(dirname "$SCRIPT_DIR")
. "$SCRIPT_DIR/env.sh"

QT_BIN=${QT_BIN:-"$PROJ_DIR/qt_client/qt_client"}

if [ ! -x "$QT_BIN" ]; then
    echo "Qt executable is not executable: $QT_BIN" >&2
    exit 1
fi

export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-linuxfb:fb=/dev/fb0}

EC11_EVENT=$(awk '
    /Name="ZenDesk_EC11_Knob"/ { found=1; next }
    found && /Handlers=/ {
        match($0, /event[0-9]+/);
        if (RSTART > 0) {
            last_event = substr($0, RSTART, RLENGTH);
        }
        found=0;
    }
    END { print last_event }
' /proc/bus/input/devices)

if [ -n "$EC11_EVENT" ]; then
    echo "Found active EC11 virtual keyboard: /dev/input/$EC11_EVENT"
    export QT_QPA_GENERIC_PLUGINS=evdevkeyboard
    export QT_QPA_EVDEV_KEYBOARD_PARAMETERS=/dev/input/$EC11_EVENT
else
    echo "[WARN] Could not find ZenDesk_EC11_Knob in /proc/bus/input/devices"
fi

echo "starting Qt: $QT_BIN"
"$QT_BIN" "$@"
