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

# 1. 启动后台服务。vision_service 作为 MPP/NPU owner，屏幕随后附着到它已初始化的 MPP。
echo ">>> [启动] 正在启动后台服务引擎..."
VISION_DISPLAY_ENABLE=1 \
VISION_DISPLAY_READY_FILE="$VISION_DISPLAY_READY_FILE" \
"$SCRIPT_DIR/start_all.sh"

echo ""
echo ">>> [启动] 后台服务就绪，准备启动前台显示界面..."

# 2. 检查 Qt 客户端可执行文件是否存在
QT_BIN="${QT_BIN:-$PROJ_DIR/qt_client/qt_client}"
if [ ! -x "$QT_BIN" ]; then
    echo "[ERROR] 找不到编译好的 Qt 客户端可执行文件: $QT_BIN"
    echo "        由于板子上可能没有交叉编译好的版本，您可以尝试运行："
    echo "        $SCRIPT_DIR/run_qt_client.sh"
    echo "        让板子自己进行本地编译。"
    exit 1
fi

# 3. 等待 vision_service 初始化屏幕，然后启动 Qt UI 进程。
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

echo ">>> [启动] 屏幕已就绪，正在启动 UI 客户端..."
QT_BIN="$QT_BIN" "$SCRIPT_DIR/run_qt_fb.sh" "$@"
