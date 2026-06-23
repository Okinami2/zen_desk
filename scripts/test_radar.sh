#!/bin/bash

# 雷达测试脚本：用于测试雷达串口是否能接收到数据
# 该脚本需要跑在板子上

# 组长配置的雷达设备号和波特率
UART_DEV="/dev/ttyAMA4"
BAUD_RATE="115200"

echo "========================================="
echo "   毫米波雷达串口连通性测试工具"
echo "========================================="

# 检查设备节点是否存在
if [ ! -e "$UART_DEV" ]; then
    echo "错误：找不到串口设备 $UART_DEV ！"
    echo "请检查板子的引脚配置或内核驱动是否正常加载。"
    exit 1
fi

echo "[1/3] 正在配置串口参数..."
echo "      设备: $UART_DEV"
echo "      波特率: $BAUD_RATE"
echo "      模式: raw (8N1)"

# 使用 stty 配置串口为原始模式、波特率 115200、无回显
stty -F "$UART_DEV" "$BAUD_RATE" raw -echo cs8 -cstopb -parenb

if [ $? -ne 0 ]; then
    echo "错误：串口配置失败！"
    exit 1
fi

echo "[2/3] 串口配置成功！"
echo "[3/3] 开始监听雷达数据..."
echo "      (提示: 毫米波雷达的数据通常是二进制格式)"
echo "      (请在雷达前方走动或挥手，观察是否有数据涌出)"
echo "      (按 Ctrl+C 退出测试)"
echo "-----------------------------------------"

# 读取串口数据并以 16 进制形式打印（因为雷达发送的通常是十六进制协议包）
cat "$UART_DEV" | hexdump -v -e '16/1 "%02X " "\n"'
