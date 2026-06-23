#include "radar_service.h"
#include "radar_protocol.h"
#include "hi_uart.h"
#include "logger.h"
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <termios.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include "radar_model_inference.h"

/* ==================== 算法参数宏 ==================== */
#define MAX_DISTANCE_DM    20    /* 雷达最大探测距离 (7~100dm, 20=2.0m) */
#define MONITOR_GATE       0     /* 监视的距离门 (0=桌面极近场) */
#define WINDOW_SIZE        10    /* 滑动窗口: 10帧 = 1秒 @10Hz */
#define TIME_IN_FRAMES     50    /* 入座判定缓冲: 5秒 (50帧) */
#define TIME_OUT_FRAMES    50    /* 离座判定缓冲: 5秒 (50帧) */

/* 新增模型相关全局变量 */
static double g_features_window[WINDOW_SIZE][32]; // 保存过去10帧的32个通道数据
static int g_model_preds[WINDOW_SIZE];            // 保存过去10帧的模型预测结果
static int g_current_state = -1;                  // 雷达当前锁定的全局状态
static int g_last_state = -1;                     // 上一状态，用于检测跳变
static int g_state_confidence[3] = {0, 0, 0};     // 漏桶积分置信度 (AWAY, NORMAL, FIDGET)

/* ---- 判定阈值 ---- */
#define ENERGY_TH_IN_G0    38.0  /* 入座的 G0(近距离门) 能量下限。必须大于 38 才能入座 */
#define ENERGY_TH_OUT      35.0  /* 离座的能量上限 (G0 < 35 判定离开) */
#define VAR_TH_FIDGET      15.0  /* 乱动全局方差阈值 (v > 15 判定为大幅乱动) */

/* ==================== 三态有限状态机 ==================== */
typedef enum {
    STATE_AWAY,    /* 没人 */
    STATE_NORMAL,  /* 安静微动 */
    STATE_FIDGET   /* 大幅乱动 */
} PersonState;

/* ==================== 全局变量 ==================== */
static RadarService g_radar_service;
static pthread_t     g_process_thread;
static int           g_running = 0;

/* 帧重叠缓冲: 防止 141 字节帧跨越 recv 边界丢失 */
static unsigned char g_overlap[140];
static int           g_overlap_len = 0;

/* 滑动窗口: 存储最近 10 帧的能量值 */
static double g_window[WINDOW_SIZE];
static double g_window_g0[WINDOW_SIZE];
static double g_window_g1[WINDOW_SIZE];
static int    g_win_idx   = 0;
static int    g_win_count = 0;

/* 漏桶积分器 */
static int g_count_in  = 0;
static int g_count_out = 0;

/* TCP 连接 */
static int g_sock_fd = -1;

/* ==================== TCP 发送 ==================== */

