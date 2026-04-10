# 慧学引擎 - 开发文档

## 1. 系统架构设计

### 1.1 分层架构

系统采用五层架构设计：

1. **感知层 (Perception Layer)**: 摄像头 + 毫米波雷达
2. **分析层 (Analysis Layer)**: 视觉识别 + 雷达识别
3. **决策层 (Decision Layer)**: 多模态融合 + 状态机
4. **执行层 (Execution Layer)**: 设备控制
5. **展示层 (Presentation Layer)**: Qt可视化界面

### 1.2 服务通信

各服务之间通过TCP/UDP进行通信，使用统一的消息协议。

## 2. 协议定义

### 2.1 消息类型

```c
typedef enum {
    MSG_VISION_STATE = 0x01,    // 视觉状态
    MSG_RADAR_STATE = 0x02,     // 雷达状态
    MSG_FUSION_STATE = 0x03,    // 融合状态
    MSG_DEVICE_CONTROL = 0x04,  // 设备控制
    MSG_HEARTBEAT = 0x05        // 心跳
} MessageType;
```

### 2.2 学习状态

```c
typedef enum {
    STATE_FOCUSED = 0,      // 专注
    STATE_DISTRACTED = 1,   // 走神
    STATE_TIRED = 2,        // 疲劳
    STATE_ABSENT = 3        // 离座
} LearningState;
```

## 3. 模块接口

### 3.1 视觉服务

**输入**: 摄像头视频流

**输出**:
- face_present: 人脸是否存在
- eye_closed_prob: 闭眼概率
- yawn_prob: 哈欠概率
- pitch/yaw: 头部姿态角度
- attention_region: 注意力区域
- face_quality: 人脸质量

### 3.2 雷达服务

**输入**: 毫米波雷达数据

**输出**:
- presence: 在位状态
- motion_level: 微动强度
- distance: 距离
- radar_quality: 雷达质量

### 3.3 融合服务

**输入**: 视觉状态 + 雷达状态

**输出**:
- current_state: 当前学习状态
- state_score: 状态置信度
- intervention_level: 干预级别

### 3.4 设备服务

**输入**: 融合状态

**输出**: 设备控制指令

## 4. 开发流程

### 4.1 第一阶段：最小闭环

1. 实现摄像头接入
2. 实现雷达串口通信
3. 实现简单的闭眼检测
4. 实现台灯控制
5. 打通端到端流程

### 4.2 第二阶段：核心功能

1. 完善人脸检测和跟踪
2. 实现头姿估计
3. 实现注意力区域判断
4. 实现雷达微动估计
5. 完成四状态识别

### 4.3 第三阶段：融合优化

1. 实现多模态融合算法
2. 优化状态平滑逻辑
3. 完善联动策略
4. 开发Qt可视化界面

## 5. 测试方案

### 5.1 单元测试

每个服务独立测试，使用模拟数据。

### 5.2 集成测试

多服务联调，验证通信协议。

### 5.3 系统测试

完整场景测试，验证识别准确率。

## 6. 部署方案

### 6.1 开发环境

在PC上开发和调试，使用模拟数据。

### 6.2 目标环境

部署到SS928开发板，接入真实硬件。

### 6.3 交叉编译

使用aarch64-linux-gnu-gcc进行交叉编译。

## 7. 注意事项

1. 各服务保持独立，避免耦合
2. 使用统一的日志系统
3. 做好异常处理和错误恢复
4. 注意线程安全
5. 优化性能，降低CPU占用
