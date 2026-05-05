#!/bin/sh
# 慧学引擎 - 一键启动所有服务
# 启动顺序: fusion(必须先起, 监听8888) → radar(连fusion) → vision(独立)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$(dirname "$SCRIPT_DIR")"
BIN_DIR="$PROJ_DIR/out/bin"
LOG_DIR="$PROJ_DIR/out/log"
PID_DIR="$PROJ_DIR/out/pid"

mkdir -p "$LOG_DIR" "$PID_DIR"

# 检查是否已启动
if [ -f "$PID_DIR/fusion_service.pid" ]; then
    echo "[WARN] fusion_service already running? pid=$(cat $PID_DIR/fusion_service.pid)"
    echo "       If you want to restart, run stop_all.sh first."
    exit 1
fi

echo "===== 慧学引擎 - 启动所有服务 ====="

# ---- 1. fusion_service (必须先启动, 监听 TCP 8888) ----
echo "[1/2] Starting fusion_service..."
"$BIN_DIR/fusion_service" \
    >"$LOG_DIR/fusion_service.log" 2>&1 &
echo $! > "$PID_DIR/fusion_service.pid"
sleep 1
echo "       fusion_service started (pid=$(cat $PID_DIR/fusion_service.pid))"

# ---- 2. radar_service (连接 fusion) ----
echo "[2/2] Starting radar_service..."
"$BIN_DIR/radar_service" \
    >"$LOG_DIR/radar_service.log" 2>&1 &
echo $! > "$PID_DIR/radar_service.pid"
sleep 1
echo "       radar_service started (pid=$(cat $PID_DIR/radar_service.pid))"

echo ""
echo "===== All services started ====="
echo "  fusion:  pid=$(cat $PID_DIR/fusion_service.pid)  log=$LOG_DIR/fusion_service.log"
echo "  radar:   pid=$(cat $PID_DIR/radar_service.pid)  log=$LOG_DIR/radar_service.log"
echo ""
echo "  Run 'tail -f $LOG_DIR/fusion_service.log' to watch fusion"
echo "  Run 'tail -f $LOG_DIR/radar_service.log'  to watch radar"
echo "  Run 'scripts/stop_all.sh' to stop all services"
