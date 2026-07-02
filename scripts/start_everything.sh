#!/bin/bash
# 终极一键启动脚本：解决依赖死锁问题的完美启动顺序！

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$(dirname "$SCRIPT_DIR")"
. "$SCRIPT_DIR/env.sh"
VISION_DISPLAY_READY_FILE="${VISION_DISPLAY_READY_FILE:-$PROJ_DIR/out/run/vision_display.ready}"

BIN_DIR="$PROJ_DIR/out/bin"
LOG_DIR="$PROJ_DIR/out/log"
PID_DIR="$PROJ_DIR/out/pid"
mkdir -p "$LOG_DIR" "$PID_DIR"

echo "================================================="
echo "   Zen Desk - 全量服务完美启动脚本 (彻底解决丢包和按键问题)   "
echo "================================================="

# 0. 注册退出回调：当用户按 Ctrl+C 时，自动帮用户把后台服务也停掉
cleanup() {
    echo ""
    echo ">>> [清理] 检测到 UI 退出或 Ctrl+C，正在自动清理所有后台服务..."
    "$SCRIPT_DIR/stop_all.sh"
    echo ">>> [清理] 后台服务已全部安全退出，拜拜！"
}
trap cleanup EXIT INT TERM HUP

echo ">>> [清理] 正在排查并清理上次可能遗留的僵尸进程..."
"$SCRIPT_DIR/stop_all.sh" > /dev/null 2>&1 || true

echo ">>> [第1步] 应用引脚复用和设备驱动..."
if ! "$BIN_DIR/pinmux_init" --apply > /dev/null 2>&1; then
    echo "[WARN] pinmux_init 失败，外设可能无法使用"
fi

echo ">>> [第2步] 启动视觉服务，初始化屏幕..."
VISION_DISPLAY_ENABLE=1 \
VISION_DISPLAY_READY_FILE="$VISION_DISPLAY_READY_FILE" \
"$SCRIPT_DIR/start_vision.sh"

attempt=0
while [ ! -f "$VISION_DISPLAY_READY_FILE" ]; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 100 ]; then
        echo "[ERROR] 等待屏幕初始化超时!" >&2
        exit 1
    fi
    sleep 0.1
done
echo "       屏幕已就绪。"

echo ">>> [第3步] 启动 Fusion 引擎 (建立 EC11 虚拟键盘 和 TCP 服务端)..."
"$BIN_DIR/fusion_service" >"$LOG_DIR/fusion_service.log" 2>&1 &
echo $! > "$PID_DIR/fusion_service.pid"
# 必须等 fusion 完全启动，虚拟键盘设备 /proc/bus/input/devices 才会出现！
sleep 1.5

QT_BIN="${QT_BIN:-$PROJ_DIR/qt_client/qt_client}"
if [ ! -x "$QT_BIN" ]; then
    echo "[ERROR] 找不到 Qt 客户端: $QT_BIN"
    exit 1
fi

echo ">>> [第4步] 屏幕与外设就绪，启动 Qt UI 客户端..."
QT_BIN="$QT_BIN" "$SCRIPT_DIR/run_qt_fb.sh" "$@" &
QT_PID=$!
# 必须等 Qt 完全启动并绑定 8889 UDP 端口！
sleep 1.5

echo ">>> [第5步] Qt 准备就绪！启动雷达和语音引擎，触发首包状态下发..."
"$BIN_DIR/radar_service" >"$LOG_DIR/radar_service.log" 2>&1 &
echo $! > "$PID_DIR/radar_service.pid"

"$BIN_DIR/asr_service" >"$LOG_DIR/asr_service.log" 2>&1 &
echo $! > "$PID_DIR/asr_service.pid"

echo ""
echo "===== 🎉 所有服务启动完成，系统全状态运行中！ ====="
echo "  Run 'tail -f $LOG_DIR/fusion_service.log' to watch fusion"
echo "  按 Ctrl+C 或退出 UI 即可安全结束全部进程。"

# 前台挂起，等待 Qt 退出
wait $QT_PID
