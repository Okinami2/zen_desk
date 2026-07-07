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
    config->head_pitch_offset = 0.0f;
    config->head_yaw_offset = 0.0f;

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
    config_default(config); // 先加载默认值
    FILE *fp = fopen(config_file, "r");
    if (!fp) {
        LOG_WARN("Config file not found, using defaults");
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char key[64];
        float val;
        if (sscanf(line, "%63[^=]=%f", key, &val) == 2) {
            if (strcmp(key, "eye_close_threshold") == 0) {
                config->eye_close_threshold = val;
            } else if (strcmp(key, "head_pitch_offset") == 0) {
                config->head_pitch_offset = val;
            } else if (strcmp(key, "head_yaw_offset") == 0) {
                config->head_yaw_offset = val;
            }
        }
    }

    fclose(fp);
    return 0;
}

int config_save(const char *config_file, const Config *config) {
    FILE *fp = fopen(config_file, "w");
    if (!fp) {
        LOG_ERROR("Failed to save config file: %s", config_file);
        return -1;
    }

    fprintf(fp, "eye_close_threshold=%.4f\n", config->eye_close_threshold);
    fprintf(fp, "head_pitch_offset=%.4f\n", config->head_pitch_offset);
    fprintf(fp, "head_yaw_offset=%.4f\n", config->head_yaw_offset);

    fclose(fp);
    return 0;
}
