#include "radar_protocol.h"
#include "serial_utils.h"
#include <math.h>  /* 引入数学库以使用 log10 函数进行分贝转换 */

void radar_reboot(HANDLE hComm) {
    /* * 重启指令包结构：
     * FD FC FB FA : 配置指令包头
     * 02 00       : 指令长度 (2字节)
     * 68 00       : 重启模块的命令字 (0x0068 小端模式即 68 00)
     * 04 03 02 01 : 配置指令包尾
     */
    unsigned char cmd[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0x68, 0x00, 0x04, 0x03, 0x02, 0x01};
    write_serial(hComm, cmd, sizeof(cmd));
    Sleep(100); /* 给雷达芯片预留重启的硬件反应时间（100毫秒） */
}

void radar_configure_all(HANDLE hComm, unsigned char max_gate) {
    /* 1. 使能配置指令：0x00FF，开启雷达的配置写权限 */
    unsigned char cmd_enable[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
    
    /* 2. 设置最大距离门指令：参数ID为 0x0001，命令字为 0x0007 */
    unsigned char cmd_maxgate[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x08, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
    
    /* 3. 开启能量上报指令：系统参数指令 0x0012，参数值 0x00000004 代表输出能量信息 */
    unsigned char cmd_report[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x08, 0x00, 0x12, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
    
    /* 4. 结束配置指令：0x00FE，保存参数并退出配置模式，准备开始正常测距 */
    unsigned char cmd_end[]    = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x00, 0x04, 0x03, 0x02, 0x01};

    /* 动态将传入的 max_gate 值放入设置指令的数据位中 (索引10处) */
    cmd_maxgate[10] = max_gate; 

    /* 依次下发指令，每个指令间留出 50 毫秒缓冲，防止芯片处理不过来丢包 */
    write_serial(hComm, cmd_enable, sizeof(cmd_enable));
    Sleep(50);
    write_serial(hComm, cmd_maxgate, sizeof(cmd_maxgate));
    Sleep(50);
    write_serial(hComm, cmd_report, sizeof(cmd_report));
    Sleep(50);
    write_serial(hComm, cmd_end, sizeof(cmd_end));
    Sleep(50);
}

int parse_radar_frame(const unsigned char* buffer, int len, RadarData* out_data) {
    /* 遵循 C89：局部变量在顶层声明 */
    int i, j;
    unsigned short raw_energy;
    
    /* 一帧带有能量数据的雷达包，协议规定固定为 45 字节。如果读到的数据不够 45 字节，直接放弃解析 */
    if (len < 45) return 0;

    /* 遍历缓存流，寻找包头 (滑动窗寻头法) */
    for (i = 0; i <= len - 45; i++) {
        /* 上报数据的包头规定为：F4 F3 F2 F1 */
        if (buffer[i] == 0xF4 && buffer[i+1] == 0xF3 && buffer[i+2] == 0xF2 && buffer[i+3] == 0xF1) {
            /* 发现包头后，验证距离包头 41 字节处的包尾是否为：F8 F7 F6 F5 */
            if (buffer[i+41] == 0xF8 && buffer[i+42] == 0xF7 && buffer[i+43] == 0xF6 && buffer[i+44] == 0xF5) {
                
                /* --- 此时确认我们截获了完整的一帧数据，开始按字节位置提取信息 --- */
                
                /* 第 6 字节是雷达自带的“有无目标”判断标志 */
                out_data->has_target = buffer[i+6];
                
                /* 第 7 和 8 字节是目标的精确距离（厘米），小端模式拼装：高位左移8位 | 低位 */
                out_data->distance_cm = buffer[i+7] | (buffer[i+8] << 8);

                /* 第 9 字节开始，每 2 个字节为一个距离门的底层能量原始值，共 16 个门 */
                for (j = 0; j < 16; j++) {
                    raw_energy = buffer[i + 9 + j*2] | (buffer[i + 10 + j*2] << 8);
                    
                    /* 根据文档算法公式：分贝值 N = 10 * log10(原始值 M) */
                    if (raw_energy > 0) {
                        out_data->energy_db[j] = 10.0 * log10((double)raw_energy);
                    } else {
                        out_data->energy_db[j] = 0.0; /* 保护逻辑：防止对 0 求对数导致数学异常(NaN) */
                    }
                }
                return 1; /* 成功解析出一帧，立刻返回告诉主调函数 */
            }
        }
    }
    return 0; /* 找遍了也没凑齐一帧 */
}