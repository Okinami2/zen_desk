#include "board_pins.h"
#include "device_service.h"
#include "ec11.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#define PWM_CHIP_PATH "/sys/class/pwm/pwmchip0/"
#define PWM_PERIOD 1000000
#define PWM_WARM_CHANNEL BOARD_PWM_LAMP_WARM_CHANNEL
#define PWM_COLD_CHANNEL BOARD_PWM_LAMP_COLD_CHANNEL
#define LAMP_TICK_US 30000
#define BREATH_PERIOD_MS 10000

typedef enum {
    LAMP_MODE_STATIC = 0,
    LAMP_MODE_BREATH = 1
} LampMode;

typedef struct {
    int brightness;
    float color_ratio;
    LampMode mode;
    int transition_ms;
    int breath_amplitude;
} LampScene;

static DeviceService g_device_service;
static pthread_t g_lamp_thread;
static int g_lamp_thread_started = 0;
static pthread_mutex_t g_lamp_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_pwm_initialized = 0;

static LampScene g_target_scene = {0, 0.5f, LAMP_MODE_STATIC, 1500, 0};
static int g_current_brightness = 0;
static float g_current_color_ratio = 0.5f;
static unsigned int g_breath_phase_ms = 0;

static int pwm_write(int channel, const char *node, int value) {
    char path[128];
    int fd;

    snprintf(path, sizeof(path), PWM_CHIP_PATH "pwm%d/%s", channel, node);
    fd = open(path, O_WRONLY);
    if (fd < 0) {
        int export_fd = open(PWM_CHIP_PATH "export", O_WRONLY);
        if (export_fd >= 0) {
            dprintf(export_fd, "%d", channel);
            close(export_fd);
            usleep(100000);
            fd = open(path, O_WRONLY);
        }
    }

    if (fd < 0) {
        LOG_ERROR("Failed to open PWM node: %s", path);
        return -1;
    }

    dprintf(fd, "%d", value);
    close(fd);
    return 0;
}

