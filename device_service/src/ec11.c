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

#include "board_pins.h"
#include "ec11.h"
#include "logger.h"

// =========================================================
// ⚙️ 硬件配置区：拿到硬件原理图后，只需修改这里的引脚号即可
// =========================================================
#define GPIO_EC11_A    "71"  // [修改此值] 旋钮 A 相对应的 GPIO 编号
#define GPIO_EC11_B    "72"  // [修改此值] 旋钮 B 相对应的 GPIO 编号
#undef GPIO_EC11_A
#undef GPIO_EC11_B
#undef GPIO_EC11_SW

#define GPIO_EC11_A BOARD_GPIO_EC11_A
#define GPIO_EC11_B BOARD_GPIO_EC11_B

#define GPIO_EC11_SW   "70"  // [修改此值] 旋钮 按键(SW) 对应的 GPIO 编号

#undef GPIO_EC11_SW
#define GPIO_EC11_SW BOARD_GPIO_EC11_SW

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
    // 0:静止(11), 1:A先拉低, 2:B先拉低, 3:A先拉低的谷底, 4:B先拉低的谷底
    int rotation_step = 0; 
    int b_went_low_during_press = 0; // 记录按键期间 B 相是否拉低过

    // --- 按键状态机变量 ---
    int last_sw = 1; // 假设默认上拉，未按为1，按下为0
    long long press_time = 0;
    int sw_stable_cnt = 0; // 用于按键软件消抖

    LOG_INFO("[EC11] 引脚 (A:%s, B:%s, SW:%s) 监听中...", GPIO_EC11_A, GPIO_EC11_B, GPIO_EC11_SW);

    while (g_ec11_running) {
        int raw_a = read_gpio_level(g_fd_a);
        int raw_b = read_gpio_level(g_fd_b);
        int raw_sw = read_gpio_level(g_fd_sw);

        // --- 软件消抖滤波 (应对雷达高频UART串扰) ---
        static int a = 1, b = 1, sw = 1;
        static int hist_a = 1, hist_b = 1, hist_sw = 1;
        static int cnt_a = 0, cnt_b = 0, cnt_sw = 0;
        const int DEBOUNCE = 3; // 3ms 消抖

        if (raw_a == hist_a) { if (++cnt_a >= DEBOUNCE) a = raw_a; } else { cnt_a = 0; hist_a = raw_a; }
        if (raw_b == hist_b) { if (++cnt_b >= DEBOUNCE) b = raw_b; } else { cnt_b = 0; hist_b = raw_b; }
        if (raw_sw == hist_sw) { if (++cnt_sw >= DEBOUNCE) sw = raw_sw; } else { cnt_sw = 0; hist_sw = raw_sw; }

        // --- 原始电平抓取与打印 (调试去噪用) ---
        static int last_raw_a = -1, last_raw_b = -1, last_raw_sw = -1;
        if (a != last_raw_a || b != last_raw_b || sw != last_raw_sw) {
            printf("[EC11 RAW] A:%d B:%d SW:%d | ms:%lld\n", a, b, sw, get_current_ms());
            fflush(stdout);
            last_raw_a = a;
            last_raw_b = b;
            last_raw_sw = sw;
        }

        // ----------------------------------------------------
        // 1. 旋钮正交解码 (防抖动深谷状态机)
        // 只有完整经历过 (1,1)->(A低或B低)->(0,0)->(1,1) 才算一次有效转动
        // ----------------------------------------------------
        if (a == 1 && b == 1) {
            if (rotation_step == 3) {
                click_key(KEY_RIGHT);  // A先拉低，根据日志物理表现为顺时针
            } else if (rotation_step == 4) {
                click_key(KEY_LEFT);   // B先拉低，根据日志物理表现为逆时针
            }
            rotation_step = 0; 
        } 
        else if (rotation_step == 0) {
            if (a == 0 && b == 1) rotation_step = 1;
            else if (b == 0 && a == 1) rotation_step = 2;
        }
        else if (rotation_step == 1 && a == 0 && b == 0) {
            rotation_step = 3;
        }
        else if (rotation_step == 2 && a == 0 && b == 0) {
            rotation_step = 4;
        }
        
        last_a = a;
        last_b = b;

        // ----------------------------------------------------
        // 2. 串扰极度智能剥离与 SW 按键处理
        // 现象：日志证明 A 相与 SW 引脚在硬件上存在严重短接/串扰！按下SW必导致A=0；A=0必导致SW=0。
        // 破局：既然 A 和 SW 绑死了，我们就看 B！
        // 如果在按键期间 B 一直是 1，说明是真实的按压。如果 B 变成了 0，说明是在转动！
        // ----------------------------------------------------
        if (press_time > 0 && b == 0) {
            b_went_low_during_press = 1;
        }

        if (sw != last_sw) {
            sw_stable_cnt++;
            if (sw_stable_cnt > 10) { 
                if (sw == 0) {
                    // 按下事件确认
                    press_time = get_current_ms();
                    // 初始化 B 相监控标志
                    b_went_low_during_press = (b == 0) ? 1 : 0; 
                } else {
                    // 抬起事件确认
                    long long duration = get_current_ms() - press_time;
                    if (press_time > 0 && duration > 50) { 
                        // 如果按键期间 B 变过低电平，说明是转动带来的 A/SW 联动串扰！
                        if (b_went_low_during_press) {
                            LOG_INFO("[EC11] 按键被屏蔽 (检出旋转串扰，B相曾被拉低)");
                        } else {
                            if (duration >= LONG_PRESS_MS) {
                                LOG_INFO("[EC11] 长按触发 (%lld ms)", duration);
                                click_key(KEY_ESC);
                            } else {
                                LOG_INFO("[EC11] 短按触发 (%lld ms)", duration);
                                click_key(KEY_SPACE);
                            }
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