#!/bin/bash

echo "========================================="
echo "      EC11 虚拟键盘事件独立监听工具"
echo "========================================="

# 动态寻找最新创建的 EC11 虚拟键盘事件节点
EC11_EVENT=$(awk '
    /Name="ZenDesk_EC11_Knob"/ { found=1; next }
    found && /Handlers=/ {
        match($0, /event[0-9]+/);
        if (RSTART > 0) {
            last_event = substr($0, RSTART, RLENGTH);
        }
        found=0;
    }
    END { print last_event }
' /proc/bus/input/devices)

if [ -z "$EC11_EVENT" ]; then
    echo "[错误] 找不到名为 ZenDesk_EC11_Knob 的虚拟键盘设备！"
    echo "       请确保您已经成功运行了后台服务 (fusion_service)。"
    exit 1
fi

DEV_PATH="/dev/input/$EC11_EVENT"
echo "[成功] 锁定活动的 EC11 键盘节点: $DEV_PATH"
echo "-----------------------------------------"
echo ">>> 请现在开始转动或按压您的 EC11 旋钮..."
echo ">>> (按 Ctrl+C 即可退出监听)"
echo ""

# 如果系统有 evtest 工具，它能把按键翻译成人类可读的名字 (如 KEY_UP, KEY_ENTER)
if command -v evtest >/dev/null 2>&1; then
    evtest "$DEV_PATH"
else
    # 如果没有 evtest，就用十六进制打印内核抛出的 input_event 原始结构体数据
    echo "注：当前系统未安装 evtest 工具，将使用十六进制实时打印内核原始键盘事件。"
    echo "    (只要转动时屏幕有数据瀑布般滚出，就代表底层的键盘事件已完美生成！)"
    echo "-----------------------------------------"
    hexdump -C "$DEV_PATH"
fi
