#include "vision_debug.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "securec.h"
#include "vision_media.h"

#define VISION_JSON_BUFFER_SIZE 1024
#define VISION_COLOR_RED_R      255
#define VISION_COLOR_GREEN_G    255
#define VISION_DRAW_RADIUS      2

static td_s32 vision_debug_make_dirs(const td_char *path)
{
    td_char buffer[VISION_DEBUG_PATH_MAX];
    td_char *cursor;

    if (path == TD_NULL || path[0] == '\0' ||
        strlen(path) >= sizeof(buffer) ||
        strncpy_s(buffer, sizeof(buffer), path, sizeof(buffer) - 1) != EOK) {
        return TD_FAILURE;
    }

    for (cursor = buffer + 1; *cursor != '\0'; cursor++) {
        if (*cursor != '/') {
            continue;
        }
        *cursor = '\0';
        if (mkdir(buffer, 0755) != 0 && errno != EEXIST) {
            return TD_FAILURE;
        }
        *cursor = '/';
    }
    if (mkdir(buffer, 0755) != 0 && errno != EEXIST) {
        return TD_FAILURE;
    }
    return TD_SUCCESS;
}

static td_u64 vision_debug_realtime_ms(td_void)
{
    struct timespec now;

    (void)clock_gettime(CLOCK_REALTIME, &now);
    return (td_u64)now.tv_sec * 1000ULL + (td_u64)now.tv_nsec / 1000000ULL;
}

static td_s32 vision_debug_open_udp(vision_debug_context *ctx,
    const td_char *host, td_u16 port)
{
    struct sockaddr_in *addr = (struct sockaddr_in *)&ctx->telemetry_addr;

    ctx->telemetry_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->telemetry_fd < 0) {
        perror("vision: telemetry socket");
        return TD_FAILURE;
    }

    (void)memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr->sin_addr) != 1) {
        fprintf(stderr, "vision: telemetry host must be an IPv4 address: %s\n", host);
        close(ctx->telemetry_fd);
        ctx->telemetry_fd = -1;
        return TD_FAILURE;
    }
    ctx->telemetry_addr_len = sizeof(*addr);
    ctx->telemetry_enabled = TD_TRUE;
    return TD_SUCCESS;
}

td_s32 vision_debug_init(vision_debug_context *ctx,
    const vision_service_config *config)
{
    if (ctx == TD_NULL || config == TD_NULL) {
        return TD_FAILURE;
    }
    (void)memset(ctx, 0, sizeof(*ctx));
    ctx->telemetry_fd = -1;

    if (config->telemetry_host != TD_NULL) {
        if (vision_debug_open_udp(ctx, config->telemetry_host,
            config->telemetry_port) != TD_SUCCESS) {
            return TD_FAILURE;
        }
        printf("vision: UDP telemetry -> %s:%u\n",
            config->telemetry_host, config->telemetry_port);
    }

    if (config->snapshot_dir != TD_NULL) {
        if (strlen(config->snapshot_dir) >= sizeof(ctx->snapshot_dir) ||
            strncpy_s(ctx->snapshot_dir, sizeof(ctx->snapshot_dir),
                config->snapshot_dir, sizeof(ctx->snapshot_dir) - 1) != EOK ||
            vision_debug_make_dirs(ctx->snapshot_dir) != TD_SUCCESS) {
            fprintf(stderr, "vision: cannot create snapshot directory: %s\n",
                config->snapshot_dir);
            vision_debug_deinit(ctx);
            return TD_FAILURE;
        }
        ctx->snapshot_every = config->snapshot_every;
        ctx->snapshot_limit = config->snapshot_limit;
        ctx->snapshots_enabled = TD_TRUE;
        printf("vision: snapshots -> %s (every=%u limit=%u)\n",
            ctx->snapshot_dir, ctx->snapshot_every, ctx->snapshot_limit);
    }
    return TD_SUCCESS;
}

td_void vision_debug_deinit(vision_debug_context *ctx)
{
    if (ctx == TD_NULL) {
        return;
    }
    if (ctx->telemetry_fd >= 0) {
        (void)close(ctx->telemetry_fd);
    }
    ctx->telemetry_fd = -1;
    ctx->telemetry_enabled = TD_FALSE;
}

static td_s32 vision_debug_format_summary(td_char *buffer, size_t size,
    td_u64 frame_id, const sample_svp_frame_result *result,
    td_s32 infer_ret, td_double inference_ms)
{
    int length = snprintf(buffer, size,
        "{\"frame_id\":%llu,\"timestamp_ms\":%llu,\"ok\":%s,"
        "\"has_face\":%s,\"face\":[%.2f,%.2f,%.2f,%.2f],"
        "\"score\":%.5f,\"yaw\":%.3f,\"pitch\":%.3f,\"roll\":%.3f,"
        "\"eyes_closed\":%s,\"yawning\":%s,\"blink_count\":%u,"
        "\"yawn_count\":%u,\"inference_ms\":%.3f}\n",
        (unsigned long long)frame_id,
        (unsigned long long)vision_debug_realtime_ms(),
        infer_ret == TD_SUCCESS ? "true" : "false",
        result->has_face == TD_TRUE ? "true" : "false",
        result->face.x1, result->face.y1, result->face.x2, result->face.y2,
        result->face.score, result->gaze.yaw_deg, result->gaze.pitch_deg,
        result->gaze.roll_deg,
        result->state_snapshot.eyes_closed == TD_TRUE ? "true" : "false",
        result->state_snapshot.yawning == TD_TRUE ? "true" : "false",
        result->state_snapshot.blink_count, result->state_snapshot.yawn_count,
        inference_ms);

    return (length < 0 || (size_t)length >= size) ? TD_FAILURE : length;
}

