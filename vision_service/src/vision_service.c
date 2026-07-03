#include "vision_service.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
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
#include "vision_display.h"
#include "vision_uvc.h"

#define VISION_FRAME_QUEUE_CAPACITY 2
#define VISION_ERROR_RETRY_US       100000
#define VISION_WAIT_INTERVAL_NS     100000000L
#define VISION_CONTROL_PACKET_MAX   64

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
    pthread_t control_thread;
    td_bool producer_started;
    td_bool control_started;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    pthread_mutex_t monitor_mutex;
    pthread_cond_t monitor_cond;
    vision_frame_item queue[VISION_FRAME_QUEUE_CAPACITY];
    td_u32 queue_head;
    td_u32 queue_count;
    vision_metrics metrics;
    vision_debug_context debug;
    td_bool monitoring_enabled;
    td_bool npu_inited;
    td_s32 control_fd;
} vision_service_context;

static volatile sig_atomic_t g_stop_requested = 0;

static td_void vision_add_wait_interval(struct timespec *deadline)
{
    deadline->tv_nsec += VISION_WAIT_INTERVAL_NS;
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec++;
        deadline->tv_nsec -= 1000000000L;
    }
}

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
        vision_add_wait_interval(&deadline);
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

static td_bool vision_monitoring_wait_enabled(vision_service_context *ctx)
{
    struct timespec deadline;
    td_bool enabled;

    (void)pthread_mutex_lock(&ctx->monitor_mutex);
    while (ctx->monitoring_enabled != TD_TRUE && g_stop_requested == 0) {
        (void)clock_gettime(CLOCK_REALTIME, &deadline);
        vision_add_wait_interval(&deadline);
        (void)pthread_cond_timedwait(&ctx->monitor_cond, &ctx->monitor_mutex, &deadline);
    }
    enabled = ctx->monitoring_enabled;
    (void)pthread_mutex_unlock(&ctx->monitor_mutex);
    return (enabled == TD_TRUE && g_stop_requested == 0) ? TD_TRUE : TD_FALSE;
}

static td_bool vision_monitoring_is_enabled(vision_service_context *ctx)
{
    td_bool enabled;

    (void)pthread_mutex_lock(&ctx->monitor_mutex);
    enabled = ctx->monitoring_enabled;
    (void)pthread_mutex_unlock(&ctx->monitor_mutex);
    return enabled;
}

static td_void vision_monitoring_set(vision_service_context *ctx, td_bool enabled)
{
    td_bool changed;

    (void)pthread_mutex_lock(&ctx->monitor_mutex);
    changed = (ctx->monitoring_enabled != enabled) ? TD_TRUE : TD_FALSE;
    ctx->monitoring_enabled = enabled;
    (void)pthread_cond_broadcast(&ctx->monitor_cond);
    (void)pthread_mutex_unlock(&ctx->monitor_mutex);

    if (enabled != TD_TRUE) {
        vision_queue_drain(ctx);
    }
    if (changed == TD_TRUE) {
        printf("vision: monitoring %s\n", enabled == TD_TRUE ? "enabled" : "disabled");
    }
}

