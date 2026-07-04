#include "fusion_service.h"
#include "logger.h"
#include "../../device_service/include/device_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
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

static int g_asr_socket_fd = -1;
static pthread_mutex_t g_asr_socket_mutex = PTHREAD_MUTEX_INITIALIZER;

void fusion_send_asr_command(uint8_t cmd_id) {
    pthread_mutex_lock(&g_asr_socket_mutex);
    if (g_asr_socket_fd >= 0) {
        Message msg;
        memset(&msg, 0, sizeof(Message));
        AsrCommand asr_cmd;
        asr_cmd.command_id = cmd_id;
        asr_cmd.timestamp = time(NULL);

        msg.type = MSG_ASR_COMMAND;
        msg.length = sizeof(AsrCommand);
        memcpy(msg.data, &asr_cmd, msg.length);

        uint32_t type_be = htonl(msg.type);
        uint32_t len_be = htonl(msg.length);

        send(g_asr_socket_fd, &type_be, 4, MSG_NOSIGNAL);
        send(g_asr_socket_fd, &len_be, 4, MSG_NOSIGNAL);
        send(g_asr_socket_fd, msg.data, msg.length, MSG_NOSIGNAL);
        LOG_INFO("Fusion sent ASR Command 0x%02X down to ASR service", cmd_id);
    }
    pthread_mutex_unlock(&g_asr_socket_mutex);
}

#define VISION_FOCUS_WINDOW_SIZE 10
#define VISION_MIN_VALID_SAMPLES 6
#define VISION_DISTRACTED_SAMPLE_THRESHOLD 7
#define VISION_FOCUSED_SAMPLE_THRESHOLD 8
#define VISION_TRANSITION_COOLDOWN_MS 1500ULL
#define VISION_NO_BLINK_TIMEOUT_MS 30000ULL
#define VISION_EYES_CLOSED_TIMEOUT_MS 1500ULL
#define VISION_SEAT_ENTER_DIAG 165.0f
#define VISION_SEAT_EXIT_DIAG 160.0f
#define VISION_SEAT_ENTER_SAMPLES 2
#define VISION_SEAT_EXIT_SAMPLES 6
#define VISION_FRAME_WIDTH 1280.0f
#define VISION_FRAME_HEIGHT 720.0f
#define VISION_SEAT_CENTER_MIN_X_RATIO 0.25f
#define VISION_SEAT_CENTER_MAX_X_RATIO 0.75f
#define VISION_SEAT_CENTER_MIN_Y_RATIO 0.15f
#define VISION_SEAT_CENTER_MAX_Y_RATIO 0.85f
#define VISION_SEAT_DEBUG_INTERVAL_MS 2000ULL
#define FUSION_STATE_HEARTBEAT_MS 1000ULL

typedef enum {
    VISION_VOTE_UNKNOWN = 0,
    VISION_VOTE_FOCUSED = 1,
    VISION_VOTE_DISTRACTED = -1
} VisionFocusVote;

typedef struct {
    VisionFocusVote votes[VISION_FOCUS_WINDOW_SIZE];
    int vote_index;
    int vote_count;
    int is_distracted;
    int blink_initialized;
    uint32_t last_blink_count;
    uint64_t last_blink_ms;
    uint64_t last_transition_ms;
    uint64_t eyes_closed_since_ms;
    uint8_t last_attention_region;
} VisionFocusFilter;

typedef struct {
    int seated;
    int enter_count;
    int exit_count;
    LearningState state_before_absent;
    uint64_t last_debug_ms;
} VisionSeatFilter;

static VisionFocusFilter g_vision_filter;
static VisionSeatFilter g_vision_seat_filter;

void fusion_send_ui_event(UiEventType type) {
    if (g_udp_fd < 0) return;
    UiEventMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.event_type = type;
    sendto(g_udp_fd, &msg, sizeof(msg), 0, (struct sockaddr*)&g_udp_addr, sizeof(g_udp_addr));
}

