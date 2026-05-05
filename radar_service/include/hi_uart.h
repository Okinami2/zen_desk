#ifndef __HI_UART_H__
#define __HI_UART_H__

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>   /* 文件控制定义*/
#include <termios.h> /* PPSIX 终端控制定义*/
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define HI_FALSE  -1
#define HI_TRUE    0

#ifdef debugprintf
    #define debugpri(mesg, args...) fprintf(stderr, "[HI Serial print:%s:%d:]  "mesg"   \n", __FILE__, __LINE__, ##args)
#else
    #define debugpri(mesg, args...)
#endif

int hi_serial_open(char* hiserdevice);
void hi_serial_close(int fd);
int hi_serial_set(int fd,int speed,int flow_ctrl,int databits,int stopbits,int parity);
int hi_serial_init(int fd, int speed,int flow_ctrl,int databits,int stopbits,int parity);
int hi_serial_send(int fd, char *send_buf,int data_len);
int hi_serial_recv(int fd, char *rcv_buf,int data_len);

#endif    //    __HI_UART_H__