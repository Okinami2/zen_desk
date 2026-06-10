#include "vision_service.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "npu_process.h"
#include "ot_common_sys.h"
#include "sdk_module_init.h"
#include "vision_debug.h"
#include "vision_uvc.h"

#define VISION_FRAME_QUEUE_CAPACITY 2
#define VISION_ERROR_RETRY_US       100000
#define VISION_WAIT_INTERVAL_NS     100000000L
#define VISION_MPP_LOCK_PATH        "/tmp/pegasus-mpp.lock"

typedef struct {
    ot_video_frame_info frame;
    td_double captured_at;
} vision_frame_item;

typedef struct {
    td_u64 captured;
    td_u64 dropped;
    td_u64 processed;
    td_u64 succeeded;
    td_u64 face_frames;
    td_double window_start;
    td_double sum_capture_latency;
    td_double sum_inference;
    td_double sum_total;
    td_double max_inference;
    td_double max_total;
} vision_metrics;

typedef struct {
    vision_service_config config;
    sample_uvc_capture_ctx capture;
    pthread_t producer_thread;
    td_bool producer_started;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    vision_frame_item queue[VISION_FRAME_QUEUE_CAPACITY];
    td_u32 queue_head;
    td_u32 queue_count;
    vision_metrics metrics;
    vision_debug_context debug;
    td_s32 display_lock_fd;
} vision_service_context;

static volatile sig_atomic_t g_stop_requested = 0;

static td_double vision_now_seconds(td_void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (td_double)ts.tv_sec + (td_double)ts.tv_nsec / 1000000000.0;
}

static td_void vision_release_frame(ot_video_frame_info *frame)
{
    if (frame->video_frame.phys_addr[0] == 0) {
        return;
    }
    if (sample_uvc_capture_release_frame(frame) != TD_SUCCESS) {
        fprintf(stderr, "vision: release frame failed\n");
    }
    (void)memset(frame, 0, sizeof(*frame));
}

static td_void vision_queue_push_latest(vision_service_context *ctx,
    const ot_video_frame_info *frame, td_double captured_at)
{
    td_u32 tail;

    (void)pthread_mutex_lock(&ctx->queue_mutex);
    if (ctx->queue_count == VISION_FRAME_QUEUE_CAPACITY) {
        vision_release_frame(&ctx->queue[ctx->queue_head].frame);
        ctx->queue_head = (ctx->queue_head + 1) % VISION_FRAME_QUEUE_CAPACITY;
        ctx->queue_count--;
        ctx->metrics.dropped++;
    }

    tail = (ctx->queue_head + ctx->queue_count) % VISION_FRAME_QUEUE_CAPACITY;
    ctx->queue[tail].frame = *frame;
    ctx->queue[tail].captured_at = captured_at;
    ctx->queue_count++;
    ctx->metrics.captured++;
    (void)pthread_cond_signal(&ctx->queue_cond);
    (void)pthread_mutex_unlock(&ctx->queue_mutex);
}

static td_bool vision_queue_pop(vision_service_context *ctx, vision_frame_item *item)
{
    struct timespec deadline;

    (void)pthread_mutex_lock(&ctx->queue_mutex);
    while (ctx->queue_count == 0 && g_stop_requested == 0) {
        (void)clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_nsec += VISION_WAIT_INTERVAL_NS;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
        (void)pthread_cond_timedwait(&ctx->queue_cond, &ctx->queue_mutex, &deadline);
    }

    if (ctx->queue_count == 0) {
        (void)pthread_mutex_unlock(&ctx->queue_mutex);
        return TD_FALSE;
    }

    *item = ctx->queue[ctx->queue_head];
    (void)memset(&ctx->queue[ctx->queue_head], 0, sizeof(ctx->queue[ctx->queue_head]));
    ctx->queue_head = (ctx->queue_head + 1) % VISION_FRAME_QUEUE_CAPACITY;
    ctx->queue_count--;
    (void)pthread_mutex_unlock(&ctx->queue_mutex);
    return TD_TRUE;
}

static td_void vision_queue_drain(vision_service_context *ctx)
{
    (void)pthread_mutex_lock(&ctx->queue_mutex);
    while (ctx->queue_count > 0) {
        vision_release_frame(&ctx->queue[ctx->queue_head].frame);
        ctx->queue_head = (ctx->queue_head + 1) % VISION_FRAME_QUEUE_CAPACITY;
        ctx->queue_count--;
    }
    (void)pthread_mutex_unlock(&ctx->queue_mutex);
}