void fusion_send_ui_event_custom_start(uint32_t mins) {
    if (g_udp_fd < 0) return;
    UiEventMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.event_type = UI_EVENT_ACTION_STUDY_START_CUSTOM;
    msg.state.duration_minutes = mins;
    sendto(g_udp_fd, &msg, sizeof(msg), 0, (struct sockaddr*)&g_udp_addr, sizeof(g_udp_addr));
}

/* ==================== 前置声明 ==================== */
static void radar_to_fusion_and_dispatch(const RadarState *rs);
static void vision_to_fusion_and_dispatch(const VisionState *vs);
static void vision_focus_reset(int is_distracted);
static uint64_t fusion_monotonic_ms(void);
static void fusion_send_current_state_snapshot(void);

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

static void* tcp_client_handler(void *arg)
{
    int client_fd = *(int*)arg;
    free(arg);

    LOG_DEBUG("TCP client thread started (fd=%d)", client_fd);

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
        } else if (msg_type == MSG_HEARTBEAT) {
            pthread_mutex_lock(&g_asr_socket_mutex);
            g_asr_socket_fd = client_fd;
            pthread_mutex_unlock(&g_asr_socket_mutex);
            LOG_INFO("Registered ASR socket fd = %d", client_fd);
        } else if (msg_type == MSG_DEVICE_CONTROL && msg_len == sizeof(DeviceControl)) {
            DeviceControl *dc = (DeviceControl *)payload;
            LOG_INFO("Recv Device Control: action=%d brightness=%d color_temp=%d", dc->action, dc->brightness, dc->color_temp);
            // Action约定: 0=关闭, 1=常规静态, 2=呼吸, 3=色温切换, 4=绝对亮度设置
            if (dc->action == 3) {
                device_toggle_lamp_color_temp();
            } else if (dc->action == 4) {
                device_set_lamp_brightness_absolute(dc->brightness);
            } else {
                device_control_lamp(dc->action, dc->brightness, dc->color_temp);
            }
        } else if (msg_type == MSG_VISION_STATE && msg_len == sizeof(VisionState)) {
            VisionState *vs = (VisionState *)payload;
            vision_to_fusion_and_dispatch(vs);
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
                    g_fusion_service.current_state = STATE_FOCUSED;
                    vision_focus_reset(0);
                    fs.duration_minutes = 0; // 默认正计时
                    g_fusion_service.config_duration_minutes = 0;
                    g_fusion_service.session_accumulated_ms = 0;
                    g_fusion_service.last_tick_ms = 0;
                    g_fusion_service.played_40m_count = 0;
                    g_fusion_service.has_played_end = 0;
                    LOG_INFO("ASR overridden state to FOCUSED (Free)");
                    fusion_send_ui_event(UI_EVENT_ACTION_STUDY_START_FREE);
                    break;
                case ASR_CMD_STUDY_RESUME:
                    g_fusion_service.current_state = STATE_FOCUSED;
                    vision_focus_reset(0);
                    g_fusion_service.last_tick_ms = 0;
                    LOG_INFO("ASR overridden state to FOCUSED (Resume)");
                    fusion_send_ui_event(UI_EVENT_ACTION_STUDY_RESUME);
                    break;
                case ASR_CMD_STUDY_START_25:
                    g_fusion_service.current_state = STATE_FOCUSED;
                    vision_focus_reset(0);
                    fs.duration_minutes = 25;
                    g_fusion_service.config_duration_minutes = 25;
                    g_fusion_service.session_accumulated_ms = 0;
                    g_fusion_service.last_tick_ms = 0;
                    g_fusion_service.played_40m_count = 0;
                    g_fusion_service.has_played_end = 0;
                    LOG_INFO("ASR overridden state to FOCUSED (25 min)");
                    fusion_send_ui_event(UI_EVENT_ACTION_STUDY_START_25);
                    break;
                case ASR_CMD_STUDY_START_45:
                    g_fusion_service.current_state = STATE_FOCUSED;
                    vision_focus_reset(0);
                    fs.duration_minutes = 45;
                    g_fusion_service.config_duration_minutes = 45;
                    g_fusion_service.session_accumulated_ms = 0;
                    g_fusion_service.last_tick_ms = 0;
                    g_fusion_service.played_40m_count = 0;
                    g_fusion_service.has_played_end = 0;
                    LOG_INFO("ASR overridden state to FOCUSED (45 min)");
                    fusion_send_ui_event(UI_EVENT_ACTION_STUDY_START_45);
                    break;
                case ASR_CMD_STUDY_START_60:
                    g_fusion_service.current_state = STATE_FOCUSED;
                    vision_focus_reset(0);
                    fs.duration_minutes = 60;
                    g_fusion_service.config_duration_minutes = 60;
                    g_fusion_service.session_accumulated_ms = 0;
                    g_fusion_service.last_tick_ms = 0;
                    g_fusion_service.played_40m_count = 0;
                    g_fusion_service.has_played_end = 0;
                    LOG_INFO("ASR overridden state to FOCUSED (60 min)");
                    fusion_send_ui_event(UI_EVENT_ACTION_STUDY_START_60);
                    break;
                default:
                    if (cmd->command_id >= ASR_CMD_STUDY_START_CUSTOM_BASE && 
                        cmd->command_id <= ASR_CMD_STUDY_START_CUSTOM_BASE + 24) {
                        uint32_t custom_mins = (cmd->command_id - ASR_CMD_STUDY_START_CUSTOM_BASE) * 5;
                        g_fusion_service.current_state = STATE_FOCUSED;
                        vision_focus_reset(0);
                        fs.duration_minutes = custom_mins;
                        g_fusion_service.config_duration_minutes = custom_mins;
                        g_fusion_service.session_accumulated_ms = 0;
                        g_fusion_service.last_tick_ms = 0;
                        g_fusion_service.played_40m_count = 0;
                        g_fusion_service.has_played_end = 0;
                        LOG_INFO("ASR overridden state to FOCUSED (%d min)", custom_mins);
                        fusion_send_ui_event_custom_start(custom_mins);
                    }
                    break;
                case ASR_CMD_STUDY_PAUSE:
                    g_fusion_service.current_state = STATE_SEATED_IDLE;
                    vision_focus_reset(0);
                    fs.duration_minutes = 0;
                    g_fusion_service.last_tick_ms = 0;
                    LOG_INFO("ASR overridden state to IDLE (Pause)");
                    fusion_send_ui_event(UI_EVENT_ACTION_STUDY_PAUSE);
                    break;
                case ASR_CMD_STUDY_STOP:
                    g_fusion_service.current_state = STATE_SEATED_IDLE;
                    vision_focus_reset(0);
                    fs.duration_minutes = 0;
                    g_fusion_service.session_accumulated_ms = 0;
                    g_fusion_service.last_tick_ms = 0;
                    LOG_INFO("ASR overridden state to IDLE (Stop)");
                    fusion_send_ui_event(UI_EVENT_ACTION_STUDY_STOP);
                    break;
                case ASR_CMD_LAMP_ON:
                    LOG_INFO("ASR requested Lamp ON");
                    device_control_lamp(1, 80, 4000); // 默认80%亮度，4000K
                    break;
                case ASR_CMD_LAMP_OFF:
                    LOG_INFO("ASR requested Lamp OFF");
                    device_control_lamp(0, 0, 0);
                    break;
                case ASR_CMD_LAMP_TOGGLE_COLOR_TEMP:
                    LOG_INFO("ASR requested Lamp Toggle Color Temp");
                    device_toggle_lamp_color_temp();
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
                case ASR_CMD_STUDY_DISTRACTED:
                    g_fusion_service.current_state = STATE_DISTRACTED;
                    vision_focus_reset(1);
                    LOG_INFO("Vision triggered DISTRACTED");
                    break;
                case ASR_CMD_STUDY_FOCUSED:
                    g_fusion_service.current_state = STATE_FOCUSED;
                    vision_focus_reset(0);
                    LOG_INFO("Vision triggered FOCUSED");
                    break;
            }
            fs.current_state = g_fusion_service.current_state;
            pthread_mutex_unlock(&g_fusion_service.mutex);
            
            // 只有状态改变时才向外广播状态，触发后续联动（UI/灯光）
            if ((cmd->command_id >= ASR_CMD_STUDY_START && cmd->command_id <= ASR_CMD_STUDY_START_60) ||
                (cmd->command_id >= ASR_CMD_STUDY_START_CUSTOM_BASE && cmd->command_id <= ASR_CMD_STUDY_START_CUSTOM_BASE + 24) ||
                cmd->command_id == ASR_CMD_STUDY_DISTRACTED || cmd->command_id == ASR_CMD_STUDY_FOCUSED) {
                fusion_send_state(&fs);
                device_handle_fusion_state(&fs);
            }
        } else {
            LOG_DEBUG("Recv unknown msg: type=0x%02X len=%u", msg_type, msg_len);
        }
    }

    pthread_mutex_lock(&g_asr_socket_mutex);
    if (client_fd == g_asr_socket_fd) {
        g_asr_socket_fd = -1;
    }
    pthread_mutex_unlock(&g_asr_socket_mutex);
    close(client_fd);
    LOG_DEBUG("TCP client thread disconnected (fd=%d)", client_fd);
    return NULL;
}

