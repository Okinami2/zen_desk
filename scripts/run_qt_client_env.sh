#!/bin/sh
export LD_LIBRARY_PATH=/opt/lib:$LD_LIBRARY_PATH
export QT_QPA_PLATFORM_PLUGIN_PATH=/opt/plugins
export QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0:size=1920x1080:offset=0x0:nographicsmodeswitch

# 为了兼容之前的启动脚本
SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
PROJ_DIR=$(dirname "$SCRIPT_DIR")
QT_BIN="$PROJ_DIR/qt_client/qt_client"

if [ ! -x "$QT_BIN" ]; then
    echo "Executable not found: $QT_BIN"
    exit 1
fi

echo "Starting Qt Client with full environment..."
"$QT_BIN" "$@"
