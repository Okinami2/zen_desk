#!/bin/sh
# 慧学引擎 - 一键停止所有服务

PID_DIR="$(cd "$(dirname "$0")" && pwd)/../out/pid"
STOPPED=0

echo "===== 慧学引擎 - 停止所有服务 ====="

for svc in radar_service asr_service device_service fusion_service; do
    pidfile="$PID_DIR/$svc.pid"
    if [ -f "$pidfile" ]; then
        pid=$(cat "$pidfile")
        if kill -0 "$pid" 2>/dev/null; then
            echo "[$svc] Stopping (pid=$pid)..."
            kill "$pid" 2>/dev/null

            # 等最多 5 秒让进程优雅退出
            for i in $(seq 1 50); do
                kill -0 "$pid" 2>/dev/null || break
                usleep 100000 2>/dev/null || sleep 0.1
            done

            # 还没死就强杀
            if kill -0 "$pid" 2>/dev/null; then
                echo "[$svc] Force killing..."
                kill -9 "$pid" 2>/dev/null
            fi
            echo "[$svc] Stopped"
        else
            echo "[$svc] pid=$pid not alive, cleaning up"
        fi
        rm -f "$pidfile"
        STOPPED=1
    fi
done

if [ "$STOPPED" -eq 0 ]; then
    echo "No services were running."
else
    echo ""
    echo "===== All services stopped ====="
fi
