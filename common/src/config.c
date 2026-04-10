#include "config.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>

void config_default(Config *config) {
    // 网络配置
    strcpy(config->fusion_host, "127.0.0.1");
    config->fusion_port = 8888;

    // 视觉配置
    config->vision_port = 8001;
    strcpy(config->vision_video_device, "/dev/video0");
    strcpy(config->vision_pixel_format, "MJPEG");
    config->vision_width = 1280;
    config->vision_height = 720;
    config->vision_fps = 30;
    config->vision_buffer_count = 4;
    config->vision_enable_hdmi_preview = 1;
    config->eye_close_threshold = 0.7f;
    config->yawn_threshold = 0.6f;

    // 雷达配置
    config->radar_port = 8002;
    strcpy(config->radar_device, "/dev/ttyUSB0");
    config->radar_baudrate = 115200;

    // 设备配置
    config->device_port = 8003;

    // 日志配置
    strcpy(config->log_path, "/tmp/zen_desk.log");
    config->log_level = LOG_LEVEL_INFO;
}

int config_load(const char *config_file, Config *config) {
    FILE *fp = fopen(config_file, "r");
    if (!fp) {
        LOG_WARN("Config file not found, using defaults");
        config_default(config);
        return -1;
    }

    // TODO: 实现配置文件解析（JSON格式）
    config_default(config);

    fclose(fp);
    return 0;
}

int config_save(const char *config_file, const Config *config) {
    FILE *fp = fopen(config_file, "w");
    if (!fp) {
        LOG_ERROR("Failed to save config file");
        return -1;
    }

    // TODO: 实现配置文件保存（JSON格式）

    fclose(fp);
    return 0;
}
