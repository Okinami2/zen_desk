#!/bin/sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PID_DIR="$SCRIPT_DIR/../out/pid"
PID_FILE="$PID_DIR/vision_service.pid"

if [ ! -f "$PID_FILE" ]; then
    exit 0
fi

pid=$(cat "$PID_FILE")
if kill -0 "$pid" 2>/dev/null; then
    echo "[vision_service] Stopping (pid=$pid)..."
    kill "$pid" 2>/dev/null || true
    for i in $(seq 1 50); do
        kill -0 "$pid" 2>/dev/null || break
        usleep 100000 2>/dev/null || sleep 0.1
    done
    if kill -0 "$pid" 2>/dev/null; then
        echo "[vision_service] Force killing..."
        kill -9 "$pid" 2>/dev/null || true
    fi
    echo "[vision_service] Stopped"
fi

rm -f "$PID_FILE"
