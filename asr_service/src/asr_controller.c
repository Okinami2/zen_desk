#include "asr_controller.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>

static int g_socket_fd = -1;
static struct sockaddr_in g_fusion_addr;

int asr_controller_init(const Config *config) {
    g_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_socket_fd < 0) {
        LOG_ERROR("Failed to create socket: %s", strerror(errno));
        return -1;
    }

    memset(&g_fusion_addr, 0, sizeof(g_fusion_addr));
    g_fusion_addr.sin_family = AF_INET;
    
    // 默认使用本地地址
    const char *host = (strlen(config->fusion_host) > 0) ? config->fusion_host : "127.0.0.1";
    g_fusion_addr.sin_addr.s_addr = inet_addr(host);
    
    // 如果 config.json 没有配，默认 8003 (fusion_port)
    uint16_t port = config->fusion_port > 0 ? config->fusion_port : 8003;
    g_fusion_addr.sin_port = htons(port);

    if (connect(g_socket_fd, (struct sockaddr *)&g_fusion_addr, sizeof(g_fusion_addr)) < 0) {
        LOG_WARN("Failed to connect to fusion_service at %s:%d, ASR will retry sending later. Error: %s", host, port, strerror(errno));
        close(g_socket_fd);
        g_socket_fd = -1;
        // 不返回错误，因为 fusion_service 可能稍后启动，我们可以在发送时重连
    } else {
        LOG_INFO("Connected to fusion_service at %s:%d", host, port);
    }
    
    return 0;
}

void asr_controller_cleanup(void) {
    if (g_socket_fd >= 0) {
        close(g_socket_fd);
        g_socket_fd = -1;
    }
}

// ============================================================================
// 网络发送助手函数
// ============================================================================
static void send_asr_command_to_fusion(uint8_t cmd) {
    if (g_socket_fd < 0) {
        // 尝试重连
        g_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (g_socket_fd >= 0) {
            if (connect(g_socket_fd, (struct sockaddr *)&g_fusion_addr, sizeof(g_fusion_addr)) < 0) {
                close(g_socket_fd);
                g_socket_fd = -1;
                LOG_ERROR("Fusion service is offline, drop cmd: 0x%02X", cmd);
                return;
            }
            LOG_INFO("Reconnected to fusion_service.");
        } else {
            return;
        }
    }

    Message msg;
    memset(&msg, 0, sizeof(Message));
    
    AsrCommand asr_cmd;
    asr_cmd.command_id = cmd;
    asr_cmd.timestamp = time(NULL);

    msg.type = MSG_ASR_COMMAND;
    msg.length = sizeof(AsrCommand);
    memcpy(msg.data, &asr_cmd, msg.length);

    // 拼装 TCP 报文: [type:4B BE][length:4B BE][payload]
    uint32_t type_be = htonl(msg.type);
    uint32_t len_be = htonl(msg.length);

    // 发送
    if (send(g_socket_fd, &type_be, 4, 0) < 0 ||
        send(g_socket_fd, &len_be, 4, 0) < 0 ||
        send(g_socket_fd, msg.data, msg.length, 0) < 0) {
        LOG_ERROR("Failed to send ASR command 0x%02X to fusion_service: %s", cmd, strerror(errno));
        close(g_socket_fd);
        g_socket_fd = -1; // 下次重连
    } else {
        LOG_INFO("Sent ASR command 0x%02X to fusion_service", cmd);
    }
}

// ============================================================================
// 具体动作函数 (根据用户要求，目前主要实现专注状态控制)
// ============================================================================

static void action_study_start(void) {
    LOG_INFO("[动作] 开始专注 (默认或正计时)");
    send_asr_command_to_fusion(ASR_CMD_STUDY_START);
}

static void action_study_start_25(void) {
    LOG_INFO("[动作] 定时专注 25 分钟");
    send_asr_command_to_fusion(ASR_CMD_STUDY_START_25);
}

static void action_study_start_45(void) {
    LOG_INFO("[动作] 定时专注 45 分钟");
    send_asr_command_to_fusion(ASR_CMD_STUDY_START_45);
}

static void action_study_start_60(void) {
    LOG_INFO("[动作] 定时专注 60 分钟");
    send_asr_command_to_fusion(ASR_CMD_STUDY_START_60);
}

static void action_study_stop(void) {
    LOG_INFO("[动作] 结束专注 (保存数据并停止计时)");
    send_asr_command_to_fusion(ASR_CMD_STUDY_STOP);
}

static void action_study_pause(void) {
    LOG_INFO("[动作] 稍微休息 (暂停计时、离座报警静音、灯光变暗)");
    send_asr_command_to_fusion(ASR_CMD_STUDY_PAUSE);
}

static void action_study_resume(void) {
    LOG_INFO("[动作] 恢复学习 (继续专注计时和灯光)");
    send_asr_command_to_fusion(ASR_CMD_STUDY_RESUME);
}

static void action_wake_up(void) { 
    LOG_INFO("[动作] 唤醒系统"); 
    send_asr_command_to_fusion(ASR_CMD_WAKEUP);
}
static void action_lamp_on(void) { LOG_INFO("[动作] 打开台灯"); }
static void action_lamp_off(void) { LOG_INFO("[动作] 关闭台灯"); }
static void action_lamp_brightness_up(void) { LOG_INFO("[动作] 提高亮度"); }
static void action_lamp_brightness_down(void) { LOG_INFO("[动作] 降低亮度"); }
static void action_lamp_toggle_color_temp(void) { LOG_INFO("[动作] 切换色温"); }
static void action_screen_show_data(void) { LOG_INFO("[动作] 查看学习数据"); }
static void action_screen_show_home(void) { LOG_INFO("[动作] 回到主页"); }

// ============================================================================
// 串口数据分发路由
// ============================================================================
void asr_process_command(uint8_t cmd) {
    switch (cmd) {
        case 0x00: action_wake_up(); break;
        
        case 0x11: action_lamp_on(); break;
        case 0x12: action_lamp_off(); break;
        case 0x13: action_lamp_brightness_up(); break;
        case 0x14: action_lamp_brightness_down(); break;
        case 0x15: action_lamp_toggle_color_temp(); break;
        
        case ASR_CMD_STUDY_START: action_study_start(); break;
        case ASR_CMD_STUDY_START_25: action_study_start_25(); break;
        case ASR_CMD_STUDY_START_45: action_study_start_45(); break;
        case ASR_CMD_STUDY_START_60: action_study_start_60(); break;
        case ASR_CMD_STUDY_STOP: action_study_stop(); break;
        case ASR_CMD_STUDY_PAUSE: action_study_pause(); break;
        case ASR_CMD_STUDY_RESUME: action_study_resume(); break;
        
        case 0x31: action_screen_show_data(); break;
        case 0x32: action_screen_show_home(); break;
        
        default:
            LOG_WARN("收到未知指令: 0x%02X", cmd);
            break;
    }
}
