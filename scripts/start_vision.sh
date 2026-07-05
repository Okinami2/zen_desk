#!/bin/sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$(dirname "$SCRIPT_DIR")"
. "$SCRIPT_DIR/env.sh"

BIN_DIR="$PROJ_DIR/out/bin"
LOG_DIR="$PROJ_DIR/out/log"
PID_DIR="$PROJ_DIR/out/pid"
VISION_SNAPSHOT_ENABLE="${VISION_SNAPSHOT_ENABLE:-0}"
SNAPSHOT_DIR="${VISION_SNAPSHOT_DIR:-}"
VISION_DEVICE="${VISION_DEVICE:-/dev/video0}"
VISION_FORMAT="${VISION_FORMAT:-YUYV}"
VISION_WIDTH="${VISION_WIDTH:-1280}"
VISION_HEIGHT="${VISION_HEIGHT:-720}"
VISION_TELEMETRY="${VISION_TELEMETRY:-127.0.0.1:8889}"
VISION_SNAPSHOT_EVERY="${VISION_SNAPSHOT_EVERY:-10}"
VISION_SNAPSHOT_LIMIT="${VISION_SNAPSHOT_LIMIT:-100}"
VISION_MPP_ATTACHED="${VISION_MPP_ATTACHED:-0}"
VISION_DISPLAY_ENABLE="${VISION_DISPLAY_ENABLE:-0}"
VISION_DISPLAY_READY_FILE="${VISION_DISPLAY_READY_FILE:-}"
VISION_MONITORING_ENABLE="${VISION_MONITORING_ENABLE:-1}"
VISION_CONTROL_PORT="${VISION_CONTROL_PORT:-9101}"
VISION_EXTRA_SAFE_ARGS=
VISION_MPP_ARGS=
VISION_DISPLAY_ARGS=
VISION_MONITOR_ARGS="--control-port $VISION_CONTROL_PORT"

if [ "${VISION_ALLOW_EXTRA_ARGS:-0}" = "1" ]; then
    VISION_EXTRA_SAFE_ARGS="${VISION_EXTRA_ARGS:-}"
fi

if [ "$VISION_MPP_ATTACHED" = "1" ]; then
    VISION_MPP_ARGS="--mpp-attached"
fi

if [ "$VISION_DISPLAY_ENABLE" = "1" ]; then
    VISION_DISPLAY_ARGS="--display"
    if [ -n "$VISION_DISPLAY_READY_FILE" ]; then
        mkdir -p "$(dirname "$VISION_DISPLAY_READY_FILE")"
        rm -f "$VISION_DISPLAY_READY_FILE"
        VISION_DISPLAY_ARGS="$VISION_DISPLAY_ARGS --display-ready-file $VISION_DISPLAY_READY_FILE"
    fi
fi

if [ "$VISION_MONITORING_ENABLE" = "1" ]; then
    VISION_MONITOR_ARGS="$VISION_MONITOR_ARGS --monitoring-start-enabled"
else
    VISION_MONITOR_ARGS="$VISION_MONITOR_ARGS --monitoring-start-disabled"
fi

mkdir -p "$LOG_DIR" "$PID_DIR"

if [ "$VISION_SNAPSHOT_ENABLE" = "1" ]; then
    SNAPSHOT_DIR="${SNAPSHOT_DIR:-$PROJ_DIR/out/snapshots}"
    mkdir -p "$SNAPSHOT_DIR"
fi

if [ -f "$PID_DIR/vision_service.pid" ]; then
    old_pid=$(cat "$PID_DIR/vision_service.pid")
    if kill -0 "$old_pid" 2>/dev/null; then
        echo "       vision_service already running (pid=$old_pid)"
        exit 0
    fi
    rm -f "$PID_DIR/vision_service.pid"
fi

echo "       Starting vision_service..."
if [ "$VISION_FORMAT" = "MJPEG" ] || [ "$VISION_FORMAT" = "H264" ] || [ "$VISION_FORMAT" = "H265" ]; then
    echo "       warning: $VISION_FORMAT uses MPP VDEC; prefer YUYV/NV12/NV21 if capture fails"
fi
if [ "$VISION_SNAPSHOT_ENABLE" = "1" ]; then
    "$BIN_DIR/vision_service" \
        --device "$VISION_DEVICE" \
        --format "$VISION_FORMAT" \
        --width "$VISION_WIDTH" \
        --height "$VISION_HEIGHT" \
        --telemetry "$VISION_TELEMETRY" \
        --snapshot-dir "$SNAPSHOT_DIR" \
        --snapshot-every "$VISION_SNAPSHOT_EVERY" \
        --snapshot-limit "$VISION_SNAPSHOT_LIMIT" \
        $VISION_MPP_ARGS \
        $VISION_DISPLAY_ARGS \
        $VISION_MONITOR_ARGS \
        $VISION_EXTRA_SAFE_ARGS \
        >"$LOG_DIR/vision_service.log" 2>&1 &
else
    "$BIN_DIR/vision_service" \
        --device "$VISION_DEVICE" \
        --format "$VISION_FORMAT" \
        --width "$VISION_WIDTH" \
        --height "$VISION_HEIGHT" \
        --telemetry "$VISION_TELEMETRY" \
        $VISION_MPP_ARGS \
        $VISION_DISPLAY_ARGS \
        $VISION_MONITOR_ARGS \
        $VISION_EXTRA_SAFE_ARGS \
        >"$LOG_DIR/vision_service.log" 2>&1 &
fi

echo $! > "$PID_DIR/vision_service.pid"
sleep 2
if ! kill -0 "$(cat "$PID_DIR/vision_service.pid")" 2>/dev/null; then
    echo "       vision_service failed; see $LOG_DIR/vision_service.log" >&2
    cat "$LOG_DIR/vision_service.log" >&2
    rm -f "$PID_DIR/vision_service.pid"
    exit 1
fi

echo "       vision_service started (pid=$(cat "$PID_DIR/vision_service.pid"))"
if [ "$VISION_SNAPSHOT_ENABLE" = "1" ]; then
    echo "       format=$VISION_FORMAT size=${VISION_WIDTH}x${VISION_HEIGHT} snapshots=$SNAPSHOT_DIR telemetry=$VISION_TELEMETRY mpp_attached=$VISION_MPP_ATTACHED display=$VISION_DISPLAY_ENABLE monitoring=$VISION_MONITORING_ENABLE control=$VISION_CONTROL_PORT"
else
    echo "       format=$VISION_FORMAT size=${VISION_WIDTH}x${VISION_HEIGHT} snapshots=disabled telemetry=$VISION_TELEMETRY mpp_attached=$VISION_MPP_ATTACHED display=$VISION_DISPLAY_ENABLE monitoring=$VISION_MONITORING_ENABLE control=$VISION_CONTROL_PORT"
fi
