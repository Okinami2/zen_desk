#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

// 消息类型定义
typedef enum {
    MSG_VISION_STATE = 0x01,
    MSG_RADAR_STATE = 0x02,
    MSG_FUSION_STATE = 0x03,
    MSG_DEVICE_CONTROL = 0x04,
    MSG_HEARTBEAT = 0x05
} MessageType;

// 学习状态定义
typedef enum {
    STATE_SEATED_IDLE = 0,   // 就座未学习
    STATE_FOCUSED = 1,       // 专注
    STATE_DISTRACTED = 2,    // 走神
    STATE_TIRED = 3,         // 疲劳
    STATE_ABSENT = 4         // 离座
} LearningState;

// 视觉状态数据
typedef struct {
    uint8_t face_present;       // 人脸是否存在
    float eye_closed_prob;      // 闭眼概率
    float yawn_prob;            // 哈欠概率
    float pitch;                // 俯仰角
    float yaw;                  // 偏航角
    uint8_t attention_region;   // 注意力区域
    float face_quality;         // 人脸质量
    uint64_t timestamp;         // 时间戳
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

// 通用消息结构
typedef struct {
    MessageType type;
    uint32_t length;
    uint8_t data[256];
} Message;

#endif // PROTOCOL_H