static void* tcp_server_thread(void *arg)
{
    struct sockaddr_in addr;
    int listen_fd, opt = 1;
    uint64_t last_state_heartbeat_ms = 0;

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
        uint64_t now_ms = fusion_monotonic_ms();
        if (last_state_heartbeat_ms == 0 ||
            now_ms - last_state_heartbeat_ms >= FUSION_STATE_HEARTBEAT_MS) {
            fusion_send_current_state_snapshot();
            last_state_heartbeat_ms = now_ms;
        }
        if (ret <= 0) continue;

        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("TCP accept failed: %s", strerror(errno));
            continue;
        }

        int *client_fd_ptr = malloc(sizeof(int));
        if (client_fd_ptr) {
            *client_fd_ptr = client_fd;
            pthread_t client_tid;
            if (pthread_create(&client_tid, NULL, tcp_client_handler, client_fd_ptr) != 0) {
                LOG_ERROR("Failed to create TCP client thread: %s", strerror(errno));
                close(client_fd);
                free(client_fd_ptr);
            } else {
                pthread_detach(client_tid);
            }
        } else {
            close(client_fd);
        }

    }

    close(listen_fd);
    g_server_fd = -1;
    LOG_INFO("TCP server stopped");
    return NULL;
}

static uint64_t fusion_monotonic_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static void fusion_send_current_state_snapshot(void)
{
    UiEventMessage msg;

    if (g_udp_fd < 0) {
        return;
    }

    memset(&msg, 0, sizeof(msg));
    msg.event_type = UI_EVENT_STATE_UPDATE;

    pthread_mutex_lock(&g_fusion_service.mutex);
    msg.state.current_state = g_fusion_service.current_state;
    msg.state.state_score = g_fusion_service.latest_vision.face_quality;
    msg.state.intervention_level = 0;
    msg.state.duration_minutes = 0;
    msg.state.timestamp = time(NULL);
    pthread_mutex_unlock(&g_fusion_service.mutex);

    sendto(g_udp_fd, &msg, sizeof(msg), 0,
        (struct sockaddr *)&g_udp_addr, sizeof(g_udp_addr));
}

