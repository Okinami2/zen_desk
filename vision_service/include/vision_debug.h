#ifndef VISION_DEBUG_H
#define VISION_DEBUG_H

#include <netinet/in.h>
#include <sys/socket.h>

#include "npu_process.h"
#include "vision_service.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VISION_DEBUG_PATH_MAX 256

typedef struct {
    td_s32 telemetry_fd;
    struct sockaddr_storage telemetry_addr;
    socklen_t telemetry_addr_len;
    td_bool telemetry_enabled;
    td_bool snapshots_enabled;
    td_char snapshot_dir[VISION_DEBUG_PATH_MAX];
    td_u32 snapshot_every;
    td_u32 snapshot_limit;
    td_u64 frame_seq;
    td_u64 saved_frames;
} vision_debug_context;

td_s32 vision_debug_init(vision_debug_context *ctx,
    const vision_service_config *config);
td_void vision_debug_deinit(vision_debug_context *ctx);
td_void vision_debug_publish(vision_debug_context *ctx,
    const ot_video_frame_info *frame, const td_u8 *frame_virt,
    const sample_svp_frame_result *result, td_s32 infer_ret,
    td_double inference_ms);

#ifdef __cplusplus
}
#endif

#endif
