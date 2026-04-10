#include "logger.h"

LogLevel g_log_level = LOG_LEVEL_INFO;

void logger_init(LogLevel level) {
    g_log_level = level;
}
