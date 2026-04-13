#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "vision_uvc.h"
#include "ot_common_sys.h"
#include "npu_process.h"
#include "sdk_module_init.h"

/* npu_process.h may not yet declare this new helper */
extern td_s32 sample_svp_npu_set_face_det_frame(const ot_video_frame_info *frame, const td_u8 *frame_virt);

#define UVC_DEV_PATH            "/dev/video0"
#define UVC_PIX_FMT             "MJPEG"
#define UVC_WIDTH               1920
#define UVC_HEIGHT              1080
#define UVC_TIMEOUT_MS          2000

#define SAVE_INTERVAL_SEC       1

#define DEBUG_FRAME_DIR         "./frames"
#define DATA_DIR                "./data"
#define DATA_INPUT_DIR          "./data/input"

static volatile int g_exit = 0;

static void sig_handler(int sig)
{
    (void)sig;
    g_exit = 1;
    sample_svp_npu_acl_handle_sig();
}

static int ensure_dir(const char *dir_path)
{
    struct stat st;

    if (dir_path == NULL || dir_path[0] == '\0') {
        return -1;
    }

    if (stat(dir_path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }
        fprintf(stderr, "path exists but is not a directory: %s\n", dir_path);
        return -1;
    }

    if (mkdir(dir_path, 0755) != 0) {
        perror("mkdir failed");
        return -1;
    }

    return 0;
}

