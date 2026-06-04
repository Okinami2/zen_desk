#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include "asr_controller.h"
#include "serial_setup.h"
#include "logger.h"
#include "config.h"

static int g_exit = 0;

void signal_handler(int sig) {
    g_exit = 1;
}

int main(int argc, char *argv[]) {
    Config config;

    logger_init(LOG_LEVEL_INFO);
    LOG_INFO("==================================================");
    LOG_INFO("       ASR 语音控制服务 (asr_service) 启动");
    LOG_INFO("==================================================");

    config_default(&config);
    if (argc > 1) {
        config_load(argv[1], &config);
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    if (asr_controller_init(&config) != 0) {
        LOG_ERROR("ASR controller init failed");
        // 继续运行，后面可以重连
    }

    if (serial_init() != 0) {
        LOG_WARN("【串口未就绪】 进入模拟模式，模拟发送指令...");
        
        uint8_t test_sequence[] = {
            0x00, 
            ASR_CMD_STUDY_START,
            ASR_CMD_STUDY_PAUSE, 
            ASR_CMD_STUDY_RESUME,
            ASR_CMD_STUDY_STOP
        };
        
        for (int i = 0; i < sizeof(test_sequence) && !g_exit; i++) {
            sleep(3);
            LOG_INFO("--- [模拟接收] 收到串口指令: 0x%02X ---", test_sequence[i]);
            asr_process_command(test_sequence[i]);
        }
        
        // 模拟完成，进入闲置等待
        while (!g_exit) {
            sleep(1);
        }
    } else {
        LOG_INFO("正在监听串口数据...");
        while (!g_exit) {
            uint8_t rx_data = 0;
            int status = serial_read_byte(&rx_data);
            
            if (status == 1) {
                LOG_INFO("--- [真实接收] 收到串口指令: 0x%02X ---", rx_data);
                asr_process_command(rx_data);
            } else if (status == -1) {
                LOG_ERROR("读取串口发生异常");
                sleep(1); // 错误延时，避免狂刷日志
            }
        }
    }

    serial_close();
    asr_controller_cleanup();
    LOG_INFO("ASR Service Stopped.");
    return 0;
}
