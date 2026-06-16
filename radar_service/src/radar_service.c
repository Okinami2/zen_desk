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

/* ==================== 算法参数宏 ==================== */
#define MAX_DISTANCE_DM    20    /* 雷达最大探测距离 (7~100dm, 20=2.0m) */
#define MONITOR_GATE       0     /* 监视的距离门 (0=桌面极近场) */
#define WINDOW_SIZE        10    /* 滑动窗口: 10帧 = 1秒 @10Hz */
#define TIME_IN_FRAMES     50    /* 入座判定缓冲: 5秒 (50帧) */
#define TIME_OUT_FRAMES    50    /* 离座判定缓冲: 5秒 (50帧) */

/* ---- 判定阈值 ---- */
#define ENERGY_TH_IN       38.0  /* 判断入座的能量均值下限 (m > 38.0) */
#define ENERGY_TH_OUT      37.0  /* 判断离座的能量均值上限 (m < 37.0) */
#define VAR_TH_FIDGET      40.0  /* 乱动全局方差阈值 (v > 40.0判定为乱动) */

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

/* FSM 状态 */
static PersonState g_current_state = STATE_AWAY;
static PersonState g_last_state    = STATE_AWAY;

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

/* ==================== 距离门阈值表 (从 Windows 侧标定结果直接移植) ==================== */

/* 入座触发阈值: 远场依赖此值过滤多径幽灵; 0-1门设为 50.00 故意让均值条件永久失效 */
static const double g_trigger_th[16] = {
    50.00, 50.00, 22.00, 20.00,
    28.50, 27.00, 21.00, 20.00,
    17.50, 18.50, 17.50, 17.00,
    19.00, 16.50, 17.00, 16.00
};

/* 离座保持阈值: 迟滞门限, 能量跌破此值才开始离座倒计时 */
static const double g_maintain_th[16] = {
    50.00, 50.00, 16.00, 15.00,
    26.50, 24.50, 19.50, 18.00,
    15.50, 16.50, 15.50, 15.00,
    17.00, 14.50, 15.00, 14.00
};

