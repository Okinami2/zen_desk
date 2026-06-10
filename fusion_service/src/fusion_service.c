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
static pthread_t g_server_thread;
static int g_running = 0;
static int g_server_fd = -1;
static int g_server_running = 0;

static int g_udp_fd = -1;
static struct sockaddr_in g_udp_addr;

void fusion_send_ui_event(UiEventType type) {
    if (g_udp_fd < 0) return;
    UiEventMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.event_type = type;
    sendto(g_udp_fd, &msg, sizeof(msg), 0, (struct sockaddr*)&g_udp_addr, sizeof(g_udp_addr));
}

/* ==================== 前置声明 ==================== */
static void radar_to_fusion_and_dispatch(const RadarState *rs);

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
                LOG_INFO("Recv radar: presence=%d, motion=%.2f, dist=%.2f m",
                         rs->presence, rs->motion_level, rs->distance);
                radar_to_fusion_and_dispatch(rs);
            } else if (msg_type == MSG_ASR_COMMAND && msg_len == sizeof(AsrCommand)) {
                AsrCommand *cmd = (AsrCommand *)payload;
                LOG_INFO("Recv ASR Command: 0x%02X", cmd->command_id);
                
                FusionState fs;
                fs.state_score = 1.0;
                fs.intervention_level = 0;
                fs.duration_minutes = 0;
                fs.timestamp = time(NULL);
                
                pthread_mutex_lock(&g_fusion_service.mutex);
                switch(cmd->command_id) {
                    case ASR_CMD_WAKEUP:
                        LOG_INFO("ASR Wakeup received, broadcasting to UI");
                        fusion_send_ui_event(UI_EVENT_WAKEUP_ASR);
                        break;
                    case ASR_CMD_STUDY_START:
                    case ASR_CMD_STUDY_RESUME:
                        g_fusion_service.current_state = STATE_FOCUSED;
                        fs.duration_minutes = 0; // 默认或正计时
                        LOG_INFO("ASR overridden state to FOCUSED");
                        break;
                    case ASR_CMD_STUDY_START_25:
                        g_fusion_service.current_state = STATE_FOCUSED;
                        fs.duration_minutes = 25;
                        LOG_INFO("ASR overridden state to FOCUSED (25 min)");
                        break;
                    case ASR_CMD_STUDY_START_45:
                        g_fusion_service.current_state = STATE_FOCUSED;
                        fs.duration_minutes = 45;
                        LOG_INFO("ASR overridden state to FOCUSED (45 min)");
                        break;
                    case ASR_CMD_STUDY_START_60:
                        g_fusion_service.current_state = STATE_FOCUSED;
                        fs.duration_minutes = 60;
                        LOG_INFO("ASR overridden state to FOCUSED (60 min)");
                        break;
                    case ASR_CMD_STUDY_PAUSE:
                    case ASR_CMD_STUDY_STOP:
                        g_fusion_service.current_state = STATE_SEATED_IDLE;
                        fs.duration_minutes = 0;
                        LOG_INFO("ASR overridden state to IDLE/PAUSE");
                        break;
                    case ASR_CMD_LAMP_ON:
                        LOG_INFO("ASR requested Lamp ON");
                        device_control_lamp(1, 80, 4000); // 默认80%亮度，4000K
                        break;
                    case ASR_CMD_LAMP_OFF:
                        LOG_INFO("ASR requested Lamp OFF");
                        device_control_lamp(0, 0, 0);
                        break;
                    case ASR_CMD_LAMP_BRIGHT_UP:
                        LOG_INFO("ASR requested Lamp Brightness UP");
                        device_adjust_lamp_brightness(20);
                        break;
                    case ASR_CMD_LAMP_BRIGHT_DOWN:
                        LOG_INFO("ASR requested Lamp Brightness DOWN");
                        device_adjust_lamp_brightness(-20);
                        break;
                    case ASR_CMD_SCREEN_DATA:
                        LOG_INFO("ASR requested UI SHOW DATA");
                        fusion_send_ui_event(UI_EVENT_SHOW_DATA);
                        break;
                    case ASR_CMD_SCREEN_HOME:
                        LOG_INFO("ASR requested UI SHOW HOME");
                        fusion_send_ui_event(UI_EVENT_SHOW_HOME);
                        break;
                }
                fs.current_state = g_fusion_service.current_state;
                pthread_mutex_unlock(&g_fusion_service.mutex);
                
                // 只有状态改变时才向外广播状态，触发后续联动（UI/灯光）
                if (cmd->command_id != ASR_CMD_WAKEUP) {
                    fusion_send_state(&fs);
                    device_handle_fusion_state(&fs);
                }
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

/* 雷达 → 融合状态: 雷达作为第一优先级
 * presence=1 → 入座, presence=0 → 离座 */
static void radar_to_fusion_and_dispatch(const RadarState *rs)
{
    FusionState fs;

    fs.state_score       = rs->radar_quality;
    fs.intervention_level = 0;
    fs.duration_minutes  = 0;
    fs.timestamp         = time(NULL);

    pthread_mutex_lock(&g_fusion_service.mutex);
    if (rs->presence) {
        // 如果雷达探测到有人，且当前是“无人”状态，则切换为“闲置”
        // 如果当前已经是专注状态，则保持不变，修复被雷达强制覆盖的Bug
        if (g_fusion_service.current_state == STATE_ABSENT) {
            g_fusion_service.current_state = STATE_SEATED_IDLE;
        }
    } else {
        // 雷达探测不到人，直接切到无人态
        g_fusion_service.current_state = STATE_ABSENT;
    }
    fs.current_state = g_fusion_service.current_state;
    pthread_mutex_unlock(&g_fusion_service.mutex);

    fusion_send_state(&fs);
    device_handle_fusion_state(&fs);
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

    g_udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_udp_fd >= 0) {
        memset(&g_udp_addr, 0, sizeof(g_udp_addr));
        g_udp_addr.sin_family = AF_INET;
        g_udp_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        g_udp_addr.sin_port = htons(8889); // Qt 客户端监听的端口
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

    if (pthread_create(&g_server_thread, NULL, tcp_server_thread, NULL) != 0) {
        LOG_ERROR("Failed to create TCP server thread");
        g_running = 0;
        g_fusion_service.running = 0;
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
    device_service_stop();

    LOG_INFO("Fusion service stopped");
}

void fusion_service_cleanup() {
    LOG_INFO("Cleaning up fusion service...");

    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }

    if (g_udp_fd >= 0) {
        close(g_udp_fd);
        g_udp_fd = -1;
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
    LOG_INFO("Fusion state: state=%d, score=%.2f, intervention=%d, duration=%d min",
             state->current_state, state->state_score, state->intervention_level, state->duration_minutes);
             
    if (g_udp_fd >= 0) {
        UiEventMessage msg;
        memset(&msg, 0, sizeof(msg));
        msg.event_type = UI_EVENT_STATE_UPDATE;
        msg.state = *state;
        sendto(g_udp_fd, &msg, sizeof(msg), 0, (struct sockaddr*)&g_udp_addr, sizeof(g_udp_addr));
    }
    return 0;
}
