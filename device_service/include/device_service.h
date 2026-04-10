#ifndef DEVICE_SERVICE_H
#define DEVICE_SERVICE_H

#include "../../common/include/protocol.h"
#include "../../common/include/config.h"

// 设备类型
typedef enum {
    DEVICE_LAMP = 0,
    DEVICE_AC = 1,
    DEVICE_HUMIDIFIER = 2
} DeviceType;

// 设备服务结构
typedef struct {
    Config config;
    int running;
    LearningState current_state;
} DeviceService;

// 初始化设备服务
int device_service_init(const Config *config);

// 启动设备服务
int device_service_start();

// 停止设备服务
void device_service_stop();

// 清理设备服务
void device_service_cleanup();

// 处理融合状态
void device_handle_fusion_state(const FusionState *state);

// 控制台灯
int device_control_lamp(uint8_t action, uint8_t brightness, uint16_t color_temp);

#endif // DEVICE_SERVICE_H