static td_s32 vision_control_open(vision_service_context *ctx)
{
    struct sockaddr_in addr;
    int opt = 1;

    ctx->control_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->control_fd < 0) {
        perror("vision: control socket");
        return TD_FAILURE;
    }

    (void)setsockopt(ctx->control_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    (void)memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(ctx->config.control_port);

    if (bind(ctx->control_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("vision: control bind");
        close(ctx->control_fd);
        ctx->control_fd = -1;
        return TD_FAILURE;
    }

    printf("vision: control UDP -> 127.0.0.1:%u\n", ctx->config.control_port);
    return TD_SUCCESS;
}

static td_void *vision_control_thread(td_void *arg)
{
    vision_service_context *ctx = (vision_service_context *)arg;
    char buffer[VISION_CONTROL_PACKET_MAX];

    while (g_stop_requested == 0) {
        fd_set fds;
        struct timeval timeout = {1, 0};
        int ret;
        ssize_t received;

        FD_ZERO(&fds);
        FD_SET(ctx->control_fd, &fds);
        ret = select(ctx->control_fd + 1, &fds, TD_NULL, TD_NULL, &timeout);
        if (ret <= 0) {
            continue;
        }

        received = recvfrom(ctx->control_fd, buffer, sizeof(buffer) - 1, 0,
            TD_NULL, TD_NULL);
        if (received <= 0) {
            continue;
        }
        buffer[received] = '\0';

        if (strncmp(buffer, "enable", 6) == 0 ||
            strncmp(buffer, "start", 5) == 0 ||
            strncmp(buffer, "1", 1) == 0) {
            vision_monitoring_set(ctx, TD_TRUE);
        } else if (strncmp(buffer, "disable", 7) == 0 ||
            strncmp(buffer, "stop", 4) == 0 ||
            strncmp(buffer, "0", 1) == 0) {
            vision_monitoring_set(ctx, TD_FALSE);
        } else {
            fprintf(stderr, "vision: ignored control command: %s\n", buffer);
        }
    }
    return TD_NULL;
}

static td_s32 vision_ensure_npu_runtime(vision_service_context *ctx)
{
    td_s32 ret;

    if (ctx->npu_inited == TD_TRUE) {
        return TD_SUCCESS;
    }

    printf("vision: initializing NPU runtime\n");
    ret = sample_svp_npu_init_runtime();
    if (ret != TD_SUCCESS) {
        fprintf(stderr, "vision: initialize NPU runtime failed, ret=%d\n", ret);
        return ret;
    }
    ctx->npu_inited = TD_TRUE;
    return TD_SUCCESS;
}

static td_void *vision_capture_thread(td_void *arg)
{
    vision_service_context *ctx = (vision_service_context *)arg;

    while (g_stop_requested == 0) {
        ot_video_frame_info frame = {0};
        td_s32 ret;

        if (vision_monitoring_wait_enabled(ctx) != TD_TRUE) {
            break;
        }

        ret = sample_uvc_capture_read_frame(&ctx->capture, &frame,
            ctx->config.capture_timeout_ms);
        if (ret != TD_SUCCESS) {
            if (g_stop_requested == 0) {
                fprintf(stderr, "vision: capture frame failed, ret=%d\n", ret);
                (void)usleep(VISION_ERROR_RETRY_US);
            }
            continue;
        }
        if (vision_monitoring_is_enabled(ctx) != TD_TRUE) {
            vision_release_frame(&frame);
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

        if (vision_monitoring_wait_enabled(ctx) != TD_TRUE) {
            break;
        }
        ret = vision_ensure_npu_runtime(ctx);
        if (ret != TD_SUCCESS) {
            return ret;
        }

        if (vision_queue_pop(ctx, &item) != TD_TRUE) {
            continue;
        }
        if (vision_monitoring_is_enabled(ctx) != TD_TRUE) {
            vision_release_frame(&item.frame);
            continue;
        }

        inference_started = vision_now_seconds();
        ret = vision_map_frame(&item.frame, &virt_addr, &mapped_size);
        if (ret == TD_SUCCESS) {
            ret = sample_svp_npu_process_frame(&item.frame, virt_addr, &result);
            finished = vision_now_seconds();
            inference_ms = (finished - inference_started) * 1000.0;
            if (vision_monitoring_is_enabled(ctx) == TD_TRUE) {
                vision_debug_publish(&ctx->debug, &item.frame, virt_addr, &result, ret, inference_ms);
            }
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

td_s32 vision_service_run(const vision_service_config *config)
{
    vision_service_context ctx;
    td_bool sdk_inited = TD_FALSE;
    td_bool capture_opened = TD_FALSE;
    td_bool display_started = TD_FALSE;
    td_s32 ret = TD_FAILURE;

    if (config == TD_NULL || config->device_path == TD_NULL ||
        config->pixel_format == TD_NULL || config->width == 0 ||
        config->height == 0 || config->capture_timeout_ms <= 0) {
        return TD_FAILURE;
    }
    if (config->display_enable == TD_TRUE && config->mpp_attached == TD_TRUE) {
        fprintf(stderr, "vision: --display cannot be combined with --mpp-attached\n");
        return TD_FAILURE;
    }

    (void)memset(&ctx, 0, sizeof(ctx));
    ctx.config = *config;
    ctx.metrics.window_start = vision_now_seconds();
    ctx.monitoring_enabled = config->monitoring_default;
    ctx.control_fd = -1;
    g_stop_requested = 0;

    if (pthread_mutex_init(&ctx.queue_mutex, TD_NULL) != 0) {
        return TD_FAILURE;
    }
    if (pthread_cond_init(&ctx.queue_cond, TD_NULL) != 0) {
        (void)pthread_mutex_destroy(&ctx.queue_mutex);
        return TD_FAILURE;
    }
    if (pthread_mutex_init(&ctx.monitor_mutex, TD_NULL) != 0) {
        (void)pthread_cond_destroy(&ctx.queue_cond);
        (void)pthread_mutex_destroy(&ctx.queue_mutex);
        return TD_FAILURE;
    }
    if (pthread_cond_init(&ctx.monitor_cond, TD_NULL) != 0) {
        (void)pthread_mutex_destroy(&ctx.monitor_mutex);
        (void)pthread_cond_destroy(&ctx.queue_cond);
        (void)pthread_mutex_destroy(&ctx.queue_mutex);
        return TD_FAILURE;
    }
    printf("vision: monitoring initially %s\n",
        ctx.monitoring_enabled == TD_TRUE ? "enabled" : "disabled");

    SDK_init();
    if (config->mpp_attached != TD_TRUE) {
        sdk_inited = TD_TRUE;
    } else {
        printf("vision: attached to existing MPP SYS/VB owner; SDK_exit will be skipped\n");
    }
    printf("vision: SDK init stage complete\n");

    ret = vision_debug_init(&ctx.debug, config);
    if (ret != TD_SUCCESS) {
        fprintf(stderr, "vision: initialize debug outputs failed\n");
        goto cleanup;
    }

    ret = vision_control_open(&ctx);
    if (ret != TD_SUCCESS) {
        fprintf(stderr, "vision: initialize control socket failed\n");
        goto cleanup;
    }
    ret = pthread_create(&ctx.control_thread, TD_NULL, vision_control_thread, &ctx);
    if (ret != 0) {
        fprintf(stderr, "vision: create control thread failed: %s\n", strerror(ret));
        ret = TD_FAILURE;
        goto cleanup;
    }
    ctx.control_started = TD_TRUE;

    ret = sample_uvc_capture_open(&ctx.capture, config->device_path,
        config->pixel_format, config->width, config->height, config->mpp_attached);
    if (ret != TD_SUCCESS) {
        fprintf(stderr, "vision: open capture failed, ret=%d\n", ret);
        goto cleanup;
    }
    capture_opened = TD_TRUE;
    printf("vision: capture/media initialized\n");

    if (config->display_enable == TD_TRUE) {
        printf("vision: starting integrated display\n");
        ret = vision_display_start(config->display_ready_file);
        if (ret != TD_SUCCESS) {
            fprintf(stderr, "vision: initialize display failed, ret=%d\n", ret);
            goto cleanup;
        }
        display_started = TD_TRUE;
        printf("vision: integrated display initialized\n");
    }

    ret = pthread_create(&ctx.producer_thread, TD_NULL, vision_capture_thread, &ctx);
    if (ret != 0) {
        fprintf(stderr, "vision: create capture thread failed: %s\n", strerror(ret));
        ret = TD_FAILURE;
        goto cleanup;
    }
    ctx.producer_started = TD_TRUE;

    printf("vision: running device=%s format=%s size=%ux%u\n",
        config->device_path, config->pixel_format, config->width, config->height);
    ret = vision_process_loop(&ctx);

cleanup:
    g_stop_requested = 1;
    (void)pthread_cond_broadcast(&ctx.monitor_cond);
    (void)pthread_cond_broadcast(&ctx.queue_cond);
    if (ctx.control_started == TD_TRUE) {
        (void)pthread_join(ctx.control_thread, TD_NULL);
    }
    if (ctx.control_fd >= 0) {
        (void)close(ctx.control_fd);
        ctx.control_fd = -1;
    }
    if (ctx.producer_started == TD_TRUE) {
        (void)pthread_join(ctx.producer_thread, TD_NULL);
    }
    vision_queue_drain(&ctx);
    if (ctx.npu_inited == TD_TRUE) {
        sample_svp_npu_deinit_runtime();
    }
    if (display_started == TD_TRUE) {
        vision_display_stop();
    }
    if (capture_opened == TD_TRUE) {
        (void)sample_uvc_capture_close(&ctx.capture);
    }
    if (sdk_inited == TD_TRUE) {
        SDK_exit();
    }
    vision_debug_deinit(&ctx.debug);
    (void)pthread_cond_destroy(&ctx.monitor_cond);
    (void)pthread_mutex_destroy(&ctx.monitor_mutex);
    (void)pthread_cond_destroy(&ctx.queue_cond);
    (void)pthread_mutex_destroy(&ctx.queue_mutex);
    return ret;
}
