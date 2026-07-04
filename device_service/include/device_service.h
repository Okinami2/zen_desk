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

/**
 * @brief 相对调整台灯亮度
 * 
 * @param delta_percent 相对亮度调整百分比 (如 +20 或 -20)
 * @return 0 成功, 其它 失败
 */
int device_adjust_lamp_brightness(int delta_percent);

/**
 * @brief 切换台灯色温
 * 
 * @return 0 成功, 其它 失败
 */
int device_toggle_lamp_color_temp(void);

/**
 * @brief 绝对设置亮度（用于进度条百分比同步）
 * 
 * @param percent 百分比 (0-100)
 * @return 0 成功, 其它 失败
 */
void device_set_lamp_brightness_absolute(int percent);

/**
 * @brief 绝对设置色温比例（用于进度条同步）
 * 
 * @param ratio 色温比例 (0.0 - 1.0, 0.0 为纯冷，1.0为纯暖)
 */
void device_set_lamp_color_temp_absolute(float ratio);

/**
 * @brief 获取当前台灯的亮度百分比和色温比例
 * 
 * @param brightness 返回当前亮度百分比 (0-100)
 * @param color_ratio 返回当前色温比例 (0.0-1.0)
 */
void device_get_lamp_state(int *brightness, float *color_ratio);

#endif // DEVICE_SERVICE_H
