#ifndef SERIAL_UTILS_H
#define SERIAL_UTILS_H

/* 引入 Windows API 核心库，用于调用 CreateFile、ReadFile 等底层系统接口 */
#include <windows.h>

/* * 函数：初始化并打开串口 
 * 参数：port_name (串口名称，如 "\\\\.\\COM3")，baud_rate (波特率，本雷达默认 115200)
 * 返回：成功返回串口句柄 (HANDLE)，失败返回 INVALID_HANDLE_VALUE
 */
HANDLE open_serial(const char* port_name, int baud_rate);

/* * 函数：从串口读取数据 
 * 参数：hComm (已打开的串口句柄)，buffer (接收数据的数组指针)，buf_size (期望读取的最大字节数)
 * 返回：实际读取到的字节数
 */
int read_serial(HANDLE hComm, unsigned char* buffer, int buf_size);

/* * 函数：向串口写入数据 
 * 参数：hComm (已打开的串口句柄)，data (要发送的数据数组指针)，len (要发送的字节数)
 * 返回：实际成功写入的字节数
 */
int write_serial(HANDLE hComm, const unsigned char* data, int len);

/* * 函数：关闭串口，释放系统资源
 * 参数：hComm (要关闭的串口句柄)
 */
void close_serial(HANDLE hComm);

#endif /* SERIAL_UTILS_H */