static void vision_focus_reset(int is_distracted)
{
    memset(&g_vision_filter, 0, sizeof(g_vision_filter));
    g_vision_filter.is_distracted = is_distracted;
}

static float vision_face_diag_sq(const VisionState *vs)
{
    float dx;
    float dy;

    if (vs == NULL || vs->face_present == 0) {
        return 0.0f;
    }

    dx = vs->face_x2 - vs->face_x1;
    dy = vs->face_y2 - vs->face_y1;
    if (dx <= 0.0f || dy <= 0.0f) {
        return 0.0f;
    }

    return dx * dx + dy * dy;
}

static int vision_face_center_in_seat_region(const VisionState *vs, float *cx_out, float *cy_out)
{
    const float min_x = VISION_FRAME_WIDTH * VISION_SEAT_CENTER_MIN_X_RATIO;
    const float max_x = VISION_FRAME_WIDTH * VISION_SEAT_CENTER_MAX_X_RATIO;
    const float min_y = VISION_FRAME_HEIGHT * VISION_SEAT_CENTER_MIN_Y_RATIO;
    const float max_y = VISION_FRAME_HEIGHT * VISION_SEAT_CENTER_MAX_Y_RATIO;
    float cx = 0.0f;
    float cy = 0.0f;

    if (vs != NULL && vs->face_present != 0 &&
        vs->face_x2 > vs->face_x1 && vs->face_y2 > vs->face_y1) {
        cx = (vs->face_x1 + vs->face_x2) * 0.5f;
        cy = (vs->face_y1 + vs->face_y2) * 0.5f;
    }

    if (cx_out != NULL) {
        *cx_out = cx;
    }
    if (cy_out != NULL) {
        *cy_out = cy;
    }

    return cx >= min_x && cx <= max_x && cy >= min_y && cy <= max_y;
}

