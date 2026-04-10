#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// 配置结构
typedef struct {
    // 网络配置
    char fusion_host[64];
    uint16_t fusion_port;

    // 视觉配置
    uint16_t vision_port;
    char vision_video_device[64];
    char vision_pixel_format[16];
    uint32_t vision_width;
    uint32_t vision_height;
    uint32_t vision_fps;
    uint32_t vision_buffer_count;
    uint8_t vision_enable_hdmi_preview;
    float eye_close_threshold;
    float yawn_threshold;

    // 雷达配置
    uint16_t radar_port;
    char radar_device[64];
    uint32_t radar_baudrate;

    // 设备配置
    uint16_t device_port;

    // 日志配置
    char log_path[256];
    uint8_t log_level;
} Config;

// 加载配置
int config_load(const char *config_file, Config *config);

// 保存配置
int config_save(const char *config_file, const Config *config);

// 获取默认配置
void config_default(Config *config);

#endif // CONFIG_H
