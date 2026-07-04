#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

// 消息类型定义
typedef enum {
    MSG_VISION_STATE = 0x01,
    MSG_RADAR_STATE = 0x02,
    MSG_FUSION_STATE = 0x03,
    MSG_DEVICE_CONTROL = 0x04,
    MSG_HEARTBEAT = 0x05,
    MSG_ASR_COMMAND = 0x06
} MessageType;

// 学习状态定义
typedef enum {
    STATE_SEATED_IDLE = 0,   // 就座未学习
    STATE_FOCUSED = 1,       // 专注
    STATE_DISTRACTED = 2,    // 走神
    STATE_TIRED = 3,         // 疲劳
    STATE_ABSENT = 4         // 离座
} LearningState;

typedef enum {
    VISION_ATTENTION_UNKNOWN = 0,
    VISION_ATTENTION_FRONT = 1,
    VISION_ATTENTION_LEFT = 2,
    VISION_ATTENTION_RIGHT = 3,
    VISION_ATTENTION_UP = 4,
    VISION_ATTENTION_DOWN = 5,
    VISION_ATTENTION_EYES_CLOSED = 6,
    VISION_ATTENTION_NO_FACE = 7,
    VISION_ATTENTION_ERROR = 8
} VisionAttentionRegion;

// 视觉状态数据
typedef struct {
    uint8_t face_present;       // 人脸是否存在
    float eye_closed_prob;      // 闭眼概率
    float yawn_prob;            // 哈欠概率
    float pitch;                // 俯仰角
    float yaw;                  // 偏航角
    uint8_t attention_region;   // 注意力区域
    float face_quality;         // 人脸质量
    float face_x1;              // 人脸框左上角 x
    float face_y1;              // 人脸框左上角 y
    float face_x2;              // 人脸框右下角 x
    float face_y2;              // 人脸框右下角 y
    uint64_t timestamp;         // 时间戳
    uint32_t blink_count;        // 眨眼计数
    uint32_t yawn_count;         // 哈欠计数
} VisionState;

// 雷达状态数据
typedef struct {
    uint8_t presence;           // 在位状态
    float motion_level;         // 微动强度
    float distance;             // 距离
    float radar_quality;        // 雷达质量
    uint64_t timestamp;         // 时间戳
} RadarState;

// 融合状态数据
typedef struct {
    LearningState current_state;    // 当前状态
    float state_score;              // 状态置信度
    uint8_t intervention_level;     // 干预级别
    uint16_t duration_minutes;      // 专注时长 (如果是定时专注，则下发时长)
    uint64_t timestamp;             // 时间戳
} FusionState;

// 设备控制指令
typedef struct {
    uint8_t device_id;          // 设备ID
    uint8_t action;             // 动作类型
    uint8_t brightness;         // 亮度
    uint16_t color_temp;        // 色温
    uint64_t timestamp;         // 时间戳
} DeviceControl;

// ASR 语音控制指令
typedef enum {
    ASR_CMD_WAKEUP             = 0x00,
    ASR_CMD_LAMP_ON            = 0x11,
    ASR_CMD_LAMP_OFF           = 0x12,
    ASR_CMD_LAMP_BRIGHT_UP     = 0x13,
    ASR_CMD_LAMP_BRIGHT_DOWN   = 0x14,
    ASR_CMD_STUDY_START        = 0x21,
    ASR_CMD_STUDY_STOP         = 0x22,
    ASR_CMD_STUDY_PAUSE        = 0x23,
    ASR_CMD_STUDY_RESUME       = 0x24,
    ASR_CMD_STUDY_START_25     = 0x25,
    ASR_CMD_STUDY_START_45     = 0x26,
    ASR_CMD_STUDY_START_60     = 0x27,
    ASR_CMD_STUDY_DISTRACTED   = 0x28,
    ASR_CMD_STUDY_FOCUSED      = 0x29,
    ASR_CMD_SCREEN_DATA        = 0x31,
    ASR_CMD_SCREEN_HOME        = 0x32
} AsrCommandType;

typedef struct {
    uint8_t command_id;         // 对应的 Hex 码，如 0x21
    uint64_t timestamp;         // 时间戳
} AsrCommand;

// 通用消息结构
typedef struct {
    MessageType type;
    uint32_t length;
    uint8_t data[256];
} Message;

// UI 事件指令 (主要用于通过 UDP 发送给 Qt 客户端)
typedef enum {
    UI_EVENT_WAKEUP_ASR = 0x01,      // 显示麦克风图标
    UI_EVENT_ASR_DONE = 0x02,        // 隐藏麦克风图标
    UI_EVENT_STATE_UPDATE = 0x03,    // 状态更新(携带FusionState)
    UI_EVENT_SHOW_DATA = 0x04,       // 显示数据页
    UI_EVENT_SHOW_HOME = 0x05,       // 显示主页
    // 新增：精确动作类事件
    UI_EVENT_ACTION_STUDY_START_25 = 0x11,
    UI_EVENT_ACTION_STUDY_START_45 = 0x12,
    UI_EVENT_ACTION_STUDY_START_60 = 0x13,
    UI_EVENT_ACTION_STUDY_START_FREE = 0x14,
    UI_EVENT_ACTION_STUDY_PAUSE = 0x15,
    UI_EVENT_ACTION_STUDY_RESUME = 0x16,
    UI_EVENT_ACTION_STUDY_STOP = 0x17
} UiEventType;

typedef struct {
    UiEventType event_type;
    FusionState state;               // 只有在 STATE_UPDATE 时有意义
} UiEventMessage;

#endif // PROTOCOL_H
