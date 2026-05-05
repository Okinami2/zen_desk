#include <stdio.h>
#include <windows.h>
#include "serial_utils.h"
#include "radar_protocol.h"

/* 宏定义：配置雷达侦测的最大距离门（0~15 门对应 0~11.25 米，12代表约 9 米范围） */
#define MAX_DISTANCE_GATE 5
/* 串口名：务必改成你电脑设备管理器里显示的编号，比如 COM3、COM5 */
#define PORT_NAME "\\\\.\\COM9" /* 请根据实际情况修改为你的雷达串口名称 */

int main() {
    /* 变量声明区 */
    HANDLE hComm;
    unsigned char rx_buffer[256]; /* 接收缓冲，雷达一帧是45字节，256足够存几个周期的杂乱数据了 */
    int bytes_read;
    RadarData radar_data;
    int i;

    /* 终端配置：切换控制台为 UTF-8 编码，防止中文显示乱码 */
    SetConsoleOutputCP(65001);
    printf("--- LD2420 全能量观测工具启动 ---\n");

    /* 打开串口并验证 */
    hComm = open_serial(PORT_NAME, 115200);
    if (hComm == INVALID_HANDLE_VALUE) {
        printf("无法打开串口，请检查波特率或端口号是否被占用。\n");
        return -1;
    }

    printf("正在下发初始化配置...\n");
    radar_configure_all(hComm, MAX_DISTANCE_GATE);
    printf("配置完成，开始监听数据...\n");

    /* 无限循环抓取数据 */
    while (1) {
        /* 从串口读取实时缓存内容 */
        bytes_read = read_serial(hComm, rx_buffer, sizeof(rx_buffer));
        if (bytes_read > 0) {
            /* 尝试从读到的缓存中解析帧格式 */
            if (parse_radar_frame(rx_buffer, bytes_read, &radar_data)) {
                
                /* 第一行：打印雷达自身识别的目标距离和有人无人状态 */
                printf("\n[距离: %3d cm] [自带判定: %s]", 
                       radar_data.distance_cm, 
                       radar_data.has_target ? "有人" : "无人");
                
                /* 第二行：遍历打印 0 到 15 门各自的能量分贝值（供肉眼观测底噪使用） */
                printf("各门能量 (dB): ");
                for (i = 0; i < 16; i++) {
                    printf("%02d:%.1f  ", i, radar_data.energy_db[i]);
                }
                //printf("\n");
            }
        }
        /* 短暂休眠，防止死循环导致 CPU 占用率达到 100% */
        Sleep(10); 
    }

    /* 释放串口资源（虽然死循环中一般走不到这） */
    close_serial(hComm);
    return 0;
}