static int save_rgb_to_ppm(const char *filename, const uint8_t *rgb, int width, int height)
{
    FILE *fp = fopen(filename, "wb");
    size_t size;

    if (fp == NULL) {
        perror("fopen ppm failed");
        return -1;
    }

    if (fprintf(fp, "P6\n%d %d\n255\n", width, height) < 0) {
        perror("fprintf ppm failed");
        fclose(fp);
        return -1;
    }

    size = (size_t)width * (size_t)height * 3;
    if (fwrite(rgb, 1, size, fp) != size) {
        perror("fwrite ppm failed");
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

int main(void)
{
    sample_uvc_capture_ctx cap;
    ot_video_frame_info frame;
    td_s32 ret;
    time_t last_save_sec = 0;
    unsigned int save_index = 0;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    if (ensure_dir(DEBUG_FRAME_DIR) != 0) {
        return -1;
    }
    if (ensure_dir(DATA_DIR) != 0) {
        return -1;
    }
    if (ensure_dir(DATA_INPUT_DIR) != 0) {
        return -1;
    }

    SDK_init();

    ret = sample_uvc_capture_open(&cap, UVC_DEV_PATH, UVC_PIX_FMT, UVC_WIDTH, UVC_HEIGHT);
    if (ret != TD_SUCCESS) {
        printf("sample_uvc_capture_open failed\n");
        SDK_exit();
        return -1;
    }

    ret = sample_svp_npu_init_runtime();
    if (ret != TD_SUCCESS) {
        printf("sample_svp_npu_init_runtime failed\n");
        sample_uvc_capture_close(&cap);
        SDK_exit();
        return -1;
    }

    while (!g_exit) {
        int width;
        int height;
        int stride0;
        int stride1;
        size_t y_size;
        size_t frame_size;
        uint8_t *base = NULL;
        uint8_t *rgb = NULL;
        const uint8_t *y_plane;
        const uint8_t *vu_plane;
        time_t now_sec;
        sample_svp_frame_result infer_result;
        char ppm_name[256];

        ret = sample_uvc_capture_read_frame(&cap, &frame, UVC_TIMEOUT_MS);
        if (ret != TD_SUCCESS) {
            printf("sample_uvc_capture_read_frame failed\n");
            continue;
        }

        printf("get frame ok: %ux%u stride0=%u stride1=%u pixel_format=%d phys0=0x%llx phys1=0x%llx\n",
            frame.video_frame.width,
            frame.video_frame.height,
            frame.video_frame.stride[0],
            frame.video_frame.stride[1],
            frame.video_frame.pixel_format,
            (unsigned long long)frame.video_frame.phys_addr[0],
            (unsigned long long)frame.video_frame.phys_addr[1]);

        width = (int)frame.video_frame.width;
        height = (int)frame.video_frame.height;
        stride0 = (int)frame.video_frame.stride[0];
        stride1 = (int)frame.video_frame.stride[1];
        y_size = (size_t)stride0 * (size_t)height;
        frame_size = y_size + ((size_t)stride1 * (size_t)height / 2);

        if (width <= 0 || height <= 0 || stride0 <= 0 || stride1 <= 0 ||
            frame.video_frame.phys_addr[0] == 0) {
            printf("invalid frame meta, skip frame\n");
            (td_void)sample_uvc_capture_release_frame(&frame);
            continue;
        }

        base = (uint8_t *)ss_mpi_sys_mmap(frame.video_frame.phys_addr[0], frame_size);
        if (base == NULL) {
            printf("ss_mpi_sys_mmap failed\n");
            (td_void)sample_uvc_capture_release_frame(&frame);
            continue;
        }

        y_plane = base;
        vu_plane = base + y_size;

        rgb = (uint8_t *)malloc((size_t)width * (size_t)height * 3);
        if (rgb == NULL) {
            printf("malloc rgb failed\n");
            ss_mpi_sys_munmap(base, frame_size);
            (td_void)sample_uvc_capture_release_frame(&frame);
            continue;
        }

        printf("start convert nv21 -> rgb\n");
        nv21_to_rgb888_safe(y_plane, vu_plane, width, height, stride0, stride1, rgb);

        now_sec = time(NULL);
        if (last_save_sec == 0 || (now_sec - last_save_sec) >= SAVE_INTERVAL_SEC) {
            snprintf(ppm_name, sizeof(ppm_name), "%s/frame_%04u.ppm", DEBUG_FRAME_DIR, save_index);

            if (save_rgb_to_ppm(ppm_name, rgb, width, height) == 0) {
                printf("saved rgb image: %s\n", ppm_name);
            } else {
                printf("save ppm failed\n");
            }

            last_save_sec = now_sec;
            save_index++;
        }

        ret = sample_svp_npu_set_face_det_frame(&frame, base);
        if (ret != TD_SUCCESS) {
            printf("sample_svp_npu_set_face_det_frame failed\n");
            free(rgb);
            ss_mpi_sys_munmap(base, frame_size);
            (td_void)sample_uvc_capture_release_frame(&frame);
            (void)memset(&frame, 0, sizeof(frame));
            continue;
        }

        (void)memset(&infer_result, 0, sizeof(infer_result));
        ret = sample_svp_npu_run_frame_pipeline_rgb888(rgb, (td_u32)width, (td_u32)height, &infer_result);
        if (ret != TD_SUCCESS) {
            printf("sample_svp_npu_run_frame_pipeline_rgb888 failed\n");
        } else {
            if (infer_result.has_face) {
                printf("[NPU] face=(%.1f, %.1f, %.1f, %.1f) score=%.3f yaw=%.2f pitch=%.2f\n",
                    infer_result.face.x1, infer_result.face.y1,
                    infer_result.face.x2, infer_result.face.y2,
                    infer_result.face.score,
                    infer_result.gaze.yaw_deg,
                    infer_result.gaze.pitch_deg);
            } else {
                printf("[NPU] no face\n");
            }
        }

        free(rgb);
        ss_mpi_sys_munmap(base, frame_size);

        ret = sample_uvc_capture_release_frame(&frame);
        if (ret != TD_SUCCESS) {
            printf("sample_uvc_capture_release_frame failed\n");
            break;
        }

        (void)memset(&frame, 0, sizeof(frame));
    }

    sample_svp_npu_deinit_runtime();
    sample_uvc_capture_close(&cap);
    SDK_exit();
    printf("exit main\n");
    return 0;
}