static int vision_seat_update(const VisionState *vs, float *diag_sq_out,
    float *face_cx_out, float *face_cy_out, int *center_ok_out)
{
    const float enter_sq = VISION_SEAT_ENTER_DIAG * VISION_SEAT_ENTER_DIAG;
    const float exit_sq = VISION_SEAT_EXIT_DIAG * VISION_SEAT_EXIT_DIAG;
    float diag_sq = vision_face_diag_sq(vs);
    float face_cx = 0.0f;
    float face_cy = 0.0f;
    int center_ok = vision_face_center_in_seat_region(vs, &face_cx, &face_cy);
    int seen_as_seated_enter = (vs != NULL && vs->face_present != 0 &&
        center_ok && diag_sq >= enter_sq);
    int seen_as_seated_keep = (vs != NULL && vs->face_present != 0 &&
        center_ok && diag_sq >= exit_sq);
    uint64_t now_ms = fusion_monotonic_ms();

    if (diag_sq_out != NULL) {
        *diag_sq_out = diag_sq;
    }
    if (face_cx_out != NULL) {
        *face_cx_out = face_cx;
    }
    if (face_cy_out != NULL) {
        *face_cy_out = face_cy;
    }
    if (center_ok_out != NULL) {
        *center_ok_out = center_ok;
    }

    if (g_vision_seat_filter.seated) {
        if (seen_as_seated_keep) {
            g_vision_seat_filter.exit_count = 0;
        } else if (++g_vision_seat_filter.exit_count >= VISION_SEAT_EXIT_SAMPLES) {
            g_vision_seat_filter.seated = 0;
            g_vision_seat_filter.enter_count = 0;
            g_vision_seat_filter.exit_count = 0;
        }
    } else {
        if (seen_as_seated_enter) {
            if (++g_vision_seat_filter.enter_count >= VISION_SEAT_ENTER_SAMPLES) {
                g_vision_seat_filter.seated = 1;
                g_vision_seat_filter.enter_count = 0;
                g_vision_seat_filter.exit_count = 0;
            }
        } else {
            g_vision_seat_filter.enter_count = 0;
        }
    }

    if (g_vision_seat_filter.last_debug_ms == 0 ||
        now_ms - g_vision_seat_filter.last_debug_ms >= VISION_SEAT_DEBUG_INTERVAL_MS) {
        LOG_INFO("Vision seat sample: face=%u diag=%.1f center=(%.1f,%.1f) center_ok=%d seated=%d enter_count=%d exit_count=%d",
                 vs != NULL ? vs->face_present : 0,
                 diag_sq > 0.0f ? sqrtf(diag_sq) : 0.0f,
                 face_cx, face_cy, center_ok, g_vision_seat_filter.seated,
                 g_vision_seat_filter.enter_count, g_vision_seat_filter.exit_count);
        g_vision_seat_filter.last_debug_ms = now_ms;
    }

    return g_vision_seat_filter.seated;
}

static void vision_focus_clear_votes(void)
{
    memset(g_vision_filter.votes, 0, sizeof(g_vision_filter.votes));
    g_vision_filter.vote_index = 0;
    g_vision_filter.vote_count = 0;
}

