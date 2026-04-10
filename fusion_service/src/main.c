#include "fusion_service.h"
#include "logger.h"
#include <signal.h>
#include <unistd.h>

static int g_exit = 0;

void signal_handler(int sig) {
    g_exit = 1;
}

int main(int argc, char *argv[]) {
    Config config;

    // 初始化日志
    logger_init(LOG_LEVEL_INFO);

    LOG_INFO("Fusion Service Starting...");

    // 加载配置
    config_default(&config);
    if (argc > 1) {
        config_load(argv[1], &config);
    }

    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 初始化服务
    if (fusion_service_init(&config) != 0) {
        LOG_ERROR("Failed to initialize fusion service");
        return -1;
    }

    // 启动服务
    if (fusion_service_start() != 0) {
        LOG_ERROR("Failed to start fusion service");
        fusion_service_cleanup();
        return -1;
    }

    // 主循环
    while (!g_exit) {
        sleep(1);
    }

    // 停止服务
    fusion_service_stop();
    fusion_service_cleanup();

    LOG_INFO("Fusion Service Stopped");
    return 0;
}
