#include <signal.h>
#include <stdio.h>
#include <string.h>
#include "vision_service.h"

#define VISION_UVC_DEV_PATH       "/dev/video0"
#define VISION_UVC_PIX_FMT        "MJPEG"
#define VISION_UVC_WIDTH          1280
#define VISION_UVC_HEIGHT         720
#define VISION_UVC_TIMEOUT_MS     2000

static void vision_signal_handler(int signo)
{
    (void)signo;
    vision_service_request_stop();
}

static td_s32 vision_install_signal_handlers(td_void)
{
    struct sigaction action;

    (void)memset(&action, 0, sizeof(action));
    action.sa_handler = vision_signal_handler;
    (void)sigemptyset(&action.sa_mask);

    if (sigaction(SIGINT, &action, TD_NULL) != 0 ||
        sigaction(SIGTERM, &action, TD_NULL) != 0) {
        perror("sigaction");
        return TD_FAILURE;
    }
    return TD_SUCCESS;
}

int main(void)
{
    const vision_service_config config = {
        .device_path = VISION_UVC_DEV_PATH,
        .pixel_format = VISION_UVC_PIX_FMT,
        .width = VISION_UVC_WIDTH,
        .height = VISION_UVC_HEIGHT,
        .capture_timeout_ms = VISION_UVC_TIMEOUT_MS,
    };

    if (vision_install_signal_handlers() != TD_SUCCESS) {
        return 1;
    }

    return (vision_service_run(&config) == TD_SUCCESS) ? 0 : 1;
}
