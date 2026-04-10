#include <stdio.h>
#include <signal.h>
#include "vision_uvc.h"

static volatile int g_exit = 0;

static void sig_handler(int sig)
{
    (void)sig;
    g_exit = 1;
}

int main(void)
{
    sample_uvc_capture_ctx cap;
    ot_video_frame_info frame;
    td_s32 ret;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    ret = sample_uvc_capture_open(&cap, "/dev/video0", "MJPEG", 1920, 1080);
    if (ret != TD_SUCCESS) {
        printf("sample_uvc_capture_open failed\n");
        return -1;
    }

    while (!g_exit) {
        ret = sample_uvc_capture_read_frame(&cap, &frame, 2000);
        if (ret != TD_SUCCESS) {
            printf("sample_uvc_capture_read_frame failed\n");
            continue;
        }

        printf("get frame ok: %ux%u stride=%u pixel_format=%d\n",
            frame.video_frame.width,
            frame.video_frame.height,
            frame.video_frame.stride[0],
            frame.video_frame.pixel_format);

        ret = sample_uvc_capture_release_frame(&frame);
        if (ret != TD_SUCCESS) {
            printf("sample_uvc_capture_release_frame failed\n");
            break;
        }
    }

    sample_uvc_capture_close(&cap);
    return 0;
}