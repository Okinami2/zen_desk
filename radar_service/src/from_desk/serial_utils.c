#include "serial_utils.h"
#include <stdio.h>

HANDLE open_serial(const char* port_name, int baud_rate) {
    /* 严格遵守 C89 标准：所有局部变量必须在代码块的最上方声明 */
    HANDLE hComm;
    DCB dcbSerialParams;      /* DCB (Device Control Block) 用于配置串口的波特率、数据位等属性 */
    COMMTIMEOUTS timeouts;    /* 用于配置串口读写时的超时时间，防止程序卡死 */

    /* 初始化 DCB 结构体大小，这是 Windows API 的固定要求 */
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    /* 调用系统 API 打开串口。GENERIC_READ | GENERIC_WRITE 表示要求读写权限 */
    hComm = CreateFileA(port_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hComm == INVALID_HANDLE_VALUE) {
        printf("错误: 无法打开系统串口 %s，请检查是否被占用！\n", port_name);
        return INVALID_HANDLE_VALUE;
    }

    /* 获取当前串口的默认配置，如果获取失败则关闭并退出 */
    if (!GetCommState(hComm, &dcbSerialParams)) {
        CloseHandle(hComm);
        return INVALID_HANDLE_VALUE;
    }

    /* 重新配置串口参数：匹配 HLK-LD2420 雷达的通信规范 */
    dcbSerialParams.BaudRate = baud_rate; /* 波特率：115200 */
    dcbSerialParams.ByteSize = 8;         /* 数据位：8位 */
    dcbSerialParams.StopBits = ONESTOPBIT;/* 停止位：1位 */
    dcbSerialParams.Parity   = NOPARITY;  /* 校验位：无校验 */

    /* 将修改后的配置写回给串口，使配置生效 */
    if (!SetCommState(hComm, &dcbSerialParams)) {
        CloseHandle(hComm);
        return INVALID_HANDLE_VALUE;
    }

    /* 配置读写超时机制。设定较短的超时时间，使得 read 函数即使没读够数据也能快速返回，不阻塞主程序的死循环 */
    timeouts.ReadIntervalTimeout         = 50; /* 两个字节到达之间的最大时间间隔(毫秒) */
    timeouts.ReadTotalTimeoutConstant    = 50; /* 读操作的总额外等待时间 */
    timeouts.ReadTotalTimeoutMultiplier  = 10; /* 每个请求读取的字节乘以该值 */
    timeouts.WriteTotalTimeoutConstant   = 50; /* 写操作同理 */
    timeouts.WriteTotalTimeoutMultiplier = 10;

    SetCommTimeouts(hComm, &timeouts); /* 应用超时配置 */
    
    return hComm; /* 返回配置完毕的串口句柄，供上层调用 */
}

int read_serial(HANDLE hComm, unsigned char* buffer, int buf_size) {
    DWORD bytesRead = 0; /* 用于系统 API 记录实际读到的字节数 */
    /* 从硬件缓冲区抓取数据存入 buffer */
    ReadFile(hComm, buffer, buf_size, &bytesRead, NULL);
    return (int)bytesRead;
}

int write_serial(HANDLE hComm, const unsigned char* data, int len) {
    DWORD bytesWritten = 0; /* 用于系统 API 记录实际发出的字节数 */
    /* 将 data 数组的内容推送到硬件发送 */
    WriteFile(hComm, data, len, &bytesWritten, NULL);
    return (int)bytesWritten;
}

void close_serial(HANDLE hComm) {
    /* 安全检查：只有当句柄有效时才执行关闭操作，防止程序崩溃 */
    if (hComm != INVALID_HANDLE_VALUE) {
        CloseHandle(hComm);
    }
}