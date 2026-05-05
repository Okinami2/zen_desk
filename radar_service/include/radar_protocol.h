#ifndef RADAR_PROTOCOL_H
#define RADAR_PROTOCOL_H

/* 雷达解析后的数据结构体 */
typedef struct {
    unsigned char has_target;    /* 雷达自带判定: 0=无人, 1=有人 (仅供观测) */
    unsigned short distance_cm;  /* 目标精确距离, 单位: cm */
    double energy_db[16];        /* 16个距离门的能量值, 单位: dB */
} RadarData;

/* 软重启雷达模块 */
void radar_reboot(int fd);

/* 下发全套配置: 开启配置模式 -> 设置最大距离门 -> 开启能量上报 -> 退出配置模式 */
void radar_configure_all(int fd, unsigned char max_gate);

/* 从字节流中解析一帧雷达数据, 成功返回 1, 失败返回 0 */
int parse_radar_frame(const unsigned char *buffer, int len, RadarData *out_data);

#endif /* RADAR_PROTOCOL_H */
