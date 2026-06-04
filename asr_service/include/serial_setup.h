#ifndef SERIAL_SETUP_H
#define SERIAL_SETUP_H

#include <stdint.h>

// 串口配置宏定义
// 预留给未来实际硬件开发板上的串口节点 (如 /dev/ttyAMA1 等，请根据实际情况取消注释)
// #define SERIAL_PORT_NAME "/dev/ttyAMA1"

// 当前测试环境: PC虚拟机外接 QinHeng Electronics USB Serial 设备
#define SERIAL_PORT_NAME "/dev/ttyUSB0"
#define SERIAL_BAUD_RATE 9600

/**
 * @brief 初始化并打开串口
 * @return 成功返回 0，失败返回 -1
 */
int serial_init(void);

/**
 * @brief 关闭串口
 */
void serial_close(void);

/**
 * @brief 从串口读取一个字节
 * @param out_byte 读取到的字节将保存在此指针指向的内存中
 * @return 成功读取返回 1，未读到数据返回 0，出错返回 -1
 */
int serial_read_byte(uint8_t *out_byte);

#endif // SERIAL_SETUP_H