static td_void *vision_capture_thread(td_void *arg)
{
    vision_service_context *ctx = (vision_service_context *)arg;

    while (g_stop_requested == 0) {
        ot_video_frame_info frame = {0};
        td_s32 ret;

        ret = sample_uvc_capture_read_frame(&ctx->capture, &frame,
            ctx->config.capture_timeout_ms);
        if (ret != TD_SUCCESS) {
            if (g_stop_requested == 0) {
                fprintf(stderr, "vision: capture frame failed, ret=%d\n", ret);
                (void)usleep(VISION_ERROR_RETRY_US);
            }
            continue;
        }
        vision_queue_push_latest(ctx, &frame, vision_now_seconds());
    }
    return TD_NULL;
}

static td_s32 vision_map_frame(const ot_video_frame_info *frame,
    td_u8 **virt_addr, size_t *mapped_size)
{
    size_t y_size;
    size_t uv_size;

    if (frame == TD_NULL || virt_addr == TD_NULL || mapped_size == TD_NULL ||
        frame->video_frame.width == 0 || frame->video_frame.height == 0 ||
        frame->video_frame.stride[0] == 0 || frame->video_frame.stride[1] == 0 ||
        frame->video_frame.phys_addr[0] == 0) {
        return TD_FAILURE;
    }

    y_size = (size_t)frame->video_frame.stride[0] * frame->video_frame.height;
    uv_size = (size_t)frame->video_frame.stride[1] * frame->video_frame.height / 2;
    if (y_size > SIZE_MAX - uv_size || y_size + uv_size > UINT32_MAX) {
        return TD_FAILURE;
    }

    *mapped_size = y_size + uv_size;
    *virt_addr = (td_u8 *)ss_mpi_sys_mmap(
        frame->video_frame.phys_addr[0], (td_u32)*mapped_size);
    return (*virt_addr == TD_NULL) ? TD_FAILURE : TD_SUCCESS;
}

static td_void vision_metrics_commit(vision_service_context *ctx,
    const vision_frame_item *item, td_double inference_started,
    td_double finished, td_s32 infer_ret, const sample_svp_frame_result *result)
{
    vision_metrics *metrics = &ctx->metrics;
    td_double window;

    (void)pthread_mutex_lock(&ctx->queue_mutex);
    metrics->processed++;
    if (infer_ret == TD_SUCCESS) {
        metrics->succeeded++;
        if (result->has_face == TD_TRUE) {
            metrics->face_frames++;
        }
    }
    metrics->sum_capture_latency += inference_started - item->captured_at;
    metrics->sum_inference += finished - inference_started;
    metrics->sum_total += finished - item->captured_at;
    if (finished - inference_started > metrics->max_inference) {
        metrics->max_inference = finished - inference_started;
    }
    if (finished - item->captured_at > metrics->max_total) {
        metrics->max_total = finished - item->captured_at;
    }

    window = finished - metrics->window_start;
    if (window < 1.0 || metrics->processed == 0) {
        (void)pthread_mutex_unlock(&ctx->queue_mutex);
        return;
    }

    printf("[VISION] capture_fps=%.2f process_fps=%.2f success_fps=%.2f "
        "drop=%llu face_ratio=%.1f%% avg_ms(queue=%.2f infer=%.2f total=%.2f) "
        "max_ms(infer=%.2f total=%.2f)\n",
        (td_double)metrics->captured / window,
        (td_double)metrics->processed / window,
        (td_double)metrics->succeeded / window,
        (unsigned long long)metrics->dropped,
        metrics->succeeded == 0 ? 0.0 :
            (td_double)metrics->face_frames * 100.0 / metrics->succeeded,
        metrics->sum_capture_latency * 1000.0 / metrics->processed,
        metrics->sum_inference * 1000.0 / metrics->processed,
        metrics->sum_total * 1000.0 / metrics->processed,
        metrics->max_inference * 1000.0,
        metrics->max_total * 1000.0);

    (void)memset(metrics, 0, sizeof(*metrics));
    metrics->window_start = finished;
    (void)pthread_mutex_unlock(&ctx->queue_mutex);
}

static td_s32 vision_process_loop(vision_service_context *ctx)
{
    while (g_stop_requested == 0) {
        vision_frame_item item = {0};
        sample_svp_frame_result result = {0};
        td_u8 *virt_addr = TD_NULL;
        size_t mapped_size = 0;
        td_double inference_started;
        td_double finished;
        td_s32 ret;
        td_double inference_ms;

        if (vision_queue_pop(ctx, &item) != TD_TRUE) {
            continue;
        }

        inference_started = vision_now_seconds();
        ret = vision_map_frame(&item.frame, &virt_addr, &mapped_size);
        if (ret == TD_SUCCESS) {
            ret = sample_svp_npu_process_frame(&item.frame, virt_addr, &result);
            finished = vision_now_seconds();
            inference_ms = (finished - inference_started) * 1000.0;
            vision_debug_publish(&ctx->debug, &item.frame, virt_addr, &result, ret, inference_ms);
            (void)ss_mpi_sys_munmap(virt_addr, (td_u32)mapped_size);
        } else {
            fprintf(stderr, "vision: invalid or unmappable video frame\n");
        }
        finished = vision_now_seconds();

        vision_release_frame(&item.frame);
        vision_metrics_commit(ctx, &item, inference_started, finished, ret, &result);
    }
    return TD_SUCCESS;
}