static int connect_to_fusion(const Config *config)
{
    struct sockaddr_in addr;

    g_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock_fd < 0) {
        LOG_ERROR("TCP socket create failed: %s", strerror(errno));
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(config->fusion_port);
    inet_pton(AF_INET, config->fusion_host, &addr.sin_addr);

    if (connect(g_sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("TCP connect to %s:%d failed: %s",
                  config->fusion_host, config->fusion_port, strerror(errno));
        close(g_sock_fd);
        g_sock_fd = -1;
        return -1;
    }

    LOG_INFO("TCP connected to fusion %s:%d", config->fusion_host, config->fusion_port);
    g_last_state = -1;
    return 0;
}

static int send_message(MessageType type, const void *payload, uint32_t payload_len)
{
    uint8_t buf[4 + 4 + 256];
    uint32_t type_be = htonl((uint32_t)type);
    uint32_t len_be  = htonl(payload_len);

    memcpy(buf,         &type_be, 4);
    memcpy(buf + 4,     &len_be,  4);
    memcpy(buf + 8,     payload,  payload_len);

    size_t total = 8 + payload_len;
    ssize_t sent = send(g_sock_fd, buf, total, MSG_NOSIGNAL);
    if (sent != (ssize_t)total) {
        LOG_ERROR("TCP send failed: %s", strerror(errno));
        close(g_sock_fd);
        g_sock_fd = -1;
        return -1;
    }
    return 0;
}

/* ==================== 辅助函数 ==================== */

static const char* state_name(PersonState s)
{
    switch (s) {
        case STATE_AWAY:   return "AWAY";
        case STATE_NORMAL: return "NORMAL";
        case STATE_FIDGET: return "FIDGET";
        default:           return "UNKNOWN";
    }
}

/* 根据 FSM 状态构建 RadarState 并发送 */
static void send_radar_state(const RadarData *rd)
{
    RadarState state;
    memset(&state, 0, sizeof(state));

    state.presence  = (g_current_state == STATE_AWAY) ? 0 : 1;
    state.distance  = rd->distance_cm / 100.0f; /* cm -> m */
    state.timestamp = time(NULL);
    state.radar_quality = 1.0f;

    switch (g_current_state) {
        case STATE_AWAY:
            state.motion_level = 0.0f;
            break;
        case STATE_NORMAL:
            state.motion_level = 0.3f;
            break;
        case STATE_FIDGET:
            state.motion_level = 0.8f;
            break;
    }

    radar_send_state(&state);
}

/* ==================== 雷达数据处理线程 ==================== */
static void* radar_process_thread(void *arg)
{
    LOG_INFO("Radar process thread started");

    /* -- 步骤 1: 下发配置, 让雷达从文本模式切换到能量上报二进制模式 -- */
    radar_reboot(g_radar_service.serial_fd);
    radar_configure_all(g_radar_service.serial_fd, MAX_DISTANCE_DM);
    tcflush(g_radar_service.serial_fd, TCIOFLUSH); /* 清空启动瞬间的脏数据 */

    LOG_INFO("Radar configured: max_dist_dm=%d, monitor_gate=%d", MAX_DISTANCE_DM, MONITOR_GATE);

    /* 积分/窗口复位 */
    g_win_idx = 0;
    g_win_count = 0;
    memset(g_features_window, 0, sizeof(g_features_window));
    g_count_in = 0;
    g_current_state = STATE_AWAY;
    g_last_state = STATE_AWAY;
    g_state_confidence[0] = 0;
    g_state_confidence[1] = 0;
    g_state_confidence[2] = 0;
    g_last_state = STATE_AWAY;
    g_overlap_len = 0;

    while (g_running) {
        unsigned char buf[512 + 140]; /* 140 字节留给前次重叠 */
        int offset = 0;
        int total, i;
        RadarData radar_data;

        /* 把上一次读取的遗留字节拷贝到 buffer 开头 */
        if (g_overlap_len > 0) {
            memcpy(buf, g_overlap, g_overlap_len);
            offset = g_overlap_len;
        }

        int ret = hi_serial_recv(g_radar_service.serial_fd, (char *)(buf + offset), 256);
        if (ret <= 0) {
            continue;
        }
        total = offset + ret;

        /* -- 步骤 2: 从字节流中解析一帧雷达数据, 成功返回消耗的字节数, 失败返回 0 */
        int consumed = parse_radar_frame(buf, total, &radar_data);
        if (consumed <= 0) {
            /* 尝试重连并强制重发状态 */
            if (g_sock_fd < 0) {
                if (connect_to_fusion(&g_radar_service.config) == 0) {
                    g_last_state = -1; /* 连上后强制同步一次状态 */
                }
            }

            /* 没找到完整帧, 更新重叠逻辑 */
            if (total >= 140) {
                memcpy(g_overlap, buf + total - 140, 140);
                g_overlap_len = 140;
            } else {
                memcpy(g_overlap, buf, total);
                g_overlap_len = total;
            }
            continue;
        }

        /* -- 步骤 3: 处理剩余数据作为下一次的重叠 */
        g_overlap_len = total - consumed;
        if (g_overlap_len > 140) g_overlap_len = 140;
        if (g_overlap_len > 0) {
            memcpy(g_overlap, buf + consumed, g_overlap_len);
        }

        /* -- 步骤 4: 记录 32 个通道的数据到滑动窗口 -- */
        for (i = 0; i < 16; i++) {
            g_features_window[g_win_idx][i]      = radar_data.motion_energy_db[i];
            g_features_window[g_win_idx][i + 16] = radar_data.static_energy_db[i];
        }

        /* -- 步骤 5: 执行模型推理 (获取当前帧的预测) -- */
        int current_pred = radar_model_predict(g_features_window);
        g_model_preds[g_win_idx] = current_pred;
        
        /* 移动滑动窗口指针 */
        g_win_idx = (g_win_idx + 1) % WINDOW_SIZE;

        /* -- 步骤 6: 一秒之内（10帧）的多数表决 -- */
        int count[3] = {0, 0, 0};
        for (i = 0; i < WINDOW_SIZE; i++) {
            if (g_model_preds[i] >= 0 && g_model_preds[i] <= 2) {
                count[g_model_preds[i]]++;
            }
        }
        
        int majority_pred = 0;
        if (count[1] > count[0] && count[1] >= count[2]) majority_pred = 1;
        else if (count[2] > count[0] && count[2] > count[1]) majority_pred = 2;
        else majority_pred = 0;

        /* -- 步骤 7: 积分漏桶与非对称状态机 (Asymmetric Leaky Bucket) -- */
        int s;
        for (s = 0; s < 3; s++) {
            if (s == majority_pred) {
                g_state_confidence[s] += 2; /* 预测对加2分 (漏桶注水) */
            } else {
                g_state_confidence[s] -= 1; /* 预测错缓慢扣1分 (漏桶漏水) */
            }
            /* 限制置信度在 0 ~ 200 之间 */
            if (g_state_confidence[s] > 200) g_state_confidence[s] = 200;
            if (g_state_confidence[s] < 0)   g_state_confidence[s] = 0;
        }

        int next_state = g_current_state;

        if (g_current_state == STATE_AWAY) {
            /* 场景 1: 入座极快。只需要 15 帧 (1.5秒) * 2 = 30 分 */
            if (g_state_confidence[STATE_NORMAL] >= 30) {
                next_state = STATE_NORMAL;
            }
        } 
        else if (g_current_state == STATE_NORMAL) {
            /* 场景 2: 离座极慢。需要 80 帧 (8.0秒) * 2 = 160 分 */
            if (g_state_confidence[STATE_AWAY] >= 160) {
                next_state = STATE_AWAY;
            }
            /* 场景 3: 乱动适中。需要 30 帧 (3.0秒) * 2 = 60 分 */
            else if (g_state_confidence[STATE_FIDGET] >= 60) {
                next_state = STATE_FIDGET;
            }
        }
        else if (g_current_state == STATE_FIDGET) {
            /* 场景 4: 恢复正常。需要 50 帧 (5.0秒) * 2 = 100 分 */
            if (g_state_confidence[STATE_NORMAL] >= 100) {
                next_state = STATE_NORMAL;
            }
            /* 场景 5: 乱动时直接离开 (极少数情况) */
            else if (g_state_confidence[STATE_AWAY] >= 160) {
                next_state = STATE_AWAY;
            }
        }

        /* 执行状态切换 */
        if (next_state != g_current_state) {
            LOG_INFO("<<< STATE CHANGED: %s -> %s >>> (Leaky Bucket triggered)", 
                     state_name(g_current_state), state_name(next_state));
            
            /* 状态切换后，重置旧状态的置信度，并给新状态加上初始分数，防止反复横跳 */
            g_state_confidence[g_current_state] = 0;
            g_state_confidence[next_state] = 100; // 给新状态一个较高初始值，避免立刻跌落
            
            g_current_state = next_state;
        }

        /* -- 步骤 8: 周期心跳日志 (每 ~1 秒), 便于观测内部状态 -- */
        {
            static int heartbeat = 0;
            if (++heartbeat >= 10) {
                heartbeat = 0;
                LOG_INFO("TGT:%d | 预测:[%d %d %d] | 多数决:%s | 置信度:[%d %d %d] | 状态:%s",
                         radar_data.has_target,
                         count[0], count[1], count[2],
                         state_name(majority_pred), 
                         g_state_confidence[0], g_state_confidence[1], g_state_confidence[2],
                         state_name(g_current_state));
            }
        }

        /* -- 步骤 9: 状态发生改变时，推送给 Fusion 服务 -- */
        if (g_current_state != g_last_state) {
            LOG_INFO("Radar FSM: %s -> %s (dist=%d cm)",
                     state_name(g_last_state), state_name(g_current_state),
                     radar_data.distance_cm);
            send_radar_state(&radar_data);
            g_last_state = g_current_state;
        }
    }

    LOG_INFO("Radar process thread stopped");
    return NULL;
}

/* ==================== 服务生命周期 ==================== */

int radar_service_init(const Config *config, int uart_fd)
{
    LOG_INFO("Initializing radar service...");

    memset(&g_radar_service, 0, sizeof(RadarService));
    g_radar_service.config = *config;
    g_radar_service.running = 0;
    g_radar_service.serial_fd = uart_fd;

    g_overlap_len = 0;
    memset(g_overlap, 0, sizeof(g_overlap));
    memset(g_window, 0, sizeof(g_window));

    LOG_INFO("Radar service initialized (uart_fd=%d)", uart_fd);
    return 0;
}

int radar_service_start()
{
    LOG_INFO("Starting radar service...");

    g_running = 1;
    g_radar_service.running = 1;

    if (pthread_create(&g_process_thread, NULL, radar_process_thread, NULL) != 0) {
        LOG_ERROR("Failed to create radar process thread");
        return -1;
    }

    LOG_INFO("Radar service started");
    return 0;
}

void radar_service_stop()
{
    LOG_INFO("Stopping radar service...");

    g_running = 0;
    g_radar_service.running = 0;

    pthread_join(g_process_thread, NULL);

    LOG_INFO("Radar service stopped");
}

void radar_service_cleanup()
{
    LOG_INFO("Cleaning up radar service...");

    if (g_radar_service.serial_fd >= 0) {
        hi_serial_close(g_radar_service.serial_fd);
        g_radar_service.serial_fd = -1;
    }

    if (g_sock_fd >= 0) {
        close(g_sock_fd);
        g_sock_fd = -1;
    }

    LOG_INFO("Radar service cleaned up");
}

int radar_send_state(const RadarState *state)
{
    /* 惰性连接: 首次发送或断线后自动重连 */
    if (g_sock_fd < 0) {
        if (connect_to_fusion(&g_radar_service.config) != 0) {
            return -1;
        }
    }

    if (send_message(MSG_RADAR_STATE, state, sizeof(RadarState)) != 0) {
        return -1;
    }

    LOG_DEBUG("Radar state sent: presence=%d, motion=%.2f, distance=%.2f",
              state->presence, state->motion_level, state->distance);
    return 0;
}
