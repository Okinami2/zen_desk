#ifndef RADAR_PROTOCOL_H
#define RADAR_PROTOCOL_H

/* 雷达解析后的数据结构体 */
typedef struct {
    int has_target;        /* 0:无人 1:有人运动 2:有人静止 */
    int distance_cm;       /* 目标距离 (cm) */
    double motion_energy_db[16]; /* 16个距离门的运动能量 (dB) */
    double static_energy_db[16]; /* 16个距离门的静止能量 (dB) */
} RadarData;

/* 软重启雷达模块 */
void radar_reboot(int fd);

/* 下发全套配置: 开启配置模式 -> 设置最大距离门 -> 开启能量上报 -> 退出配置模式 */
void radar_configure_all(int fd, unsigned char max_distance_dm);

/* 从字节流中解析一帧雷达数据, 成功返回 1, 失败返回 0 */
int parse_radar_frame(const unsigned char *buffer, int len, RadarData *out_data);

#endif /* RADAR_PROTOCOL_H */
