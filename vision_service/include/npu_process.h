/*
 * Copyright (c) 2025 HiSilicon (Shanghai) Technologies Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef NPU_PROCESS_H
#define NPU_PROCESS_H

#include "ot_type.h"
#include "ot_common_video.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

#define SAMPLE_SVP_LANDMARK_NUM 106

typedef struct {
    td_float x1;
    td_float y1;
    td_float x2;
    td_float y2;
    td_float score;
} sample_svp_face_box;

typedef struct {
    td_u32 point_num;
    td_float points[SAMPLE_SVP_LANDMARK_NUM][2];
} sample_svp_landmark106_result;

typedef struct {
    td_float yaw_deg;
    td_float pitch_deg;
    td_float roll_deg;
} sample_svp_gaze_result;

typedef struct {
    td_u32 blink_count;
    td_u32 closed_frames;
    td_bool eyes_closed;

    td_u32 yawn_count;
    td_bool yawning;
    td_double yawn_start_time;
    td_bool yawn_counted;

    td_float smooth_yaw;
    td_float smooth_pitch;
    td_float smooth_eye_open;
    td_float smooth_mouth_open;
} sample_svp_face_state;

typedef struct {
    td_bool has_face;
    sample_svp_face_box face;
    sample_svp_landmark106_result landmark;
    sample_svp_gaze_result gaze;
    sample_svp_face_state state_snapshot;
} sample_svp_frame_result;

/* abnormal termination signal */
td_void sample_svp_npu_acl_handle_sig(td_void);

/* 原来的离线入口保留 */
td_void sample_svp_npu_acl_offline_pipeline(td_void);
td_void sample_svp_npu_acl_offline_smoke_test(td_void);

/* 新增：对一帧 RGB888 执行最小三模型 pipeline */
td_s32 sample_svp_npu_run_frame_pipeline_rgb888(
    const td_u8 *rgb,
    td_u32 width,
    td_u32 height,
    sample_svp_frame_result *result);

/* 获取内部状态，便于 main 里打印调试 */
const sample_svp_face_state *sample_svp_npu_get_face_state(td_void);
td_s32 sample_svp_npu_init_runtime(td_void);
td_void sample_svp_npu_deinit_runtime(td_void);
td_s32 sample_svp_npu_process_frame(const ot_video_frame_info *frame,
    const td_u8 *frame_virt, sample_svp_frame_result *result);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif /* NPU_PROCESS_H */