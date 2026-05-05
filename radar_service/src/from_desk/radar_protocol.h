#ifndef RADAR_PROTOCOL_H
#define RADAR_PROTOCOL_H

#include <windows.h>

/* 雷达解析后的数据结构体：承载我们要用的业务数据 */
typedef struct {
    unsigned char has_target;    /* 雷达自带的判定结果：0表示无人，1表示有人（我们在高级监控中不用它，仅供观测） */
    unsigned short distance_cm;  /* 目标的精确物理距离，单位：厘米 (cm) */
    double energy_db[16];        /* 核心数据：16个距离门的精确能量值，转换为 dB (分贝) 后存入此处 */
} RadarData;

/* 函数：下发软重启指令，使雷达模块复位 */
void radar_reboot(HANDLE hComm);

/* 函数：下发全套配置指令（开启配置模式 -> 设置最大侦测门 -> 开启底层能量上报 -> 退出配置模式） */
void radar_configure_all(HANDLE hComm, unsigned char max_gate);

/* * 函数：从串口读到的杂乱字节流中，精准剥离并解析出一帧有效雷达数据
 * 返回值：解析成功返回 1，失败或数据不完整返回 0
 */
int parse_radar_frame(const unsigned char* buffer, int len, RadarData* out_data);

#endif /* RADAR_PROTOCOL_H */