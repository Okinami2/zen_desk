#!/bin/bash
# 终极一键启动脚本：同时拉起后台所有服务（Fusion/Radar/ASR）和前台 UI 界面（屏幕/Qt）

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$(dirname "$SCRIPT_DIR")"
. "$SCRIPT_DIR/env.sh"
VISION_DISPLAY_READY_FILE="${VISION_DISPLAY_READY_FILE:-$PROJ_DIR/out/run/vision_display.ready}"

echo "================================================="
echo "   Zen Desk - 全量服务启动脚本 (后台服务 + 屏幕UI)   "
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

# 1. 启动视觉服务。vision_service 作为 MPP/NPU owner，需要先初始化屏幕。
echo ">>> [启动] 正在启动视觉服务以初始化屏幕..."
VISION_DISPLAY_ENABLE=1 \
VISION_DISPLAY_READY_FILE="$VISION_DISPLAY_READY_FILE" \
"$SCRIPT_DIR/start_vision.sh"

echo ""
echo ">>> [启动] 等待 vision 初始化屏幕..."
attempt=0
while [ ! -f "$VISION_DISPLAY_READY_FILE" ]; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 100 ]; then
        echo "[ERROR] 等待屏幕初始化超时: $VISION_DISPLAY_READY_FILE" >&2
        exit 1
    fi
    sleep 0.1
done

# 2. 检查 Qt 客户端可执行文件是否存在
QT_BIN="${QT_BIN:-$PROJ_DIR/qt_client/qt_client}"
if [ ! -x "$QT_BIN" ]; then
    echo "[ERROR] 找不到编译好的 Qt 客户端可执行文件: $QT_BIN"
    echo "        由于板子上可能没有交叉编译好的版本，您可以尝试运行："
    echo "        $SCRIPT_DIR/run_qt_client.sh"
    echo "        让板子自己进行本地编译。"
    exit 1
fi

echo ">>> [启动] 屏幕已就绪，正在启动 UI 客户端..."
# 放入后台，这样我们可以紧接着去启动雷达和融合服务
QT_BIN="$QT_BIN" "$SCRIPT_DIR/run_qt_fb.sh" "$@" &
QT_PID=$!

# 留出 1 秒钟给 Qt 客户端绑定 8889 UDP 端口
sleep 1

echo ">>> [启动] UI 启动完毕，正在启动融合、雷达、语音等后台服务..."
# 3. 启动后台服务 (vision 已经启动过了，所以 VISION_ENABLE=0)
VISION_ENABLE=0 "$SCRIPT_DIR/start_all.sh"

# 4. 等待 Qt 客户端退出 (防止脚本直接结束)
wait $QT_PID