static int pwm_enable(int channel, int enable) {
    char path[128];
    int fd;

    snprintf(path, sizeof(path), PWM_CHIP_PATH "pwm%d/enable", channel);
    fd = open(path, O_WRONLY);
    if (fd < 0) {
        LOG_ERROR("Failed to open PWM enable node: %s", path);
        return -1;
    }

    if (write(fd, enable ? "1" : "0", 1) != 1) {
        LOG_ERROR("Failed to write PWM enable node: %s", path);
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

static int lamp_apply_pwm(int brightness, float color_ratio) {
    int cold_value;
    int warm_value;
    int cold_duty;
    int warm_duty;

    if (brightness < 0) {
        brightness = 0;
    }
    if (brightness > PWM_PERIOD) {
        brightness = PWM_PERIOD;
    }
    if (color_ratio < 0.0f) {
        color_ratio = 0.0f;
    }
    if (color_ratio > 1.0f) {
        color_ratio = 1.0f;
    }

    cold_value = (int)(brightness * color_ratio);
    warm_value = (int)(brightness * (1.0f - color_ratio));
    cold_duty = PWM_PERIOD - cold_value;
    warm_duty = PWM_PERIOD - warm_value;

    if (pwm_write(PWM_COLD_CHANNEL, "duty_cycle", cold_duty) != 0) {
        return -1;
    }
    if (pwm_write(PWM_WARM_CHANNEL, "duty_cycle", warm_duty) != 0) {
        return -1;
    }

    return 0;
}

static int approach_int(int current, int target, int step) {
    if (current < target) {
        current += step;
        if (current > target) {
            current = target;
        }
    } else if (current > target) {
        current -= step;
        if (current < target) {
            current = target;
        }
    }
    return current;
}

static float approach_float(float current, float target, float step) {
    if (current < target) {
        current += step;
        if (current > target) {
            current = target;
        }
    } else if (current > target) {
        current -= step;
        if (current < target) {
            current = target;
        }
    }
    return current;
}

static int calculate_breath_offset(int amplitude, unsigned int phase_ms) {
    unsigned int cycle_pos;
    int quarter;

    if (amplitude <= 0) {
        return 0;
    }

    cycle_pos = phase_ms % BREATH_PERIOD_MS;
    quarter = BREATH_PERIOD_MS / 4;

    if (cycle_pos < (unsigned int)quarter) {
        return (amplitude * (int)cycle_pos) / quarter;
    }
    if (cycle_pos < (unsigned int)(quarter * 3)) {
        return amplitude - (amplitude * ((int)cycle_pos - quarter)) / quarter;
    }
    return -amplitude + (amplitude * ((int)cycle_pos - quarter * 3)) / quarter;
}

static void set_lamp_scene(int brightness_percent, float color_ratio,
                           LampMode mode, int transition_ms, int breath_percent) {
    pthread_mutex_lock(&g_lamp_mutex);
    g_target_scene.brightness = (brightness_percent * PWM_PERIOD) / 100;
    g_target_scene.color_ratio = color_ratio;
    g_target_scene.mode = mode;
    g_target_scene.transition_ms = transition_ms;
    g_target_scene.breath_amplitude = (breath_percent * PWM_PERIOD) / 100;
    if (mode != LAMP_MODE_BREATH) {
        g_breath_phase_ms = 0;
    }
    pthread_mutex_unlock(&g_lamp_mutex);
}

static void* lamp_worker_thread(void *arg) {
    (void)arg;

    while (g_device_service.running) {
        LampScene scene;
        int tick_ms = LAMP_TICK_US / 1000;
        int step;
        float color_step;
        int output_brightness;

        pthread_mutex_lock(&g_lamp_mutex);
        scene = g_target_scene;
        pthread_mutex_unlock(&g_lamp_mutex);

        if (scene.transition_ms <= 0) {
            scene.transition_ms = tick_ms;
        }

        step = (PWM_PERIOD * tick_ms) / scene.transition_ms;
        if (step < 5000) {
            step = 5000;
        }
        color_step = (float)tick_ms / (float)scene.transition_ms;
        if (color_step < 0.01f) {
            color_step = 0.01f;
        }

        g_current_brightness = approach_int(g_current_brightness, scene.brightness, step);
        g_current_color_ratio = approach_float(g_current_color_ratio, scene.color_ratio, color_step);

        output_brightness = g_current_brightness;
        if (scene.mode == LAMP_MODE_BREATH) {
            output_brightness += calculate_breath_offset(scene.breath_amplitude, g_breath_phase_ms);
            g_breath_phase_ms = (g_breath_phase_ms + tick_ms) % BREATH_PERIOD_MS;
            if (output_brightness < 0) {
                output_brightness = 0;
            }
            if (output_brightness > PWM_PERIOD) {
                output_brightness = PWM_PERIOD;
            }
        }

        lamp_apply_pwm(output_brightness, g_current_color_ratio);
        usleep(LAMP_TICK_US);
    }

    return NULL;
}

int device_service_init(const Config *config) {
    int channels[] = {PWM_WARM_CHANNEL, PWM_COLD_CHANNEL};
    int i;

    LOG_INFO("Initializing device service...");

    if (board_pins_apply() != 0) {
        LOG_ERROR("Failed to apply unified board pin mapping");
        return -1;
    }

    memset(&g_device_service, 0, sizeof(DeviceService));
    g_device_service.config = *config;
    g_device_service.running = 0;
    g_device_service.current_state = STATE_ABSENT;

    for (i = 0; i < 2; ++i) {
        if (pwm_write(channels[i], "period", PWM_PERIOD) != 0) {
            return -1;
        }
        if (pwm_write(channels[i], "duty_cycle", PWM_PERIOD) != 0) {
            return -1;
        }
        if (pwm_enable(channels[i], 1) != 0) {
            return -1;
        }
    }

    g_pwm_initialized = 1;
    g_current_brightness = 0;
    g_current_color_ratio = 0.45f;
    set_lamp_scene(0, 0.45f, LAMP_MODE_STATIC, 1500, 0);

    if (ec11_init() != 0) {
        LOG_ERROR("EC11 init failed");
        return -1;
    }

    LOG_INFO("Device service initialized");
    return 0;
}

int device_service_start() {
    LOG_INFO("Starting device service...");

    g_device_service.running = 1;
    if (pthread_create(&g_lamp_thread, NULL, lamp_worker_thread, NULL) != 0) {
        LOG_ERROR("Failed to create lamp worker thread");
        g_device_service.running = 0;
        return -1;
    }
    g_lamp_thread_started = 1;

    if (ec11_start() != 0) {
        LOG_ERROR("EC11 start failed");
        g_device_service.running = 0;
        if (g_lamp_thread_started) {
            pthread_join(g_lamp_thread, NULL);
            g_lamp_thread_started = 0;
        }
        return -1;
    }

    LOG_INFO("Device service started");
    return 0;
}

void device_service_stop() {
    LOG_INFO("Stopping device service...");

    g_device_service.running = 0;
    ec11_stop();
    if (g_lamp_thread_started) {
        pthread_join(g_lamp_thread, NULL);
        g_lamp_thread_started = 0;
    }

    LOG_INFO("Device service stopped");
}

void device_service_cleanup() {
    LOG_INFO("Cleaning up device service...");

    ec11_cleanup();

    if (g_pwm_initialized) {
        pwm_write(PWM_WARM_CHANNEL, "duty_cycle", PWM_PERIOD);
        pwm_write(PWM_COLD_CHANNEL, "duty_cycle", PWM_PERIOD);
        pwm_enable(PWM_WARM_CHANNEL, 0);
        pwm_enable(PWM_COLD_CHANNEL, 0);
        g_pwm_initialized = 0;
    }

    LOG_INFO("Device service cleaned up");
}

void device_handle_fusion_state(const FusionState *state) {
    LOG_INFO("Received fusion state: %d", state->current_state);

    switch (state->current_state) {
        case STATE_SEATED_IDLE:
            LOG_INFO("User is seated but idle, switching to reading light");
            set_lamp_scene(70, 0.45f, LAMP_MODE_STATIC, 1800, 0);
            break;

        case STATE_FOCUSED:
            LOG_INFO("User is focused, switching to warm study light");
            set_lamp_scene(60, 0.30f, LAMP_MODE_STATIC, 15000, 0);
            break;

        case STATE_DISTRACTED:
            LOG_INFO("User is distracted, switching to breathing reminder");
            set_lamp_scene(40, 0.35f, LAMP_MODE_BREATH, 1200, 8);
            break;

        case STATE_TIRED:
            LOG_INFO("User is tired, switching to cool alert light");
            set_lamp_scene(90, 0.85f, LAMP_MODE_STATIC, 20000, 0);
            break;

        case STATE_ABSENT:
            LOG_INFO("User is absent, fading lamp off");
            set_lamp_scene(0, 0.45f, LAMP_MODE_STATIC, 10000, 0);
            break;

        default:
            LOG_WARN("Unknown state: %d", state->current_state);
            break;
    }

    g_device_service.current_state = state->current_state;
}

int device_control_lamp(uint8_t action, uint8_t brightness, uint16_t color_temp) {
    float color_ratio;

    if (action == 0) {
        set_lamp_scene(0, 0.45f, LAMP_MODE_STATIC, 2500, 0);
        return 0;
    }

    color_ratio = (float)(color_temp - 2700) / (6500 - 2700);
    if (color_ratio < 0.0f) {
        color_ratio = 0.0f;
    }
    if (color_ratio > 1.0f) {
        color_ratio = 1.0f;
    }

    if (action == 2) {
        set_lamp_scene(brightness, color_ratio, LAMP_MODE_BREATH, 1200, 8);
        return 0;
    }

    if (action == 1) {
        set_lamp_scene(brightness, color_ratio, LAMP_MODE_STATIC, 1500, 0);
        return 0;
    }

    LOG_WARN("Unknown lamp action: %d", action);
    return -1;
}