static void vision_focus_push_vote(VisionFocusVote vote)
{
    if (vote == VISION_VOTE_UNKNOWN) {
        return;
    }

    g_vision_filter.votes[g_vision_filter.vote_index] = vote;
    g_vision_filter.vote_index =
        (g_vision_filter.vote_index + 1) % VISION_FOCUS_WINDOW_SIZE;
    if (g_vision_filter.vote_count < VISION_FOCUS_WINDOW_SIZE) {
        g_vision_filter.vote_count++;
    }
}

static void vision_focus_count_votes(int *focused_count, int *distracted_count)
{
    int i;
    int focused = 0;
    int distracted = 0;

    for (i = 0; i < g_vision_filter.vote_count; ++i) {
        if (g_vision_filter.votes[i] == VISION_VOTE_FOCUSED) {
            focused++;
        } else if (g_vision_filter.votes[i] == VISION_VOTE_DISTRACTED) {
            distracted++;
        }
    }

    *focused_count = focused;
    *distracted_count = distracted;
}

static int vision_attention_is_distracting(uint8_t region)
{
    return region == VISION_ATTENTION_LEFT ||
           region == VISION_ATTENTION_RIGHT ||
           region == VISION_ATTENTION_UP ||
           region == VISION_ATTENTION_DOWN;
}

static VisionFocusVote vision_state_to_vote(const VisionState *vs)
{
    uint64_t now_ms;

    if (vs == NULL) {
        return VISION_VOTE_UNKNOWN;
    }

    now_ms = (vs->timestamp != 0) ? vs->timestamp : fusion_monotonic_ms();

    if (vs->face_present == 0 ||
        vs->attention_region == VISION_ATTENTION_NO_FACE ||
        vs->attention_region == VISION_ATTENTION_ERROR ||
        vs->attention_region == VISION_ATTENTION_UNKNOWN) {
        g_vision_filter.last_attention_region = vs->attention_region;
        g_vision_filter.eyes_closed_since_ms = 0;
        return VISION_VOTE_UNKNOWN;
    }

    if (g_vision_filter.last_attention_region != vs->attention_region) {
        g_vision_filter.last_attention_region = vs->attention_region;
        g_vision_filter.eyes_closed_since_ms = 0;
        if (vs->attention_region == VISION_ATTENTION_FRONT) {
            g_vision_filter.blink_initialized = 1;
            g_vision_filter.last_blink_count = vs->blink_count;
            g_vision_filter.last_blink_ms = now_ms;
        }
    }

    if (vision_attention_is_distracting(vs->attention_region)) {
        return VISION_VOTE_DISTRACTED;
    }
    if (vs->yawn_prob >= 0.5f) {
        return VISION_VOTE_DISTRACTED;
    }

    if (!g_vision_filter.blink_initialized ||
        vs->blink_count != g_vision_filter.last_blink_count) {
        g_vision_filter.blink_initialized = 1;
        g_vision_filter.last_blink_count = vs->blink_count;
        g_vision_filter.last_blink_ms = now_ms;
    } else if (now_ms >= g_vision_filter.last_blink_ms &&
               now_ms - g_vision_filter.last_blink_ms >= VISION_NO_BLINK_TIMEOUT_MS) {
        return VISION_VOTE_DISTRACTED;
    }

    if (vs->eye_closed_prob >= 0.5f) {
        if (g_vision_filter.eyes_closed_since_ms == 0) {
            g_vision_filter.eyes_closed_since_ms = now_ms;
        } else if (now_ms >= g_vision_filter.eyes_closed_since_ms &&
                   now_ms - g_vision_filter.eyes_closed_since_ms >= VISION_EYES_CLOSED_TIMEOUT_MS) {
            return VISION_VOTE_DISTRACTED;
        }
    } else {
        g_vision_filter.eyes_closed_since_ms = 0;
    }

    return VISION_VOTE_FOCUSED;
}

