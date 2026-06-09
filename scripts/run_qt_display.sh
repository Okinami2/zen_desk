#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJ_DIR=$(dirname "$SCRIPT_DIR")
VO_INIT_BIN=${VO_INIT_BIN:-"$PROJ_DIR/out/bin/vo_init"}
QT_BIN=${QT_BIN:-"$PROJ_DIR/qt_client/qt_client"}
RUN_DIR=${RUN_DIR:-"$PROJ_DIR/out/run"}
READY_FILE="$RUN_DIR/vo_init.ready"
LOG_FILE="$RUN_DIR/vo_init.log"
VO_PID=

cleanup()
{
    if [ -n "${VO_PID:-}" ] && kill -0 "$VO_PID" 2>/dev/null; then
        kill "$VO_PID" 2>/dev/null || true
        wait "$VO_PID" 2>/dev/null || true
    fi
    rm -f "$READY_FILE"
}

trap cleanup EXIT INT TERM HUP

if [ ! -x "$VO_INIT_BIN" ]; then
    echo "vo_init is not executable: $VO_INIT_BIN" >&2
    exit 1
fi
if [ ! -x "$QT_BIN" ]; then
    echo "Qt executable is not executable: $QT_BIN" >&2
    echo "Set QT_BIN=/path/to/qt_client when launching this script." >&2
    exit 1
fi

mkdir -p "$RUN_DIR"
rm -f "$READY_FILE"

"$VO_INIT_BIN" --ready-file "$READY_FILE" >"$LOG_FILE" 2>&1 &
VO_PID=$!

attempt=0
while [ ! -f "$READY_FILE" ]; do
    if ! kill -0 "$VO_PID" 2>/dev/null; then
        echo "vo_init exited before display became ready:" >&2
        cat "$LOG_FILE" >&2
        exit 1
    fi
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 100 ]; then
        echo "timed out waiting for vo_init; see $LOG_FILE" >&2
        exit 1
    fi
    sleep 0.1
done

export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-linuxfb:fb=/dev/fb0}

echo "display stack ready: vo_init pid=$VO_PID"
echo "starting Qt: $QT_BIN"
"$QT_BIN" "$@"