td_void vision_service_request_stop(td_void)
{
    g_stop_requested = 1;
}

static td_s32 vision_display_lock_acquire(vision_service_context *ctx)
{
    ctx->display_lock_fd = open(VISION_MPP_LOCK_PATH, O_CREAT | O_RDWR, 0666);
    if (ctx->display_lock_fd < 0) {
        perror("vision: open MPP lock");
        return TD_FAILURE;
    }
    if (flock(ctx->display_lock_fd, LOCK_EX | LOCK_NB) != 0) {
        fprintf(stderr,
            "vision: MPP resources are owned by vo_init/Qt or another vision process\n");
        close(ctx->display_lock_fd);
        ctx->display_lock_fd = -1;
        return TD_FAILURE;
    }
    return TD_SUCCESS;
}

static td_void vision_display_lock_release(vision_service_context *ctx)
{
    if (ctx->display_lock_fd >= 0) {
        (void)flock(ctx->display_lock_fd, LOCK_UN);
        (void)close(ctx->display_lock_fd);
        ctx->display_lock_fd = -1;
    }
}

td_s32 vision_service_run(const vision_service_config *config)
{
    vision_service_context ctx;
    td_bool sdk_inited = TD_FALSE;
    td_bool capture_opened = TD_FALSE;
    td_bool npu_inited = TD_FALSE;
    td_s32 ret = TD_FAILURE;

    if (config == TD_NULL || config->device_path == TD_NULL ||
        config->pixel_format == TD_NULL || config->width == 0 ||
        config->height == 0 || config->capture_timeout_ms <= 0) {
        return TD_FAILURE;
    }

    (void)memset(&ctx, 0, sizeof(ctx));
    ctx.config = *config;
    ctx.display_lock_fd = -1;
    ctx.metrics.window_start = vision_now_seconds();
    g_stop_requested = 0;

    if (pthread_mutex_init(&ctx.queue_mutex, TD_NULL) != 0) {
        return TD_FAILURE;
    }
    if (pthread_cond_init(&ctx.queue_cond, TD_NULL) != 0) {
        (void)pthread_mutex_destroy(&ctx.queue_mutex);
        return TD_FAILURE;
    }

    if (vision_display_lock_acquire(&ctx) != TD_SUCCESS) {
        goto cleanup_sync;
    }

    SDK_init();
    sdk_inited = TD_TRUE;

    ret = vision_debug_init(&ctx.debug, config);
    if (ret != TD_SUCCESS) {
        fprintf(stderr, "vision: initialize debug outputs failed\n");
        goto cleanup;
    }

    ret = sample_uvc_capture_open(&ctx.capture, config->device_path,
        config->pixel_format, config->width, config->height,
        config->hdmi_preview);
    if (ret != TD_SUCCESS) {
        fprintf(stderr, "vision: open capture failed, ret=%d\n", ret);
        goto cleanup;
    }
    capture_opened = TD_TRUE;

    ret = sample_svp_npu_init_runtime();
    if (ret != TD_SUCCESS) {
        fprintf(stderr, "vision: initialize NPU runtime failed, ret=%d\n", ret);
        goto cleanup;
    }
    npu_inited = TD_TRUE;

    ret = pthread_create(&ctx.producer_thread, TD_NULL, vision_capture_thread, &ctx);
    if (ret != 0) {
        fprintf(stderr, "vision: create capture thread failed: %s\n", strerror(ret));
        ret = TD_FAILURE;
        goto cleanup;
    }
    ctx.producer_started = TD_TRUE;

    printf("vision: running device=%s format=%s size=%ux%u hdmi=%s\n",
        config->device_path, config->pixel_format, config->width, config->height,
        config->hdmi_preview == TD_TRUE ? "exclusive" : "off");
    ret = vision_process_loop(&ctx);

cleanup:
    g_stop_requested = 1;
    if (ctx.producer_started == TD_TRUE) {
        (void)pthread_join(ctx.producer_thread, TD_NULL);
    }
    vision_queue_drain(&ctx);
    if (npu_inited == TD_TRUE) {
        sample_svp_npu_deinit_runtime();
    }
    if (capture_opened == TD_TRUE) {
        (void)sample_uvc_capture_close(&ctx.capture);
    }
    if (sdk_inited == TD_TRUE) {
        SDK_exit();
    }
    vision_debug_deinit(&ctx.debug);
    vision_display_lock_release(&ctx);
cleanup_sync:
    (void)pthread_cond_destroy(&ctx.queue_cond);
    (void)pthread_mutex_destroy(&ctx.queue_mutex);
    return ret;
}
