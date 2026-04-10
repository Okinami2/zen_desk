#ifndef RADAR_SERVICE_H
#define RADAR_SERVICE_H

#include "../../common/include/protocol.h"
#include "../../common/include/config.h"

// 雷达服务结构
typedef struct {
    Config config;
    int running;
    int serial_fd;
} RadarService;

// 初始化雷达服务
int radar_service_init(const Config *config);

// 启动雷达服务
int radar_service_start();

// 停止雷达服务
void radar_service_stop();

// 清理雷达服务
void radar_service_cleanup();

// 发送雷达状态
int radar_send_state(const RadarState *state);

#endif // RADAR_SERVICE_H