static td_void vision_debug_send_telemetry(vision_debug_context *ctx,
    const td_char *json, size_t length)
{
    ssize_t sent;

    if (ctx->telemetry_enabled != TD_TRUE) {
        return;
    }
    sent = sendto(ctx->telemetry_fd, json, length, MSG_DONTWAIT,
        (const struct sockaddr *)&ctx->telemetry_addr, ctx->telemetry_addr_len);
    if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        fprintf(stderr, "vision: UDP telemetry send failed: %s\n", strerror(errno));
    }
}

static td_void vision_debug_set_rgb(td_u8 *rgb, td_u32 width, td_u32 height,
    td_s32 x, td_s32 y, td_u8 red, td_u8 green, td_u8 blue)
{
    size_t offset;

    if (x < 0 || y < 0 || (td_u32)x >= width || (td_u32)y >= height) {
        return;
    }
    offset = ((size_t)y * width + (td_u32)x) * 3;
    rgb[offset] = red;
    rgb[offset + 1] = green;
    rgb[offset + 2] = blue;
}

static td_void vision_debug_draw_box(td_u8 *rgb, td_u32 width, td_u32 height,
    const sample_svp_face_box *box)
{
    td_s32 x;
    td_s32 y;
    td_s32 x1 = (td_s32)box->x1;
    td_s32 y1 = (td_s32)box->y1;
    td_s32 x2 = (td_s32)box->x2;
    td_s32 y2 = (td_s32)box->y2;
    td_s32 thickness;

    for (thickness = 0; thickness < 3; thickness++) {
        for (x = x1; x <= x2; x++) {
            vision_debug_set_rgb(rgb, width, height, x, y1 + thickness,
                VISION_COLOR_RED_R, 0, 0);
            vision_debug_set_rgb(rgb, width, height, x, y2 - thickness,
                VISION_COLOR_RED_R, 0, 0);
        }
        for (y = y1; y <= y2; y++) {
            vision_debug_set_rgb(rgb, width, height, x1 + thickness, y,
                VISION_COLOR_RED_R, 0, 0);
            vision_debug_set_rgb(rgb, width, height, x2 - thickness, y,
                VISION_COLOR_RED_R, 0, 0);
        }
    }
}

static td_void vision_debug_draw_landmarks(td_u8 *rgb, td_u32 width, td_u32 height,
    const sample_svp_landmark106_result *landmark)
{
    td_u32 i;
    td_s32 dx;
    td_s32 dy;

    for (i = 0; i < landmark->point_num && i < SAMPLE_SVP_LANDMARK_NUM; i++) {
        td_s32 x = (td_s32)landmark->points[i][0];
        td_s32 y = (td_s32)landmark->points[i][1];
        for (dy = -VISION_DRAW_RADIUS; dy <= VISION_DRAW_RADIUS; dy++) {
            for (dx = -VISION_DRAW_RADIUS; dx <= VISION_DRAW_RADIUS; dx++) {
                vision_debug_set_rgb(rgb, width, height, x + dx, y + dy,
                    0, VISION_COLOR_GREEN_G, 0);
            }
        }
    }
}

static td_s32 vision_debug_write_nv21(const td_char *path,
    const ot_video_frame_info *frame, const td_u8 *frame_virt)
{
    FILE *file = fopen(path, "wb");
    td_u32 row;
    const td_u8 *uv;

    if (file == TD_NULL) {
        return TD_FAILURE;
    }
    for (row = 0; row < frame->video_frame.height; row++) {
        if (fwrite(frame_virt + (size_t)row * frame->video_frame.stride[0],
            1, frame->video_frame.width, file) != frame->video_frame.width) {
            fclose(file);
            return TD_FAILURE;
        }
    }
    uv = frame_virt + (size_t)frame->video_frame.stride[0] * frame->video_frame.height;
    for (row = 0; row < frame->video_frame.height / 2; row++) {
        if (fwrite(uv + (size_t)row * frame->video_frame.stride[1],
            1, frame->video_frame.width, file) != frame->video_frame.width) {
            fclose(file);
            return TD_FAILURE;
        }
    }
    return fclose(file) == 0 ? TD_SUCCESS : TD_FAILURE;
}