static int vision_focus_apply_vote(VisionFocusVote vote, uint64_t now_ms, LearningState *new_state)
{
    int focused_count = 0;
    int distracted_count = 0;

    if (vote == VISION_VOTE_UNKNOWN) {
        return 0;
    }

    vision_focus_push_vote(vote);

    if (g_vision_filter.vote_count < VISION_MIN_VALID_SAMPLES) {
        return 0;
    }

    if (g_vision_filter.last_transition_ms != 0 &&
        now_ms >= g_vision_filter.last_transition_ms &&
        now_ms - g_vision_filter.last_transition_ms < VISION_TRANSITION_COOLDOWN_MS) {
        return 0;
    }

    vision_focus_count_votes(&focused_count, &distracted_count);

    if (!g_vision_filter.is_distracted &&
        distracted_count >= VISION_DISTRACTED_SAMPLE_THRESHOLD) {
        g_vision_filter.is_distracted = 1;
        g_vision_filter.last_transition_ms = now_ms;
        vision_focus_clear_votes();
        *new_state = STATE_DISTRACTED;
        LOG_INFO("Vision vote: enter DISTRACTED (good=%d bad=%d)",
                 focused_count, distracted_count);
        return 1;
    }

    if (g_vision_filter.is_distracted &&
        focused_count >= VISION_FOCUSED_SAMPLE_THRESHOLD) {
        g_vision_filter.is_distracted = 0;
        g_vision_filter.last_transition_ms = now_ms;
        vision_focus_clear_votes();
        *new_state = STATE_FOCUSED;
        LOG_INFO("Vision vote: recover FOCUSED (good=%d bad=%d)",
                 focused_count, distracted_count);
        return 1;
    }

    return 0;
}

