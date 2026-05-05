#include "hi_uart.h"

int hi_serial_open(char* hiserdevice)
{
    int fd;
    fd = open(hiserdevice, O_RDWR|O_NOCTTY|O_NDELAY);
    if (HI_FALSE == fd) {
        perror("HiSerial Can't Open Serial hiserdevice");
        return(HI_FALSE);
    }
    //恢复串口为阻塞状态
    if (fcntl(fd, F_SETFL, 0) < 0) {
        debugpri("fcntl failed!\n");
        return(HI_FALSE);
    }
    else {
        debugpri("fcntl=%d\n",fcntl(fd, F_SETFL,0));
    }
#if 0
    // 测试是否为终端设备
    if (0 == isatty(STDIN_FILENO)) {
        debugpri("standard input is not a terminal device\n");
        return(HI_FALSE);
    }
    else {
        debugpri("isatty success!\n");
    }
#endif
    return fd;
}

void hi_serial_close(int fd)
{
    if(fd > 0) {
        close(fd);
    }
    return;
}

int hi_serial_set(int fd,int speed,int flow_ctrl,int databits,int stopbits,int parity)
{
    int i;
    int status;
    int speed_arr[] ={ B4000000, B3000000, B921600, B576000, B115200, B57600, B38400, B19200, B9600, B4800, B2400, B1200, B300};
    int name_arr[] = { 4000000, 3000000, 921600, 576000, 115200, 57600, 38400,  19200,  9600,  4800,  2400,  1200,  300};

    struct termios options;
    if (tcgetattr(fd,&options) != 0) {
        perror("SetupSerial 1");
        return(HI_FALSE);
    }

    //set buater rate
    for (i= 0;  i < sizeof(speed_arr) / sizeof(int);  i++) {
        if (speed == name_arr[i]) {
            cfsetispeed(&options, speed_arr[i]);
            cfsetospeed(&options, speed_arr[i]);
        }
    }

    //set control model
    options.c_cflag |= CLOCAL;
    options.c_cflag |= CREAD;

    //set flow control
    switch (flow_ctrl) {
        case 0://none
            options.c_cflag &= ~CRTSCTS;
            break;

        case 1://use hard ware
            options.c_cflag |= CRTSCTS;
            break;

        case 2://use sofware
            options.c_cflag |= IXON | IXOFF | IXANY;
            break;
    }

    //select data bit
    options.c_cflag &= ~CSIZE;
    switch (databits) {
        case 5:
            options.c_cflag |= CS5;
            break;

        case 6:
            options.c_cflag |= CS6;
            break;

        case 7:
            options.c_cflag |= CS7;
            break;

        case 8:
            options.c_cflag |= CS8;
            break;

        default:
            fprintf(stderr,"Unsupported data size\n");
            return (HI_FALSE);
    }

    //select parity bit
    switch (parity) {
        case 'n':
        case 'N'://无奇偶校验位
            options.c_cflag &= ~PARENB;
            options.c_iflag &= ~INPCK;
            options.c_iflag &= ~(INLCR | ICRNL); //禁止将输入的CR转换为NL
            options.c_iflag &= ~(IXON); //清bit位 关闭流控字符
            break;

        case 'o':
        case 'O':
            options.c_cflag |= (PARODD | PARENB);
            options.c_iflag |= INPCK;
            break;

        case 'e':
        case 'E':
            options.c_cflag |= PARENB;
            options.c_cflag &= ~PARODD;
            options.c_iflag |= INPCK;
            break;

        case 's':
        case 'S':
            options.c_cflag &= ~PARENB;
            options.c_cflag &= ~CSTOPB;
            break;

        default:
            fprintf(stderr,"Unsupported parity\n");
            return (HI_FALSE);
    }

    // set stopbit
    switch (stopbits) {
        case 1:
            options.c_cflag &= ~CSTOPB; break;

        case 2:
            options.c_cflag |= CSTOPB; break;

        default:
            fprintf(stderr,"Unsupported stop bits\n");
            return (HI_FALSE);
    }

    //set raw data output
    options.c_oflag &= ~OPOST;
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);  //Sunshine
    //options.c_lflag &= ~(ISIG | ICANON);

    //set wait time
    options.c_cc[VTIME] = 1;
    options.c_cc[VMIN] = 1;

    tcflush(fd,TCIFLUSH);

    //set the attribute to HiSerial device
    if (tcsetattr(fd,TCSANOW,&options) != 0) {
        perror("com set error!\n");
        return (HI_FALSE);
    }

    return (HI_TRUE);
}

int hi_serial_init(int fd, int speed,int flow_ctrl,int databits,int stopbits,int parity)
{
    int err;
    //设置串口数据帧格式
    if (hi_serial_set(fd,speed,flow_ctrl,databits,stopbits,parity) == HI_FALSE) {
        return HI_FALSE;
    }
    else {
        return  HI_TRUE;
    }
}

int hi_serial_send(int fd, char *send_buf,int data_len)
{
    int len = 0;
    len = write(fd,send_buf,data_len);
    if (len == data_len ) {
        debugpri("send data is %s\n",send_buf);
        return len;
    }
    else {
        tcflush(fd,TCOFLUSH);
        return HI_FALSE;
    }
}

int hi_serial_recv(int fd, char *rcv_buf,int data_len)
{
    int len,fs_sel;
    fd_set fs_read;
    struct timeval time;

    FD_ZERO(&fs_read);
    FD_SET(fd,&fs_read);

    time.tv_sec = 1;
    time.tv_usec = 0;

    //select fdset
    fs_sel = select(fd+1,&fs_read,NULL,NULL,&time);
    if (fs_sel) {
        len = read(fd,rcv_buf,data_len);
        debugpri("HiSeral Receive Data = %s len = %d fs_sel = %d\n",rcv_buf,len,fs_sel);
        return len;
    }
    else {
        debugpri("Hiserial haven't data receive!");
        return HI_FALSE;
    }
}