static td_s32 vision_debug_write_ppm(const td_char *path,
    const ot_video_frame_info *frame, const td_u8 *frame_virt,
    const sample_svp_frame_result *result)
{
    td_u32 width = frame->video_frame.width;
    td_u32 height = frame->video_frame.height;
    size_t rgb_size;
    td_u8 *rgb;
    const td_u8 *uv;
    FILE *file;

    if (width > SIZE_MAX / height / 3) {
        return TD_FAILURE;
    }
    rgb_size = (size_t)width * height * 3;
    rgb = malloc(rgb_size);
    if (rgb == TD_NULL) {
        return TD_FAILURE;
    }
    uv = frame_virt + (size_t)frame->video_frame.stride[0] * height;
    nv21_to_rgb888_safe(frame_virt, uv, width, height,
        frame->video_frame.stride[0], frame->video_frame.stride[1], rgb);

    if (result->has_face == TD_TRUE) {
        vision_debug_draw_box(rgb, width, height, &result->face);
        vision_debug_draw_landmarks(rgb, width, height, &result->landmark);
    }

    file = fopen(path, "wb");
    if (file == TD_NULL) {
        free(rgb);
        return TD_FAILURE;
    }
    if (fprintf(file, "P6\n%u %u\n255\n", width, height) < 0 ||
        fwrite(rgb, 1, rgb_size, file) != rgb_size) {
        fclose(file);
        free(rgb);
        return TD_FAILURE;
    }
    free(rgb);
    return fclose(file) == 0 ? TD_SUCCESS : TD_FAILURE;
}

static td_s32 vision_debug_write_json(const td_char *path,
    const td_char *summary, const sample_svp_frame_result *result)
{
    FILE *file = fopen(path, "wb");
    td_u32 i;
    size_t summary_len = strlen(summary);

    if (file == TD_NULL) {
        return TD_FAILURE;
    }
    if (summary_len < 2 || fwrite(summary, 1, summary_len - 2, file) != summary_len - 2 ||
        fputs(",\"landmarks\":[", file) == EOF) {
        fclose(file);
        return TD_FAILURE;
    }
    for (i = 0; i < result->landmark.point_num && i < SAMPLE_SVP_LANDMARK_NUM; i++) {
        if (fprintf(file, "%s[%.3f,%.3f]", i == 0 ? "" : ",",
            result->landmark.points[i][0], result->landmark.points[i][1]) < 0) {
            fclose(file);
            return TD_FAILURE;
        }
    }
    if (fputs("]}\n", file) == EOF) {
        fclose(file);
        return TD_FAILURE;
    }
    return fclose(file) == 0 ? TD_SUCCESS : TD_FAILURE;
}

static td_void vision_debug_save_snapshot(vision_debug_context *ctx,
    td_u64 frame_id, const ot_video_frame_info *frame, const td_u8 *frame_virt,
    const sample_svp_frame_result *result, const td_char *summary)
{
    td_char base[VISION_DEBUG_PATH_MAX + 64];
    td_char path[VISION_DEBUG_PATH_MAX + 80];
    int length;
    td_s32 status = TD_SUCCESS;

    length = snprintf(base, sizeof(base), "%s/frame_%08llu",
        ctx->snapshot_dir, (unsigned long long)frame_id);
    if (length < 0 || (size_t)length >= sizeof(base)) {
        return;
    }

    (void)snprintf(path, sizeof(path), "%s.nv21", base);
    status |= vision_debug_write_nv21(path, frame, frame_virt);
    (void)snprintf(path, sizeof(path), "%s.ppm", base);
    status |= vision_debug_write_ppm(path, frame, frame_virt, result);
    (void)snprintf(path, sizeof(path), "%s.json", base);
    status |= vision_debug_write_json(path, summary, result);

    if (status == TD_SUCCESS) {
        ctx->saved_frames++;
    } else {
        fprintf(stderr, "vision: failed to save snapshot %s\n", base);
    }
}

td_void vision_debug_publish(vision_debug_context *ctx,
    const ot_video_frame_info *frame, const td_u8 *frame_virt,
    const sample_svp_frame_result *result, td_s32 infer_ret,
    td_double inference_ms)
{
    td_char summary[VISION_JSON_BUFFER_SIZE];
    td_s32 length;
    td_u64 frame_id;

    if (ctx == TD_NULL || frame == TD_NULL || frame_virt == TD_NULL || result == TD_NULL) {
        return;
    }
    frame_id = ++ctx->frame_seq;
    length = vision_debug_format_summary(summary, sizeof(summary), frame_id,
        result, infer_ret, inference_ms);
    if (length < 0) {
        return;
    }
    vision_debug_send_telemetry(ctx, summary, (size_t)length);

    if (ctx->snapshots_enabled == TD_TRUE &&
        frame_id % ctx->snapshot_every == 0 &&
        (ctx->snapshot_limit == 0 || ctx->saved_frames < ctx->snapshot_limit)) {
        vision_debug_save_snapshot(ctx, frame_id, frame, frame_virt, result, summary);
    }
}
