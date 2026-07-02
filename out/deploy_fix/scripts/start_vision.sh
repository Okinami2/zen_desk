#!/bin/sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$(dirname "$SCRIPT_DIR")"
. "$SCRIPT_DIR/env.sh"

BIN_DIR="$PROJ_DIR/out/bin"
LOG_DIR="$PROJ_DIR/out/log"
PID_DIR="$PROJ_DIR/out/pid"
SNAPSHOT_DIR="${VISION_SNAPSHOT_DIR:-$PROJ_DIR/out/snapshots}"
VISION_DEVICE="${VISION_DEVICE:-/dev/video0}"
VISION_FORMAT="${VISION_FORMAT:-MJPEG}"
VISION_WIDTH="${VISION_WIDTH:-1280}"
VISION_HEIGHT="${VISION_HEIGHT:-720}"
VISION_TELEMETRY="${VISION_TELEMETRY:-127.0.0.1:8889}"
VISION_SNAPSHOT_EVERY="${VISION_SNAPSHOT_EVERY:-10}"
VISION_SNAPSHOT_LIMIT="${VISION_SNAPSHOT_LIMIT:-100}"
VISION_EXTRA_SAFE_ARGS=

if [ "${VISION_ALLOW_EXTRA_ARGS:-0}" = "1" ]; then
    VISION_EXTRA_SAFE_ARGS="${VISION_EXTRA_ARGS:-}"
fi

mkdir -p "$LOG_DIR" "$PID_DIR" "$SNAPSHOT_DIR"

if [ -f "$PID_DIR/vision_service.pid" ]; then
    old_pid=$(cat "$PID_DIR/vision_service.pid")
    if kill -0 "$old_pid" 2>/dev/null; then
        echo "       vision_service already running (pid=$old_pid)"
        exit 0
    fi
    rm -f "$PID_DIR/vision_service.pid"
fi

echo "       Starting vision_service..."
"$BIN_DIR/vision_service" \
    --device "$VISION_DEVICE" \
    --format "$VISION_FORMAT" \
    --width "$VISION_WIDTH" \
    --height "$VISION_HEIGHT" \
    --telemetry "$VISION_TELEMETRY" \
    --snapshot-dir "$SNAPSHOT_DIR" \
    --snapshot-every "$VISION_SNAPSHOT_EVERY" \
    --snapshot-limit "$VISION_SNAPSHOT_LIMIT" \
    $VISION_EXTRA_SAFE_ARGS \
    >"$LOG_DIR/vision_service.log" 2>&1 &

echo $! > "$PID_DIR/vision_service.pid"
sleep 2
if ! kill -0 "$(cat "$PID_DIR/vision_service.pid")" 2>/dev/null; then
    echo "       vision_service failed; see $LOG_DIR/vision_service.log" >&2
    cat "$LOG_DIR/vision_service.log" >&2
    rm -f "$PID_DIR/vision_service.pid"
    exit 1
fi

echo "       vision_service started (pid=$(cat "$PID_DIR/vision_service.pid"))"
echo "       snapshots=$SNAPSHOT_DIR telemetry=$VISION_TELEMETRY"
