#ifndef ASR_CONTROLLER_H
#define ASR_CONTROLLER_H

#include <stdint.h>
#include "protocol.h"
#include "config.h"

/**
 * @brief 初始化 ASR 控制器（建立与 fusion_service 的连接）
 * @param config 配置对象指针
 * @return 成功返回 0，失败返回 -1
 */
int asr_controller_init(const Config *config);

/**
 * @brief 关闭 ASR 控制器
 */
void asr_controller_cleanup(void);

/**
 * @brief 处理串口收到的命令
 * @param cmd 串口读取到的 1 字节命令
 */
void asr_process_command(uint8_t cmd);

#endif // ASR_CONTROLLER_H
