#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <sys/time.h>
#include "hi_uart.h"
#include "radar_protocol.h"

// 海鸥派默认雷达串口 (之前写错了，应该是 ttyAMA4！)
#define RADAR_UART_DEVICE "/dev/ttyAMA4"
#define RADAR_UART_BAUD 115200

// 延时配置
#define COUNTDOWN_SECONDS 5

void print_usage(const char *prog_name) {
    printf("Usage: %s <label_id> <duration_secs> <output_csv_path>\n", prog_name);
    printf("Example: %s 0 60 /tmp/away.csv\n", prog_name);
    printf("Labels: 0=AWAY(无人), 1=NORMAL(在座), 2=FIDGET(乱动)\n");
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        print_usage(argv[0]);
        return -1;
    }

    int label = atoi(argv[1]);
    int duration = atoi(argv[2]);
    const char *out_file = argv[3];

    printf("==========================================\n");
    printf("[INFO] 目标标签: %d\n", label);
    printf("[INFO] 采集时长: %d 秒\n", duration);
    printf("[INFO] 输出文件: %s\n", out_file);
    printf("==========================================\n");

    // 1. 初始化串口
    int uart_fd = hi_serial_open(RADAR_UART_DEVICE);
    if (uart_fd < 0) {
        printf("[ERROR] 无法打开串口 %s. 请确保主程序(radar_service)已经停止!\n", RADAR_UART_DEVICE);
        return -1;
    }

    if (hi_serial_init(uart_fd, RADAR_UART_BAUD, 0, 8, 1, 'N') != 0) {
        printf("[ERROR] 串口初始化失败!\n");
        hi_serial_close(uart_fd);
        return -1;
    }

    // 2. 配置雷达
    printf("[INFO] 正在配置雷达并开启能量上报模式...\n");
    radar_reboot(uart_fd);
    sleep(2); // 等待雷达重启完毕，否则配置指令会被忽略！
    radar_configure_all(uart_fd, 80); // 80dm = 8m
    tcflush(uart_fd, TCIOFLUSH);
    
    // 3. 准备写入文件
    FILE *fp = fopen(out_file, "a");
    if (!fp) {
        printf("[ERROR] 无法打开文件 %s\n", out_file);
        hi_serial_close(uart_fd);
        return -1;
    }
    
    // 如果是新文件，写入表头
    fseek(fp, 0, SEEK_END);
    if (ftell(fp) == 0) {
        fprintf(fp, "label");
        for (int i = 0; i < 16; i++) fprintf(fp, ",m%d", i);
        for (int i = 0; i < 16; i++) fprintf(fp, ",s%d", i);
        fprintf(fp, "\n");
    }
    
    // 4. 倒计时
    printf("\n[INFO] >>> 准备阶段 <<<\n");
    for (int i = COUNTDOWN_SECONDS; i > 0; i--) {
        printf("[INFO] %d 秒后开始采集数据，请摆好姿势或离开座位...\n", i);
        sleep(1);
    }
    
    printf("\n[INFO] >>> 开始采集 <<< (不要强制终止)\n");

    // 5. 采集循环
    struct timeval start_tv, current_tv;
    gettimeofday(&start_tv, NULL);
    
    unsigned char buf[1024];
    int offset = 0;
    int frames_collected = 0;

    while (1) {
        gettimeofday(&current_tv, NULL);
        double elapsed = (current_tv.tv_sec - start_tv.tv_sec) + 
                         (current_tv.tv_usec - start_tv.tv_usec) / 1000000.0;
                         
        if (elapsed >= duration) {
            break;
        }

        int ret = hi_serial_recv(uart_fd, (char *)(buf + offset), 256);
        if (ret <= 0) {
            continue;
        }
        
        int total = offset + ret;
        RadarData radar_data;
        int consumed;
        int parsed = 0;

        // 循环解析 buffer 中所有的完整帧
        while ((consumed = parse_radar_frame(buf, total, &radar_data)) > 0) {
            // 写入 CSV
            fprintf(fp, "%d", label);
            for (int i = 0; i < 16; i++) {
                fprintf(fp, ",%.1f", radar_data.motion_energy_db[i]);
            }
            for (int i = 0; i < 16; i++) {
                fprintf(fp, ",%.1f", radar_data.static_energy_db[i]);
            }
            fprintf(fp, "\n");
            
            frames_collected++;
            parsed++;
            
            // 满足用户要求：采集一个生成一行成功采集的标（换行打印）
            printf("[SUCCESS] 已成功采集 %d 帧 (耗时: %.1f / %d 秒)\n", frames_collected, elapsed, duration);
            fflush(stdout);
            
            // 移动剩余数据到缓冲区头部
            total -= consumed;
            if (total > 0) {
                memmove(buf, buf + consumed, total);
            }
        }
        
        // 如果读到了数据，但死活解析不出帧，打印出来看看我们到底收到了什么鬼东西
        if (parsed == 0 && ret > 0) {
            static int debug_print_cnt = 0;
            if (debug_print_cnt < 20) { // 最多只刷 20 行，防止卡死
                printf("[DEBUG] 收到了 %d 字节原始数据: ", total);
                for (int i = 0; i < total && i < 20; i++) {
                    printf("%02X ", buf[i]);
                }
                printf("...\n");
                debug_print_cnt++;
            }
        }
        
        if (parsed > 0) {
            fflush(fp);
        }
        
        // 防止缓冲区因脏数据塞满
        if (total > 768) {
            memmove(buf, buf + 1, total - 1);
            total -= 1;
        }
        
        offset = total;
    }

    printf("\n\n[INFO] 采集完成! 共采集有效特征帧: %d\n", frames_collected);
    
    fclose(fp);
    hi_serial_close(uart_fd);
    
    return 0;
}
