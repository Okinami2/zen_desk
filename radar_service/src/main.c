#include "board_pins.h"
#include "radar_service.h"
#include "hi_uart.h"
#include "logger.h"
#include <signal.h>
#include <unistd.h>
#include <string.h>

#define RADAR_UART_DEVICE   BOARD_RADAR_UART_DEVICE
#define RADAR_UART_BAUD     115200

static int g_exit = 0;

void signal_handler(int sig) {
    g_exit = 1;
}

int main(int argc, char *argv[]) {
    Config config;

    // 初始化日志
    logger_init(LOG_LEVEL_INFO);

    LOG_INFO("Radar Service Starting...");
    if (board_pins_apply() != 0) {
        LOG_ERROR("Failed to apply unified board pin mapping");
        return -1;
    }


    // 打开串口
    int uart_fd = hi_serial_open(RADAR_UART_DEVICE);
    if (uart_fd < 0) {
        LOG_ERROR("Failed to open uart device: %s", RADAR_UART_DEVICE);
        return -1;
    }

    // 初始化串口: 8数据位, 1停止位, 无校验, 无流控
    if (hi_serial_init(uart_fd, RADAR_UART_BAUD, 0, 8, 1, 'N') != 0) {
        LOG_ERROR("Failed to init uart");
        hi_serial_close(uart_fd);
        return -1;
    }

    LOG_INFO("UART opened: %s, baud: %d, fd: %d", RADAR_UART_DEVICE, RADAR_UART_BAUD, uart_fd);

    // 加载配置
    config_default(&config);
    if (argc > 1) {
        config_load(argv[1], &config);
    }

    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 初始化服务
    if (radar_service_init(&config, uart_fd) != 0) {
        LOG_ERROR("Failed to initialize radar service");
        hi_serial_close(uart_fd);
        return -1;
    }

    // 启动服务
    if (radar_service_start() != 0) {
        LOG_ERROR("Failed to start radar service");
        radar_service_cleanup();
        return -1;
    }

    // 主循环
    while (!g_exit) {
        sleep(1);
    }

    // 停止服务
    radar_service_stop();
    radar_service_cleanup();

    LOG_INFO("Radar Service Stopped");
    return 0;
}
