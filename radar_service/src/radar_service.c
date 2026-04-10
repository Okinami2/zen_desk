#include "radar_service.h"
#include "logger.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <termios.h>

static RadarService g_radar_service;
static pthread_t g_process_thread;
static int g_running = 0;

// 模拟雷达处理线程
static void* radar_process_thread(void *arg) {
    LOG_INFO("Radar process thread started");

    while (g_running) {
        // TODO: 实现实际的雷达处理逻辑
        // 1. 从串口读取雷达数据
        // 2. 解析雷达数据
        // 3. 提取在位/离座状态
        // 4. 提取微动信息

        // 模拟数据
        RadarState state;
        state.presence = 1;
        state.motion_level = 0.3f;
        state.distance = 0.8f;
        state.radar_quality = 0.85f;
        state.timestamp = time(NULL);

        // 发送状态到融合服务
        radar_send_state(&state);

        usleep(200000); // 200ms
    }

    LOG_INFO("Radar process thread stopped");
    return NULL;
}

int radar_service_init(const Config *config) {
    LOG_INFO("Initializing radar service...");

    memset(&g_radar_service, 0, sizeof(RadarService));
    g_radar_service.config = *config;
    g_radar_service.running = 0;
    g_radar_service.serial_fd = -1;

    // TODO: 打开串口设备
    // g_radar_service.serial_fd = open(config->radar_device, O_RDWR | O_NOCTTY);

    LOG_INFO("Radar service initialized");
    return 0;
}

int radar_service_start() {
    LOG_INFO("Starting radar service...");

    g_running = 1;
    g_radar_service.running = 1;

    if (pthread_create(&g_process_thread, NULL, radar_process_thread, NULL) != 0) {
        LOG_ERROR("Failed to create radar process thread");
        return -1;
    }

    LOG_INFO("Radar service started");
    return 0;
}

void radar_service_stop() {
    LOG_INFO("Stopping radar service...");

    g_running = 0;
    g_radar_service.running = 0;

    pthread_join(g_process_thread, NULL);

    LOG_INFO("Radar service stopped");
}

void radar_service_cleanup() {
    LOG_INFO("Cleaning up radar service...");

    if (g_radar_service.serial_fd >= 0) {
        close(g_radar_service.serial_fd);
        g_radar_service.serial_fd = -1;
    }

    LOG_INFO("Radar service cleaned up");
}

int radar_send_state(const RadarState *state) {
    // TODO: 通过网络发送状态到融合服务
    LOG_DEBUG("Radar state: presence=%d, motion=%.2f, distance=%.2f",
              state->presence, state->motion_level, state->distance);
    return 0;
}
