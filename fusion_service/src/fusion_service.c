#include "fusion_service.h"
#include "logger.h"
#include "../../device_service/include/device_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

static FusionService g_fusion_service;
static pthread_t g_fusion_thread;
static pthread_t g_server_thread;
static int g_running = 0;
static int g_server_fd = -1;
static int g_server_running = 0;

/* ==================== TCP 服务器 ==================== */

static int recv_all(int fd, void *buf, size_t len)
{
    size_t remain = len;
    char *p = (char *)buf;
    while (remain > 0) {
        ssize_t n = recv(fd, p, remain, 0);
        if (n <= 0) return -1;
        remain -= n;
        p += n;
    }
    return 0;
}

static void* tcp_server_thread(void *arg)
{
    struct sockaddr_in addr;
    int listen_fd, opt = 1;

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        LOG_ERROR("TCP server socket failed: %s", strerror(errno));
        return NULL;
    }

    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(g_fusion_service.config.fusion_port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("TCP bind :%d failed: %s",
                  g_fusion_service.config.fusion_port, strerror(errno));
        close(listen_fd);
        return NULL;
    }

    if (listen(listen_fd, 4) < 0) {
        LOG_ERROR("TCP listen failed: %s", strerror(errno));
        close(listen_fd);
        return NULL;
    }

    g_server_fd = listen_fd;
    g_server_running = 1;

    LOG_INFO("TCP server listening on :%d", g_fusion_service.config.fusion_port);

    while (g_server_running) {
        fd_set fds;
        struct timeval tv = {1, 0};

        FD_ZERO(&fds);
        FD_SET(listen_fd, &fds);

        int ret = select(listen_fd + 1, &fds, NULL, NULL, &tv);
        if (ret <= 0) continue;

        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("TCP accept failed: %s", strerror(errno));
            continue;
        }

        LOG_INFO("TCP client connected (fd=%d)", client_fd);

        /* 读取消息循环: [type:4B BE][length:4B BE][payload:N B] */
        while (g_server_running) {
            uint32_t type_be, len_be;
            uint8_t payload[256];

            if (recv_all(client_fd, &type_be, 4) != 0) break;
            if (recv_all(client_fd, &len_be, 4) != 0) break;

            uint32_t msg_type = ntohl(type_be);
            uint32_t msg_len  = ntohl(len_be);

            if (msg_len > sizeof(payload)) break;

            if (recv_all(client_fd, payload, msg_len) != 0) break;

            if (msg_type == MSG_RADAR_STATE && msg_len == sizeof(RadarState)) {
                RadarState *rs = (RadarState *)payload;
                fusion_update_radar(rs);
                LOG_INFO("Recv radar: presence=%d, motion=%.2f, dist=%.2f m, quality=%.2f",
                         rs->presence, rs->motion_level, rs->distance, rs->radar_quality);
            } else {
                LOG_DEBUG("Recv unknown msg: type=0x%02X len=%u", msg_type, msg_len);
            }
        }

        close(client_fd);
        LOG_INFO("TCP client disconnected (fd=%d)", client_fd);
    }

    close(listen_fd);
    g_server_fd = -1;
    LOG_INFO("TCP server stopped");
    return NULL;
}

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

    if (pthread_create(&g_server_thread, NULL, tcp_server_thread, NULL) != 0) {
        LOG_ERROR("Failed to create TCP server thread");
        g_running = 0;
        g_fusion_service.running = 0;
        pthread_join(g_fusion_thread, NULL);
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

    g_server_running = 0;
    pthread_join(g_server_thread, NULL);
    pthread_join(g_fusion_thread, NULL);
    device_service_stop();

    LOG_INFO("Fusion service stopped");
}

void fusion_service_cleanup() {
    LOG_INFO("Cleaning up fusion service...");

    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }

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
