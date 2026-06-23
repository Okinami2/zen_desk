#include "serial_setup.h"
#include "logger.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <string.h>

static int serial_fd = -1;

int serial_init(void) {
    serial_fd = open(SERIAL_PORT_NAME, O_RDWR | O_NOCTTY | O_NDELAY);
    if (serial_fd == -1) {
        LOG_ERROR("Failed to open serial port %s: %s", SERIAL_PORT_NAME, strerror(errno));
        return -1;
    }

    struct termios options;
    if (tcgetattr(serial_fd, &options) < 0) {
        LOG_ERROR("Failed to get serial attributes");
        close(serial_fd);
        serial_fd = -1;
        return -1;
    }

    // 设置波特率
    // 既然用户提到设置成了 9600 波特率，这里修改为 B9600
    cfsetispeed(&options, B9600);
    cfsetospeed(&options, B9600);

    // 8N1
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    
    // 启用接收器，忽略调制解调器控制线
    options.c_cflag |= CREAD | CLOCAL;

    // 禁用硬件流控
    options.c_cflag &= ~CRTSCTS;

    // 原始模式
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_oflag &= ~OPOST;
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_iflag &= ~(INLCR | ICRNL | IGNCR);

    // 非阻塞读取
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 1;

    if (tcsetattr(serial_fd, TCSANOW, &options) < 0) {
        LOG_ERROR("Failed to set serial attributes");
        close(serial_fd);
        serial_fd = -1;
        return -1;
    }

    fcntl(serial_fd, F_SETFL, 0); // 阻塞读取，结合 VTIME 和 VMIN 控制超时
    LOG_INFO("Serial port %s opened successfully", SERIAL_PORT_NAME);
    return 0;
}

void serial_close(void) {
    if (serial_fd != -1) {
        close(serial_fd);
        serial_fd = -1;
        LOG_INFO("Serial port closed");
    }
}

int serial_read_byte(uint8_t *out_byte) {
    if (serial_fd == -1) {
        return -1;
    }

    ssize_t n = read(serial_fd, out_byte, 1);
    if (n > 0) {
        return 1;
    } else if (n == 0 || (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
        return 0; // 超时或无数据
    }
    
    return -1; // 错误
}
