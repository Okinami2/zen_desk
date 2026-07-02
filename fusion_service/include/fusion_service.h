#ifndef FUSION_SERVICE_H
#define FUSION_SERVICE_H

#include "../../common/include/protocol.h"
#include "../../common/include/config.h"
#include <pthread.h>

// 融合服务结构
typedef struct {
    Config config;
    int running;

    VisionState latest_vision;
    RadarState latest_radar;

    LearningState current_state;
    int state_counter;

    pthread_mutex_t mutex;
} FusionService;

// 初始化融合服务
int fusion_service_init(const Config *config);

// 启动融合服务
int fusion_service_start();

// 停止融合服务
void fusion_service_stop();

// 清理融合服务
void fusion_service_cleanup();

// 更新视觉状态
void fusion_update_vision(const VisionState *state);

// 更新雷达状态
void fusion_update_radar(const RadarState *state);

// 发送融合状态
int fusion_send_state(const FusionState *state);

#endif // FUSION_SERVICE_H
