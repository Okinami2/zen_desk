#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <math.h>

#include "vision_uvc.h"
#include "vision_media.h"
#include "ot_common_sys.h"
#include "npu_process.h"
#include "sdk_module_init.h"

/* npu_process.h may not yet declare this new helper */
extern td_s32 sample_svp_npu_set_face_det_frame(const ot_video_frame_info *frame, const td_u8 *frame_virt);

#define UVC_DEV_PATH            "/dev/video0"
#define UVC_PIX_FMT             "MJPEG"
#define UVC_WIDTH               1280
#define UVC_HEIGHT              720
#define UVC_TIMEOUT_MS          2000
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

static double now_monotonic_seconds(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

int main(void)
{
    sample_uvc_capture_ctx cap;
    ot_video_frame_info frame;
    td_s32 ret;
    td_u64 fps_frames = 0;
    td_u64 fps_ok_frames = 0;
    td_u64 fps_face_frames = 0;
    double fps_window_start = 0.0;
    double t_sum_read = 0.0;
    double t_sum_mmap = 0.0;
    double t_sum_convert = 0.0;
    double t_sum_set_frame = 0.0;
    double t_sum_pipeline = 0.0;
    double t_sum_release = 0.0;
    double t_sum_loop = 0.0;
    double t_max_read = 0.0;
    double t_max_convert = 0.0;
    double t_max_pipeline = 0.0;
    double t_max_loop = 0.0;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

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

    fps_window_start = now_monotonic_seconds();

    while (!g_exit) {
        int width;
        int height;
        int stride0;
        int stride1;
        size_t y_size;
        size_t frame_size;
        uint8_t *base = NULL;
        sample_svp_frame_result infer_result;
        double t0;
        double t1;
        double t2;
        double t3;
        double t4;
        double t5;
        double t6;
        double v;

        t0 = now_monotonic_seconds();

        ret = sample_uvc_capture_read_frame(&cap, &frame, UVC_TIMEOUT_MS);
        t1 = now_monotonic_seconds();
        if (ret != TD_SUCCESS) {
            printf("sample_uvc_capture_read_frame failed\n");
            continue;
        }

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
        t2 = now_monotonic_seconds();
        if (base == NULL) {
            printf("ss_mpi_sys_mmap failed\n");
            (td_void)sample_uvc_capture_release_frame(&frame);
            continue;
        }

        t3 = now_monotonic_seconds();

        ret = sample_svp_npu_set_face_det_frame(&frame, base);
        t4 = now_monotonic_seconds();
        if (ret != TD_SUCCESS) {
            printf("sample_svp_npu_set_face_det_frame failed\n");
            ss_mpi_sys_munmap(base, frame_size);
            (td_void)sample_uvc_capture_release_frame(&frame);
            (void)memset(&frame, 0, sizeof(frame));
            continue;
        }

        (void)memset(&infer_result, 0, sizeof(infer_result));
        ret = sample_svp_npu_run_frame_pipeline_rgb888(TD_NULL, (td_u32)width, (td_u32)height, &infer_result);
        t5 = now_monotonic_seconds();
        fps_frames++;
        if (ret != TD_SUCCESS) {
            printf("sample_svp_npu_run_frame_pipeline_rgb888 failed\n");
        } else {
            fps_ok_frames++;
            if (infer_result.has_face) {
                fps_face_frames++;
            }
        }

        {
            double now = now_monotonic_seconds();
            double dt = now - fps_window_start;
            if (dt >= 1.0) {
                double fps_total = (double)fps_frames / dt;
                double fps_ok = (double)fps_ok_frames / dt;
                double face_ratio = (fps_ok_frames == 0) ? 0.0 : ((double)fps_face_frames * 100.0 / (double)fps_ok_frames);
                printf("[PERF] fps_total=%.2f fps_ok=%.2f face_ratio=%.1f%% window=%.2fs\n",
                    fps_total, fps_ok, face_ratio, dt);
                fps_window_start = now;
                fps_frames = 0;
                fps_ok_frames = 0;
                fps_face_frames = 0;
            }
        }

        ss_mpi_sys_munmap(base, frame_size);

        ret = sample_uvc_capture_release_frame(&frame);
        t6 = now_monotonic_seconds();
        if (ret != TD_SUCCESS) {
            printf("sample_uvc_capture_release_frame failed\n");
            break;
        }

        v = t1 - t0;
        t_sum_read += v;
        if (v > t_max_read) {
            t_max_read = v;
        }

        t_sum_mmap += (t2 - t1);

        v = t3 - t2;
        t_sum_convert += v;
        if (v > t_max_convert) {
            t_max_convert = v;
        }

        t_sum_set_frame += (t4 - t3);

        v = t5 - t4;
        t_sum_pipeline += v;
        if (v > t_max_pipeline) {
            t_max_pipeline = v;
        }

        t_sum_release += (t6 - t5);

        v = t6 - t0;
        t_sum_loop += v;
        if (v > t_max_loop) {
            t_max_loop = v;
        }

        (void)memset(&frame, 0, sizeof(frame));

        {
            double now = now_monotonic_seconds();
            double dt = now - fps_window_start;
            if (dt >= 1.0 && fps_frames > 0) {
                double inv_n = 1.0 / (double)fps_frames;
                printf("[PERF-DETAIL] avg_ms read=%.2f mmap=%.2f convert=%.2f set=%.2f pipeline=%.2f release=%.2f loop=%.2f | max_ms read=%.2f convert=%.2f pipeline=%.2f loop=%.2f\n",
                    t_sum_read * 1000.0 * inv_n,
                    t_sum_mmap * 1000.0 * inv_n,
                    t_sum_convert * 1000.0 * inv_n,
                    t_sum_set_frame * 1000.0 * inv_n,
                    t_sum_pipeline * 1000.0 * inv_n,
                    t_sum_release * 1000.0 * inv_n,
                    t_sum_loop * 1000.0 * inv_n,
                    t_max_read * 1000.0,
                    t_max_convert * 1000.0,
                    t_max_pipeline * 1000.0,
                    t_max_loop * 1000.0);

                t_sum_read = 0.0;
                t_sum_mmap = 0.0;
                t_sum_convert = 0.0;
                t_sum_set_frame = 0.0;
                t_sum_pipeline = 0.0;
                t_sum_release = 0.0;
                t_sum_loop = 0.0;
                t_max_read = 0.0;
                t_max_convert = 0.0;
                t_max_pipeline = 0.0;
                t_max_loop = 0.0;
            }
        }
    }

    sample_svp_npu_deinit_runtime();
    sample_uvc_capture_close(&cap);
    SDK_exit();
    printf("exit main\n");
    return 0;
}