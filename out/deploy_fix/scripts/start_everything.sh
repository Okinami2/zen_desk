#!/bin/bash
# 终极一键启动脚本：同时拉起后台所有服务（Fusion/Radar/ASR）和前台 UI 界面（屏幕/Qt）

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$(dirname "$SCRIPT_DIR")"
. "$SCRIPT_DIR/env.sh"

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

# 1. 先启动非显示相关后台服务。vision 需要等 vo_init 占住显示栈后再启动。
echo ">>> [启动] 正在启动后台服务引擎..."
VISION_ENABLE=0 "$SCRIPT_DIR/start_all.sh"

echo ""
echo ">>> [启动] 后台服务就绪，准备启动前台显示界面..."

# 2. 检查 Qt 客户端可执行文件是否存在
QT_BIN="$PROJ_DIR/qt_client/qt_client"
if [ ! -x "$QT_BIN" ]; then
    echo "[ERROR] 找不到编译好的 Qt 客户端可执行文件: $QT_BIN"
    echo "        由于板子上可能没有交叉编译好的版本，您可以尝试运行："
    echo "        $SCRIPT_DIR/run_qt_client.sh"
    echo "        让板子自己进行本地编译。"
    exit 1
fi

# 3. 启动屏幕和 Qt UI 进程
echo ">>> [启动] 正在点亮屏幕并启动 UI 客户端..."
# 注意：这里去掉了 exec，这样脚本才能在 UI 结束后继续执行陷阱(trap)来自动停止后台。
export POST_DISPLAY_READY_CMD="VISION_EXTRA_ARGS= $SCRIPT_DIR/start_vision.sh"
"$SCRIPT_DIR/run_qt_display.sh" "$@"