/* 微动方差阈值: 每个门独立的抗干扰底线 */
static const double g_var_th_motion[16] = {
    1.5, 1.5, 3.0, 5.0,
    5.0, 5.0, 5.0, 5.0,
    5.0, 5.0, 5.0, 5.0,
    5.0, 5.0, 5.0, 5.0
};

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
    // radar_configure_all(g_radar_service.serial_fd, MAX_DISTANCE_DM);
    tcflush(g_radar_service.serial_fd, TCIOFLUSH); /* 清空启动瞬间的脏数据 */

    LOG_INFO("Radar configured: max_dist_dm=%d, monitor_gate=%d", MAX_DISTANCE_DM, MONITOR_GATE);

    /* 积分/窗口复位 */
    g_win_idx = 0;
    g_win_count = 0;
    memset(g_window_g0, 0, sizeof(g_window_g0));
    memset(g_window_g1, 0, sizeof(g_window_g1));
    g_count_in = 0;
    g_count_out = 0;
    g_current_state = STATE_AWAY;
    g_last_state = STATE_AWAY;
    g_overlap_len = 0;

    while (g_running) {
        unsigned char buf[512 + 140]; /* 140 字节留给前次重叠 */
        int offset = 0;
        int total, i;
        RadarData radar_data;
        double sum_g0, mean_g0, var_sum_g0, variance_g0;
        double sum_g1, mean_g1, var_sum_g1, variance_g1;
        double sum, mean, var_sum, variance; /* 兼容原有逻辑 */
        int condition_in, condition_out;

        /* 把上一次读取的遗留字节拷贝到 buffer 开头 */
        if (g_overlap_len > 0) {
            memcpy(buf, g_overlap, g_overlap_len);
            offset = g_overlap_len;
        }

        int ret = hi_serial_recv(g_radar_service.serial_fd, (char *)(buf + offset), 512);
        if (ret <= 0) {
            continue;
        }
        total = offset + ret;
        LOG_INFO("Read %d bytes, total %d bytes. First byte: %02X", ret, total, buf[0]);
        LOG_INFO("Read %d bytes, total %d bytes. First bytes: %02X %02X %02X %02X", ret, total, buf[0], buf[1], buf[2], buf[3]);

        /* -- 步骤 2: 从字节流中解析一帧雷达数据, 成功返回消耗的字节数, 失败返回 0 */
        int consumed = parse_radar_frame(buf, total, &radar_data);
        if (consumed <= 0) {
            /* 没找到完整帧, 更新重叠逻辑 */
            if (total > 140) {
                memcpy(g_overlap, buf + total - 140, 140);
                g_overlap_len = 140;
            } else {
                memcpy(g_overlap, buf, total);
                g_overlap_len = total;
            }
            continue;
        }

        /* -- 步骤 3: 处理剩余数据作为下一次的重叠 -- */
        g_overlap_len = total - consumed;
        if (g_overlap_len > 140) g_overlap_len = 140;
        if (g_overlap_len > 0) {
            memcpy(g_overlap, buf + consumed, g_overlap_len);
        }

        /* -- 步骤 4: 滑动窗口 — 提取并合并运动和静止能量 -- */
        /* 雷达协议输出16个距离门，每个距离门有运动能量和静止能量 */
        double m0 = radar_data.motion_energy_db[0];
        double s0 = radar_data.static_energy_db[0];
        double e0 = m0 > s0 ? m0 : s0; /* 取两者最大值作为该门的综合能量 */

        double m1 = radar_data.motion_energy_db[1];
        double s1 = radar_data.static_energy_db[1];
        double e1 = m1 > s1 ? m1 : s1;

        g_window[g_win_idx] = m0 > s0 ? m0 : s0; /* 兼容旧逻辑 */
        g_window_g0[g_win_idx] = e0;
        g_window_g1[g_win_idx] = e1;
        g_win_idx = (g_win_idx + 1) % WINDOW_SIZE;
        if (g_win_count < WINDOW_SIZE) g_win_count++;

        /* 窗口未满 10 帧, 继续收集数据 */
        if (g_win_count < WINDOW_SIZE) {
            continue;
        }

        /* -- 步骤 5: 计算均值与方差 -- */
        sum = 0.0; sum_g0 = 0.0; sum_g1 = 0.0;
        for (i = 0; i < WINDOW_SIZE; i++) {
            sum += g_window[i];
            sum_g0 += g_window_g0[i];
            sum_g1 += g_window_g1[i];
        }
        mean = sum / WINDOW_SIZE;
        mean_g0 = sum_g0 / WINDOW_SIZE;
        mean_g1 = sum_g1 / WINDOW_SIZE;

        var_sum = 0.0; var_sum_g0 = 0.0; var_sum_g1 = 0.0;
        for (i = 0; i < WINDOW_SIZE; i++) {
            double d = g_window[i] - mean;
            var_sum += d * d;
            
            double d0 = g_window_g0[i] - mean_g0;
            var_sum_g0 += d0 * d0;
            
            double d1 = g_window_g1[i] - mean_g1;
            var_sum_g1 += d1 * d1;
        }
        variance = var_sum / WINDOW_SIZE;
        variance_g0 = var_sum_g0 / WINDOW_SIZE;
        variance_g1 = var_sum_g1 / WINDOW_SIZE;

        /* -- 步骤 6: 纯能量判定法则 (彻底抛弃不可靠的距离, 解决测不准问题) -- */
        /* 根据组长实测数据深度分析:
           [在椅子上]:   G1_mean = 38.9 ~ 49.0
           [站椅子后]:   G1_mean = 29.9 ~ 36.5
           方差(v)在两种状态下都可能很高(由于人体晃动), 不能作为唯一依据!
        */
        condition_in  = (mean_g1 > ENERGY_TH_IN || mean_g0 > ENERGY_TH_IN);
        condition_out = (mean_g1 < ENERGY_TH_OUT && mean_g0 < ENERGY_TH_OUT);

        /* -- 步骤 7: 漏桶容错有限状态机 -- */
        if (g_current_state == STATE_AWAY) {
            /* 漏桶算法: 满足条件 +1, 不满足仅 -1 (轻度惩罚), 允许入座停顿 1-2 秒 */
            if (condition_in) {
                g_count_in++;
            } else {
                if (g_count_in > 0) g_count_in -= 1;
            }

            if (g_count_in >= TIME_IN_FRAMES) {
                g_current_state = STATE_NORMAL;
                g_count_in = 0;
            }
        } else {
            /* 已入座: 离座判定严苛 */
            if (condition_out) {
                g_count_out++;
            } else {
                /* 改为缓慢递减而不是直接清零, 极大增强抗噪性 (防一帧干扰) */
                if (g_count_out > 0) g_count_out -= 1;
            }

            if (g_count_out >= TIME_OUT_FRAMES) {
                g_current_state = STATE_AWAY;
                g_count_out = 0;
            } else {
                /* 人在座位上, 用方差幅度进行动作级别分类 (结合0门和1门的最大方差) */
                double max_var = variance_g0 > variance_g1 ? variance_g0 : variance_g1;
                if (max_var > VAR_TH_FIDGET)
                    g_current_state = STATE_FIDGET;
                else
                    g_current_state = STATE_NORMAL;
            }
        }

        /* -- 步骤 8: 周期心跳日志 (每 ~1 秒), 便于观测内部状态 -- */
        {
            static int heartbeat = 0;
            if (++heartbeat >= 10) {
                heartbeat = 0;
                LOG_INFO("DIST:%d cm | TGT:%d | G0(m:%.1f s:%.1f v:%.1f) | G1(m:%.1f s:%.1f v:%.1f) | FSM:%s",
                         radar_data.distance_cm, radar_data.has_target,
                         radar_data.motion_energy_db[0], radar_data.static_energy_db[0], variance_g0,
                         radar_data.motion_energy_db[1], radar_data.static_energy_db[1], variance_g1,
                         state_name(g_current_state));
            }
        }

        /* -- 步骤 9: 状态跃迁时输出日志并发送状态 -- */
        if (g_current_state != g_last_state) {
            LOG_INFO("Radar FSM: %s -> %s (mean=%.1f dB, var=%.1f, dist=%d cm)",
                     state_name(g_last_state), state_name(g_current_state),
                     mean, variance, radar_data.distance_cm);
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
