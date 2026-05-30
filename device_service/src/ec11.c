#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uinput.h>

#include "ec11.h"
#include "logger.h"

// =========================================================
// ⚙️ 硬件配置区：拿到硬件原理图后，只需修改这里的引脚号即可
// =========================================================
#define GPIO_EC11_A    "71"  // [修改此值] 旋钮 A 相对应的 GPIO 编号
#define GPIO_EC11_B    "72"  // [修改此值] 旋钮 B 相对应的 GPIO 编号
#define GPIO_EC11_SW   "70"  // [修改此值] 旋钮 按键(SW) 对应的 GPIO 编号

#define LONG_PRESS_MS  600    // 长按判定阈值 (毫秒)
#define POLL_INTERVAL_US 1000 // 轮询间隔 (1毫秒, 用于纯软件消抖)

// 虚拟键盘文件描述符
static int uinput_fd = -1;
static int g_uinput_created = 0;
static pthread_t g_ec11_thread;
static int g_ec11_thread_started = 0;
static volatile int g_ec11_running = 0;
static int g_fd_a = -1;
static int g_fd_b = -1;
static int g_fd_sw = -1;

// =========================================================
// 辅助函数：GPIO 操作 (基于 sysfs)
// =========================================================
static int export_gpio(const char* gpio) {
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) return -1;
    if (write(fd, gpio, strlen(gpio)) < 0) {
        if (errno != EBUSY) {
            close(fd);
            return -1;
        }
    }
    close(fd);
    usleep(50000); // 等待系统创建节点
    return 0;
}

static int set_gpio_direction(const char* gpio, const char* dir) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%s/direction", gpio);
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    write(fd, dir, strlen(dir));
    close(fd);
    return 0;
}

static int open_gpio_value(const char* gpio) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%s/value", gpio);
    return open(path, O_RDONLY);
}

static int read_gpio_level(int fd) {
    char val;
    lseek(fd, 0, SEEK_SET);
    if (read(fd, &val, 1) < 0) return 1; // 默认返回高电平
    return val == '1' ? 1 : 0;
}

// =========================================================
// 辅助函数：虚拟键盘事件注入
// =========================================================
static void emit_key(int fd, int keycode, int val) {
    struct input_event ie;
    ie.type = EV_KEY;
    ie.code = keycode;
    ie.value = val; // 1: 按下, 0: 释放
    ie.time.tv_sec = 0; 
    ie.time.tv_usec = 0;
    write(fd, &ie, sizeof(ie));

    // 发送同步事件，告诉内核处理这次输入
    ie.type = EV_SYN;
    ie.code = SYN_REPORT;
    ie.value = 0;
    write(fd, &ie, sizeof(ie));
}

// 模拟完整的敲击 (按下然后立刻释放)
static void click_key(int keycode) {
    if (uinput_fd < 0) {
        return;
    }
    emit_key(uinput_fd, keycode, 1);
    usleep(10000); // 10ms 按压保持，增加系统识别率
    emit_key(uinput_fd, keycode, 0);
    printf("[EC11] 发送按键事件 -> Code: %d\n", keycode);
}

// 获取单调时间 (毫秒)
static long long get_current_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// =========================================================
// 核心工作线程：高频轮询读取与状态机
// =========================================================
static void* ec11_poll_thread(void* arg) {
    (void)arg;

    if (g_fd_a < 0 || g_fd_b < 0 || g_fd_sw < 0) {
        LOG_ERROR("EC11 GPIO fd invalid, aborting thread");
        return NULL;
    }

    // --- 旋钮状态机变量 ---
    int last_a = 1, last_b = 1;
    int rotation_step = 0; // 0:静止(11), 1:A先拉低(逆时针), 2:B先拉低(顺时针)

    // --- 按键状态机变量 ---
    int last_sw = 1; // 假设默认上拉，未按为1，按下为0
    long long press_time = 0;
    int sw_stable_cnt = 0; // 用于按键软件消抖

    LOG_INFO("[EC11] 引脚 (A:%s, B:%s, SW:%s) 监听中...", GPIO_EC11_A, GPIO_EC11_B, GPIO_EC11_SW);

    while (g_ec11_running) {
        int a = read_gpio_level(g_fd_a);
        int b = read_gpio_level(g_fd_b);
        int sw = read_gpio_level(g_fd_sw);

        // ----------------------------------------------------
        // 1. 旋钮正交解码 (绝对状态机，无视轻微抖动)
        // 标准 EC11 的两个引脚静止时都在定位点 (高电平 11)
        // ----------------------------------------------------
        if (a == 1 && b == 1) {
            // 回到定位点，结算上一次的转动
            if (rotation_step == 1) {
                click_key(KEY_LEFT);  // A相先拉低，判定为逆时针/向左
            } else if (rotation_step == 2) {
                click_key(KEY_RIGHT); // B相先拉低，判定为顺时针/向右
            }
            rotation_step = 0; // 重置状态
        } 
        else if (rotation_step == 0) {
            // 刚离开定位点，记录谁先被拉低
            if (a == 0 && last_a == 1 && b == 1) {
                rotation_step = 1; // 标记A领先
            } else if (b == 0 && last_b == 1 && a == 1) {
                rotation_step = 2; // 标记B领先
            }
        }
        last_a = a;
        last_b = b;

        // ----------------------------------------------------
        // 2. 按键 SW 处理 (含软件消抖与长短按判定)
        // ----------------------------------------------------
        if (sw != last_sw) {
            sw_stable_cnt++;
            // 连续 10 次 (10ms) 电平不翻转，才认为是有效电平变化，彻底过滤物理抖动
            if (sw_stable_cnt > 10) { 
                if (sw == 0) {
                    // 按下事件确认
                    press_time = get_current_ms();
                } else {
                    // 抬起事件确认
                    long long duration = get_current_ms() - press_time;
                    if (press_time > 0 && duration > 50) { // 剔除超短毛刺
                        if (duration >= LONG_PRESS_MS) {
                            LOG_INFO("[EC11] 长按触发 (%lld ms)", duration);
                            click_key(KEY_ESC);
                        } else {
                            LOG_INFO("[EC11] 短按触发 (%lld ms)", duration);
                            click_key(KEY_SPACE);
                        }
                    }
                    press_time = 0;
                }
                last_sw = sw;
                sw_stable_cnt = 0;
            }
        } else {
            sw_stable_cnt = 0;
        }

        // 每次循环挂起 1 毫秒
        usleep(POLL_INTERVAL_US);
    }
    return NULL;
}