static void vision_to_fusion_and_dispatch(const VisionState *vs)
{
    FusionState fs;
    LearningState next_state = STATE_FOCUSED;
    VisionFocusVote vote;
    int should_dispatch = 0;
    uint64_t now_ms;
    int seated;
    float face_diag_sq = 0.0f;
    float face_cx = 0.0f;
    float face_cy = 0.0f;
    int center_ok = 0;

    if (vs == NULL) {
        return;
    }

    memset(&fs, 0, sizeof(fs));
    fs.state_score = vs->face_quality;
    fs.intervention_level = 0;
    fs.duration_minutes = 0;
    fs.timestamp = time(NULL);

    pthread_mutex_lock(&g_fusion_service.mutex);
    g_fusion_service.latest_vision = *vs;
    seated = vision_seat_update(vs, &face_diag_sq, &face_cx, &face_cy, &center_ok);

    if (!seated) {
        if (g_fusion_service.current_state != STATE_ABSENT) {
            g_vision_seat_filter.state_before_absent = g_fusion_service.current_state;
            g_fusion_service.current_state = STATE_ABSENT;
            should_dispatch = 1;
            vision_focus_reset(0);
            LOG_INFO("Vision seat -> ABSENT (face=%u diag=%.1f center=(%.1f,%.1f) center_ok=%d)",
                     vs->face_present, face_diag_sq > 0.0f ? sqrtf(face_diag_sq) : 0.0f,
                     face_cx, face_cy, center_ok);
        }
        fs.current_state = g_fusion_service.current_state;
        pthread_mutex_unlock(&g_fusion_service.mutex);

        if (should_dispatch) {
            fusion_send_state(&fs);
            device_handle_fusion_state(&fs);
        }
        return;
    }

    if (g_fusion_service.current_state == STATE_ABSENT) {
        LearningState restore_state = g_vision_seat_filter.state_before_absent;
        if (restore_state == STATE_ABSENT || restore_state == STATE_DISTRACTED) {
            restore_state = STATE_FOCUSED;
        }
        g_fusion_service.current_state = restore_state;
        g_fusion_service.last_tick_ms = (vs->timestamp != 0) ? vs->timestamp : fusion_monotonic_ms();
        vision_focus_reset(restore_state == STATE_DISTRACTED);
        should_dispatch = 1;
        LOG_INFO("Vision seat -> PRESENT (face=%u diag=%.1f center=(%.1f,%.1f) center_ok=%d restore=%d)",
                 vs->face_present, face_diag_sq > 0.0f ? sqrtf(face_diag_sq) : 0.0f,
                 face_cx, face_cy, center_ok, restore_state);
    }

    if (g_fusion_service.current_state != STATE_FOCUSED &&
        g_fusion_service.current_state != STATE_DISTRACTED) {
        vision_focus_reset(0);
        pthread_mutex_unlock(&g_fusion_service.mutex);
        if (should_dispatch) {
            fs.current_state = g_fusion_service.current_state;
            fusion_send_state(&fs);
            device_handle_fusion_state(&fs);
        }
        return;
    }

    if (g_fusion_service.current_state == STATE_DISTRACTED) {
        g_vision_filter.is_distracted = 1;
    }

    now_ms = (vs->timestamp != 0) ? vs->timestamp : fusion_monotonic_ms();
    vote = vision_state_to_vote(vs);
    if (vision_focus_apply_vote(vote, now_ms, &next_state)) {
        if (g_fusion_service.current_state != next_state) {
            g_fusion_service.current_state = next_state;
            should_dispatch = 1;
            LOG_INFO("Vision fusion state -> %s",
                next_state == STATE_DISTRACTED ? "DISTRACTED" : "FOCUSED");
        }
    }

    fs.current_state = g_fusion_service.current_state;
    
    // ==========================================
    // Timer Logic for voice prompts
    // ==========================================
    if (g_fusion_service.current_state == STATE_FOCUSED || g_fusion_service.current_state == STATE_DISTRACTED) {
        if (g_fusion_service.last_tick_ms > 0) {
            g_fusion_service.session_accumulated_ms += (now_ms - g_fusion_service.last_tick_ms);
        }
        g_fusion_service.last_tick_ms = now_ms;
        
        // Positive timer (config_duration_minutes == 0)
        if (g_fusion_service.config_duration_minutes == 0) {
            uint32_t current_40m_count = g_fusion_service.session_accumulated_ms / (40ULL * 60ULL * 1000ULL);
            if (current_40m_count > g_fusion_service.played_40m_count) {
                g_fusion_service.played_40m_count = current_40m_count;
                fusion_send_asr_command(ASR_CMD_PLAY_BREAK_40M);
            }
        } 
        // Countdown timer
        else {
            if (g_fusion_service.session_accumulated_ms >= g_fusion_service.config_duration_minutes * 60ULL * 1000ULL) {
                if (!g_fusion_service.has_played_end) {
                    g_fusion_service.has_played_end = 1;
                    fusion_send_asr_command(ASR_CMD_PLAY_END_REST);
                    
                    // Auto-end the study session
                    g_fusion_service.current_state = STATE_SEATED_IDLE;
                    vision_focus_reset(0);
                    fs.duration_minutes = 0;
                    g_fusion_service.session_accumulated_ms = 0;
                    g_fusion_service.last_tick_ms = 0;
                    fusion_send_ui_event(UI_EVENT_ACTION_STUDY_STOP);
                    
                    fs.current_state = g_fusion_service.current_state;
                    should_dispatch = 1;
                }
            }
        }
    } else {
        g_fusion_service.last_tick_ms = 0; // reset tick if not focused
    }

    if (g_fusion_service.current_state == STATE_DISTRACTED && next_state == STATE_DISTRACTED && should_dispatch) {
        // Just entered distracted state
        fusion_send_asr_command(ASR_CMD_PLAY_DISTRACTED);
    }
    // ==========================================
    
    pthread_mutex_unlock(&g_fusion_service.mutex);

    if (should_dispatch) {
        fusion_send_state(&fs);
        device_handle_fusion_state(&fs);
    }
}

/* 雷达已不再参与入座/离座判断。摄像头的人脸框大小是当前唯一在座依据。 */
static void radar_to_fusion_and_dispatch(const RadarState *rs)
{
    (void)rs;
}

int fusion_service_init(const Config *config) {
    LOG_INFO("Initializing fusion service...");

    memset(&g_fusion_service, 0, sizeof(FusionService));
    g_fusion_service.config = *config;
    g_fusion_service.running = 0;
    g_fusion_service.current_state = STATE_SEATED_IDLE;
    vision_focus_reset(0);
    memset(&g_vision_seat_filter, 0, sizeof(g_vision_seat_filter));
    g_vision_seat_filter.seated = 1;
    g_vision_seat_filter.state_before_absent = STATE_SEATED_IDLE;
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
