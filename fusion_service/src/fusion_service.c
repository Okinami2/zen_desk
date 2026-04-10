#include "fusion_service.h"
#include "logger.h"
#include "../../device_service/include/device_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

static FusionService g_fusion_service;
static pthread_t g_fusion_thread;
static int g_running = 0;

static const LearningState g_simulated_states[] = {
    STATE_SEATED_IDLE,
    STATE_FOCUSED,
    STATE_DISTRACTED,
    STATE_TIRED,
    STATE_ABSENT
};

// 融合处理线程
static void* fusion_process_thread(void *arg) {
    LOG_INFO("Fusion process thread started");

    size_t state_index = 0;

    while (g_running) {
        FusionState fusion_state;
        LearningState state = g_simulated_states[state_index];

        pthread_mutex_lock(&g_fusion_service.mutex);
        g_fusion_service.current_state = state;
        pthread_mutex_unlock(&g_fusion_service.mutex);

        fusion_state.current_state = state;
        fusion_state.state_score = 1.0f;
        fusion_state.intervention_level = (state == STATE_TIRED) ? 2 :
                                          (state == STATE_DISTRACTED) ? 1 : 0;
        fusion_state.timestamp = time(NULL);

        fusion_send_state(&fusion_state);
        device_handle_fusion_state(&fusion_state);

        LOG_INFO("Simulated fusion state: %d", state);

        state_index = (state_index + 1) %
                      (sizeof(g_simulated_states) / sizeof(g_simulated_states[0]));
        sleep(30);
    }

    LOG_INFO("Fusion process thread stopped");
    return NULL;
}

int fusion_service_init(const Config *config) {
    LOG_INFO("Initializing fusion service...");

    memset(&g_fusion_service, 0, sizeof(FusionService));
    g_fusion_service.config = *config;
    g_fusion_service.running = 0;
    g_fusion_service.current_state = STATE_SEATED_IDLE;
    pthread_mutex_init(&g_fusion_service.mutex, NULL);

    if (device_service_init(config) != 0) {
        LOG_ERROR("Failed to initialize device service from fusion service");
        pthread_mutex_destroy(&g_fusion_service.mutex);
        return -1;
    }

    LOG_INFO("Fusion service initialized");
    return 0;
}

int fusion_service_start() {
    LOG_INFO("Starting fusion service...");

    if (device_service_start() != 0) {
        LOG_ERROR("Failed to start device service from fusion service");
        return -1;
    }

    g_running = 1;
    g_fusion_service.running = 1;

    if (pthread_create(&g_fusion_thread, NULL, fusion_process_thread, NULL) != 0) {
        LOG_ERROR("Failed to create fusion process thread");
        device_service_stop();
        return -1;
    }

    LOG_INFO("Fusion service started");
    return 0;
}

void fusion_service_stop() {
    LOG_INFO("Stopping fusion service...");

    g_running = 0;
    g_fusion_service.running = 0;

    pthread_join(g_fusion_thread, NULL);
    device_service_stop();

    LOG_INFO("Fusion service stopped");
}

void fusion_service_cleanup() {
    LOG_INFO("Cleaning up fusion service...");

    device_service_cleanup();
    pthread_mutex_destroy(&g_fusion_service.mutex);

    LOG_INFO("Fusion service cleaned up");
}

void fusion_update_vision(const VisionState *state) {
    pthread_mutex_lock(&g_fusion_service.mutex);
    g_fusion_service.latest_vision = *state;
    pthread_mutex_unlock(&g_fusion_service.mutex);
}

void fusion_update_radar(const RadarState *state) {
    pthread_mutex_lock(&g_fusion_service.mutex);
    g_fusion_service.latest_radar = *state;
    pthread_mutex_unlock(&g_fusion_service.mutex);
}

int fusion_send_state(const FusionState *state) {
    // TODO: 通过网络发送融合状态到设备服务和Qt客户端
    LOG_INFO("Fusion state: state=%d, score=%.2f, intervention=%d",
             state->current_state, state->state_score, state->intervention_level);
    return 0;
}