// =========================================================
// 生命周期：初始化 / 启动 / 停止 / 清理
// =========================================================
int ec11_init(void) {
    LOG_INFO("[EC11] 正在初始化虚拟键盘...");

    // 1. 打开 uinput 设备 (必须有 root 权限)
    uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_fd < 0) {
        LOG_ERROR("无法打开 /dev/uinput。请检查内核是否加载了 uinput 模块，并确保使用 sudo 运行。");
        return -1;
    }

    // 2. 配置虚拟键盘能够发出的按键类型
    if (ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(uinput_fd, UI_SET_KEYBIT, KEY_LEFT) < 0 ||
        ioctl(uinput_fd, UI_SET_KEYBIT, KEY_RIGHT) < 0 ||
        ioctl(uinput_fd, UI_SET_KEYBIT, KEY_SPACE) < 0 ||
        ioctl(uinput_fd, UI_SET_KEYBIT, KEY_ESC) < 0) {
        LOG_ERROR("[EC11] 配置 uinput 事件类型失败");
        close(uinput_fd);
        uinput_fd = -1;
        return -1;
    }

    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor  = 0x1234; 
    usetup.id.product = 0x5678;
    strcpy(usetup.name, "ZenDesk_EC11_Knob");

    if (ioctl(uinput_fd, UI_DEV_SETUP, &usetup) < 0 ||
        ioctl(uinput_fd, UI_DEV_CREATE) < 0) {
        LOG_ERROR("[EC11] 创建 uinput 设备失败");
        close(uinput_fd);
        uinput_fd = -1;
        return -1;
    }
    g_uinput_created = 1;
    usleep(100000);

    LOG_INFO("[EC11] 虚拟键盘 ZenDesk_EC11_Knob 挂载成功！");

    // 3. 导出并配置 GPIO
    if (export_gpio(GPIO_EC11_A) != 0 ||
        export_gpio(GPIO_EC11_B) != 0 ||
        export_gpio(GPIO_EC11_SW) != 0) {
        LOG_ERROR("[EC11] 导出 GPIO 失败");
        return -1;
    }
    if (set_gpio_direction(GPIO_EC11_A, "in") != 0 ||
        set_gpio_direction(GPIO_EC11_B, "in") != 0 ||
        set_gpio_direction(GPIO_EC11_SW, "in") != 0) {
        LOG_ERROR("[EC11] 设置 GPIO 方向失败");
        return -1;
    }

    return 0;
}

int ec11_start(void) {
    if (g_ec11_thread_started) {
        return 0;
    }

    g_fd_a = open_gpio_value(GPIO_EC11_A);
    g_fd_b = open_gpio_value(GPIO_EC11_B);
    g_fd_sw = open_gpio_value(GPIO_EC11_SW);
    if (g_fd_a < 0 || g_fd_b < 0 || g_fd_sw < 0) {
        LOG_ERROR("[EC11] 无法打开 GPIO 文件。请确认引脚号是否正确，并使用 root 权限运行！");
        if (g_fd_a >= 0) close(g_fd_a);
        if (g_fd_b >= 0) close(g_fd_b);
        if (g_fd_sw >= 0) close(g_fd_sw);
        g_fd_a = g_fd_b = g_fd_sw = -1;
        return -1;
    }

    g_ec11_running = 1;
    if (pthread_create(&g_ec11_thread, NULL, ec11_poll_thread, NULL) != 0) {
        LOG_ERROR("[EC11] 创建监听线程失败");
        g_ec11_running = 0;
        close(g_fd_a);
        close(g_fd_b);
        close(g_fd_sw);
        g_fd_a = g_fd_b = g_fd_sw = -1;
        return -1;
    }

    g_ec11_thread_started = 1;
    return 0;
}

void ec11_stop(void) {
    if (g_ec11_thread_started) {
        g_ec11_running = 0;
        pthread_join(g_ec11_thread, NULL);
        g_ec11_thread_started = 0;
    }

    if (g_fd_a >= 0) {
        close(g_fd_a);
        g_fd_a = -1;
    }
    if (g_fd_b >= 0) {
        close(g_fd_b);
        g_fd_b = -1;
    }
    if (g_fd_sw >= 0) {
        close(g_fd_sw);
        g_fd_sw = -1;
    }
}

void ec11_cleanup(void) {
    ec11_stop();

    if (g_uinput_created) {
        ioctl(uinput_fd, UI_DEV_DESTROY);
        g_uinput_created = 0;
    }
    if (uinput_fd >= 0) {
        close(uinput_fd);
        uinput_fd = -1;
    }
}