#include "radar_protocol.h"
#include "hi_uart.h"
#include <math.h>
#include <unistd.h>
#include <stdint.h>

void radar_reboot(int fd)
{
    unsigned char cmd[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0x68, 0x00, 0x04, 0x03, 0x02, 0x01};
    hi_serial_send(fd, (char *)cmd, sizeof(cmd));
    usleep(100000); /* 给雷达芯片 100ms 重启时间 */
}

void radar_configure_all(int fd, unsigned char max_distance_dm)
{
    /* 1. 使能配置指令: 0x00FF, 开启雷达的配置写权限 */
    unsigned char cmd_enable[]  = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};

    /* 2. 设置最大距离门: 参数ID=0x0001, 命令字=0x0007 */
    unsigned char cmd_maxgate[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x08, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};

    /* 3. 开启能量上报: 系统参数 0x0012, 参数值 0x00000004 */
    unsigned char cmd_report[]  = {0xFD, 0xFC, 0xFB, 0xFA, 0x08, 0x00, 0x12, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};

    /* 4. 结束配置: 0x00FE, 保存并退出配置模式 */
    unsigned char cmd_end[]     = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x00, 0x04, 0x03, 0x02, 0x01};

    cmd_maxgate[10] = max_distance_dm;

    hi_serial_send(fd, (char *)cmd_enable,  sizeof(cmd_enable));  usleep(50000);
    hi_serial_send(fd, (char *)cmd_maxgate, sizeof(cmd_maxgate)); usleep(50000);
    hi_serial_send(fd, (char *)cmd_report,  sizeof(cmd_report));  usleep(50000);
    hi_serial_send(fd, (char *)cmd_end,     sizeof(cmd_end));     usleep(50000);
}

int parse_radar_frame(const unsigned char *buffer, int len, RadarData *out_data)
{
    int i, j;
    if (len < 141) return 0;

    for (i = 0; i <= len - 141; i++) {
        /* 包头: F4 F3 F2 F1 */
        if (buffer[i] == 0xF4 && buffer[i+1] == 0xF3 &&
            buffer[i+2] == 0xF2 && buffer[i+3] == 0xF1) {

            /* 长度应为 0x0083 (131) */
            if (buffer[i+4] == 0x83 && buffer[i+5] == 0x00) {

                /* 包尾(偏移137): F8 F7 F6 F5 */
                if (buffer[i+137] == 0xF8 && buffer[i+138] == 0xF7 &&
                    buffer[i+139] == 0xF6 && buffer[i+140] == 0xF5) {

                    out_data->has_target  = buffer[i+6];
                    out_data->distance_cm = buffer[i+7] | (buffer[i+8] << 8);

                    for (j = 0; j < 16; j++) {
                        /* 提取每个距离门的4字节运动能量值 */
                        uint32_t raw_motion = buffer[i+9 + j*4] | 
                                             (buffer[i+10 + j*4] << 8) | 
                                             (buffer[i+11 + j*4] << 16) | 
                                             (buffer[i+12 + j*4] << 24);
                        if (raw_motion > 0) {
                            out_data->motion_energy_db[j] = 10.0 * log10((double)raw_motion);
                        } else {
                            out_data->motion_energy_db[j] = 0.0;
                        }

                        /* 提取每个距离门的4字节静止能量值 (紧跟在运动能量的64字节之后, 即 i+9+64 = i+73) */
                        uint32_t raw_static = buffer[i+73 + j*4] | 
                                             (buffer[i+74 + j*4] << 8) | 
                                             (buffer[i+75 + j*4] << 16) | 
                                             (buffer[i+76 + j*4] << 24);
                        if (raw_static > 0) {
                            out_data->static_energy_db[j] = 10.0 * log10((double)raw_static);
                        } else {
                            out_data->static_energy_db[j] = 0.0;
                        }
                    }
                    return 1;
                }
            }
        }
    }
    return 0;
}
