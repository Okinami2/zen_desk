#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <time.h>
#include <string.h>

// 日志级别
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3
} LogLevel;

// 当前日志级别
extern LogLevel g_log_level;

// 日志宏
#define LOG_DEBUG(fmt, ...) \
    do { \
        if (g_log_level <= LOG_LEVEL_DEBUG) { \
            printf("[DEBUG][%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
        } \
    } while(0)

#define LOG_INFO(fmt, ...) \
    do { \
        if (g_log_level <= LOG_LEVEL_INFO) { \
            printf("[INFO][%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
        } \
    } while(0)

#define LOG_WARN(fmt, ...) \
    do { \
        if (g_log_level <= LOG_LEVEL_WARN) { \
            printf("[WARN][%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
        } \
    } while(0)

#define LOG_ERROR(fmt, ...) \
    do { \
        if (g_log_level <= LOG_LEVEL_ERROR) { \
            printf("[ERROR][%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
        } \
    } while(0)

// 初始化日志系统
void logger_init(LogLevel level);

#endif // LOGGER_H
