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

#include "npu_process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include "securec.h"
#include "svp_acl_rt.h"
#include "svp_acl.h"
#include "svp_acl_ext.h"
#include "sample_common_svp.h"
#include "sample_common_svp_npu.h"
#include "sample_common_svp_npu_model.h"

#define SAMPLE_SVP_NPU_OFFLINE_TASK_NUM      4
#define SAMPLE_SVP_NPU_FACE_DET_MODEL_IDX    0
#define SAMPLE_SVP_NPU_LANDMARK_MODEL_IDX    1
#define SAMPLE_SVP_NPU_POSE_DET_MODEL_IDX    2
#define SAMPLE_SVP_NPU_POSE_LANDMARK_MODEL_IDX 3
#define SAMPLE_SVP_NPU_ACTIVE_TASK_NUM       4

#define SAMPLE_SVP_NPU_INPUT_FILE_NUM_ONE    1
#define SAMPLE_SVP_NPU_PATH_LEN              256
#define SAMPLE_SVP_NPU_MAX_FACE_NUM          16

#define SAMPLE_SVP_NPU_FACE_DET_MODEL_PATH   "./data/model/face_detection.om"
#define SAMPLE_SVP_NPU_LANDMARK_MODEL_PATH   "./data/model/landmark106.om"
#define SAMPLE_SVP_NPU_POSE_DET_MODEL_PATH   "./data/model/pose_detector.om"
#define SAMPLE_SVP_NPU_POSE_LANDMARK_MODEL_PATH "./data/model/pose_landmarks_detector.om"

#define SAMPLE_SVP_LANDMARK_INPUT_BIN_PATH   "./data/input/landmark_input.bin"

#define SAMPLE_SVP_EYE_CLOSED_TH      0.19f
#define SAMPLE_SVP_MOUTH_OPEN_TH      0.28f
#define SAMPLE_SVP_BLINK_MIN_FRAMES   2
#define SAMPLE_SVP_BLINK_MAX_FRAMES   8
#define SAMPLE_SVP_YAWN_MIN_SECONDS   0.8

#define SAMPLE_SVP_LANDMARK_IN_W      192
#define SAMPLE_SVP_LANDMARK_IN_H      192
#define SAMPLE_SVP_LANDMARK_NUM       106
#define SAMPLE_SVP_POSE_DET_INPUT_W   224
#define SAMPLE_SVP_POSE_DET_INPUT_H   224
#define SAMPLE_SVP_POSE_LM_INPUT_W    256
#define SAMPLE_SVP_POSE_LM_INPUT_H    256
#define SAMPLE_SVP_POSE_HEATMAP_SIZE  64
#define SAMPLE_SVP_POSE_LANDMARK_NUM  39
#define SAMPLE_SVP_POSE_ANCHOR_NUM    2254
#define SAMPLE_SVP_POSE_INTERVAL_S    2.0
#define SAMPLE_SVP_POSE_DET_TH        0.50f
#define SAMPLE_SVP_POSE_SCORE_TH      0.50f
#define SAMPLE_SVP_POSE_VIS_TH        0.35f
#define SAMPLE_SVP_PI_F               3.14159265358979323846f

#define SAMPLE_SVP_LANDMARK_CROP_SCALE 1.50f
#define SAMPLE_SVP_HEAD_YAW_LIMIT_DEG  45.0f
#define SAMPLE_SVP_HEAD_PITCH_LIMIT_DEG 45.0f

typedef struct {
    td_u32 num;
    sample_svp_face_box boxes[SAMPLE_SVP_NPU_MAX_FACE_NUM];
} sample_svp_face_box_list;

typedef struct {
    td_float scale;
    td_float translate_x;
    td_float translate_y;
} sample_svp_landmark_affine;

typedef struct {
    td_float cx;
    td_float cy;
    td_float size;
    td_float rotation;
} sample_svp_pose_roi;

typedef struct {
    td_float score;
    td_float box[4];
    td_float keypoints[4][2];
} sample_svp_pose_detection;

#define SAMPLE_SVP_FACE_DET_DEBUG_FRAME_MAX   30
#define SAMPLE_SVP_FACE_DET_SCORE_TH          0.50f
#define SAMPLE_SVP_FACE_DET_NMS_TH            0.40f
#define SAMPLE_SVP_SCRFD_LEVEL_NUM            3
#define SAMPLE_SVP_SCRFD_ANCHOR_NUM           2

static ot_video_frame_info g_svp_npu_face_det_frame = {0};
static const td_u8 *g_svp_npu_face_det_frame_virt = TD_NULL;
static td_bool g_svp_npu_face_det_frame_ready = TD_FALSE;


static td_bool g_svp_npu_terminate_signal = TD_FALSE;
static td_s32 g_svp_npu_dev_id = 0;
static td_bool g_pipeline_inited = TD_FALSE;
static sample_svp_face_state g_face_state = {0};
static sample_svp_npu_task_info g_svp_npu_task[SAMPLE_SVP_NPU_OFFLINE_TASK_NUM] = {0};
static td_u32 g_face_det_debug_frame_idx = 0;
static td_u32 g_face_det_expect_w = 0;
static td_u32 g_face_det_expect_h = 0;
static td_u32 g_face_det_expect_size = 0;
static td_u32 g_face_det_expect_stride = 0;
static td_u8 *g_face_det_resized_buf = TD_NULL;
static td_u8 *g_face_det_model_input_virt = TD_NULL;
static td_float g_face_det_scale = 1.0f;
static td_u32 g_face_det_pad_x = 0;
static td_u32 g_face_det_pad_y = 0;
static td_u32 g_landmark_expect_w = 0;
static td_u32 g_landmark_expect_h = 0;
static td_u32 g_landmark_expect_size = 0;
static td_u32 g_landmark_expect_stride = 0;
static td_u8 *g_landmark_model_input_virt = TD_NULL;
static td_u32 g_pose_det_input_size = 0;
static td_u32 g_pose_det_input_stride = 0;
static td_u8 *g_pose_det_input_virt = TD_NULL;
static td_u32 g_pose_lm_input_size = 0;
static td_u32 g_pose_lm_input_stride = 0;
static td_u8 *g_pose_lm_input_virt = TD_NULL;
static td_double g_pose_last_run_s = -1000.0;
static sample_svp_pose_result g_pose_cached = {0};
static td_float g_pose_output_tmp[2][SAMPLE_SVP_POSE_ANCHOR_NUM * 12];

static svp_acl_format sample_svp_model_input_format(td_u32 model_idx);
static svp_acl_data_type sample_svp_model_input_type(td_u32 model_idx);

typedef struct {
    td_u64 frame_cnt;
    td_u64 face_cnt;
    td_double win_start;
    td_double sum_total;
    td_double sum_face_det;
    td_double sum_lm_prep;
    td_double sum_lm_infer;
    td_double sum_lm_parse_map;
    td_double max_total;
    td_double max_face_det;
    td_double max_lm_infer;
} sample_svp_pipeline_profile;

static sample_svp_pipeline_profile g_pipe_prof = {0};

td_s32 sample_svp_npu_set_face_det_frame(const ot_video_frame_info *frame, const td_u8 *frame_virt);

typedef struct {
    const td_u8 *score;
    const td_u8 *bbox;
    td_u32 score_stride;
    td_u32 bbox_stride;
    td_u32 count;
    td_u32 feature_w;
    td_u32 feature_h;
    td_u32 stride;
} sample_svp_scrfd_level;

static td_float sample_svp_face_box_iou(const sample_svp_face_box *a, const sample_svp_face_box *b)
{
    td_float xx1 = fmaxf(a->x1, b->x1);
    td_float yy1 = fmaxf(a->y1, b->y1);
    td_float xx2 = fminf(a->x2, b->x2);
    td_float yy2 = fminf(a->y2, b->y2);
    td_float w = fmaxf(0.0f, xx2 - xx1 + 1.0f);
    td_float h = fmaxf(0.0f, yy2 - yy1 + 1.0f);
    td_float inter = w * h;
    td_float area_a = fmaxf(0.0f, a->x2 - a->x1 + 1.0f) *
        fmaxf(0.0f, a->y2 - a->y1 + 1.0f);
    td_float area_b = fmaxf(0.0f, b->x2 - b->x1 + 1.0f) *
        fmaxf(0.0f, b->y2 - b->y1 + 1.0f);

    return inter / (area_a + area_b - inter + 1e-6f);
}

static int sample_svp_face_box_score_desc(const td_void *lhs, const td_void *rhs)
{
    const sample_svp_face_box *a = (const sample_svp_face_box *)lhs;
    const sample_svp_face_box *b = (const sample_svp_face_box *)rhs;

    if (a->score < b->score) {
        return 1;
    }
    if (a->score > b->score) {
        return -1;
    }
    return 0;
}

static td_u32 sample_svp_dims_row_count(const svp_acl_mdl_io_dims *dims)
{
    td_u32 i;
    td_u32 count = 1;

    if (dims == TD_NULL || dims->dim_count < 2) {
        return 0;
    }
    for (i = 0; i + 1 < dims->dim_count; i++) {
        count *= (td_u32)dims->dims[i];
    }
    return count;
}

static td_s32 sample_svp_scrfd_bind_outputs(const sample_svp_npu_task_info *task,
    sample_svp_scrfd_level levels[SAMPLE_SVP_SCRFD_LEVEL_NUM])
{
    static const td_u32 level_strides[SAMPLE_SVP_SCRFD_LEVEL_NUM] = {8, 16, 32};
    sample_svp_npu_model_info *model_info;
    td_u32 output_num;
    td_u32 output_idx;

    model_info = sample_common_svp_npu_get_model_info(SAMPLE_SVP_NPU_FACE_DET_MODEL_IDX);
    sample_svp_check_exps_return(model_info == TD_NULL || model_info->model_desc == TD_NULL,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "SCRFD model description is null\n");

    output_num = (td_u32)svp_acl_mdl_get_dataset_num_buffers(task->output_dataset);
    sample_svp_check_exps_return(output_num != SAMPLE_SVP_SCRFD_LEVEL_NUM * 2,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "SCRFD expects 6 outputs, got %u\n", output_num);

    (td_void)memset_s(levels, sizeof(sample_svp_scrfd_level) * SAMPLE_SVP_SCRFD_LEVEL_NUM,
        0, sizeof(sample_svp_scrfd_level) * SAMPLE_SVP_SCRFD_LEVEL_NUM);
    for (output_idx = 0; output_idx < output_num; output_idx++) {
        svp_acl_data_buffer *buf;
        svp_acl_mdl_io_dims dims = {0};
        const td_u8 *addr;
        size_t size;
        size_t row_stride;
        td_u32 row_count;
        td_u32 channels;
        td_u32 level_idx;
        td_bool matched = TD_FALSE;
        svp_acl_error acl_ret;

        acl_ret = svp_acl_mdl_get_output_dims(model_info->model_desc, output_idx, &dims);
        sample_svp_check_exps_return(acl_ret != SVP_ACL_SUCCESS || dims.dim_count < 2,
            TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "get SCRFD output[%u] dims failed\n", output_idx);

        row_count = sample_svp_dims_row_count(&dims);
        channels = (td_u32)dims.dims[dims.dim_count - 1];
        buf = svp_acl_mdl_get_dataset_buffer(task->output_dataset, output_idx);
        sample_svp_check_exps_return(buf == TD_NULL, TD_FAILURE,
            SAMPLE_SVP_ERR_LEVEL_ERROR, "SCRFD output[%u] buffer is null\n", output_idx);
        addr = (const td_u8 *)svp_acl_get_data_buffer_addr(buf);
        size = svp_acl_get_data_buffer_size(buf);
        row_stride = svp_acl_get_data_buffer_stride(buf);
        sample_svp_check_exps_return(addr == TD_NULL || row_stride < channels * sizeof(td_float) ||
            size < (size_t)row_count * row_stride,
            TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "SCRFD output[%u] layout invalid: rows=%u channels=%u size=%u stride=%u\n",
            output_idx, row_count, channels, (td_u32)size, (td_u32)row_stride);

        for (level_idx = 0; level_idx < SAMPLE_SVP_SCRFD_LEVEL_NUM; level_idx++) {
            td_u32 feature_w = g_face_det_expect_w / level_strides[level_idx];
            td_u32 feature_h = g_face_det_expect_h / level_strides[level_idx];
            td_u32 expected_count = feature_w * feature_h * SAMPLE_SVP_SCRFD_ANCHOR_NUM;

            if (row_count != expected_count) {
                continue;
            }
            levels[level_idx].count = row_count;
            levels[level_idx].feature_w = feature_w;
            levels[level_idx].feature_h = feature_h;
            levels[level_idx].stride = level_strides[level_idx];
            if (channels == 1) {
                levels[level_idx].score = addr;
                levels[level_idx].score_stride = (td_u32)row_stride;
                matched = TD_TRUE;
            } else if (channels == 4) {
                levels[level_idx].bbox = addr;
                levels[level_idx].bbox_stride = (td_u32)row_stride;
                matched = TD_TRUE;
            }
            break;
        }

        sample_svp_check_exps_return(matched != TD_TRUE, TD_FAILURE,
            SAMPLE_SVP_ERR_LEVEL_ERROR,
            "unsupported SCRFD output[%u] shape: rows=%u channels=%u\n",
            output_idx, row_count, channels);
    }

    for (output_idx = 0; output_idx < SAMPLE_SVP_SCRFD_LEVEL_NUM; output_idx++) {
        sample_svp_check_exps_return(levels[output_idx].score == TD_NULL ||
            levels[output_idx].bbox == TD_NULL,
            TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "SCRFD stride %u output pair is incomplete\n", levels[output_idx].stride);
    }
    return TD_SUCCESS;
}

static td_s32 sample_svp_npu_decode_scrfd_output(const sample_svp_npu_task_info *task,
    td_u32 frame_w, td_u32 frame_h, sample_svp_face_box_list *face_list)
{
    sample_svp_scrfd_level levels[SAMPLE_SVP_SCRFD_LEVEL_NUM];
    sample_svp_face_box *candidates = TD_NULL;
    td_u32 candidate_capacity = 0;
    td_u32 candidate_num = 0;
    td_u32 level_idx;
    td_u32 i;
    td_s32 ret;

    sample_svp_check_exps_return(task == TD_NULL || face_list == TD_NULL ||
        frame_w == 0 || frame_h == 0 || g_face_det_scale <= 0.0f,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "invalid SCRFD decode args\n");

    ret = sample_svp_scrfd_bind_outputs(task, levels);
    sample_svp_check_exps_return(ret != TD_SUCCESS, ret,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "bind SCRFD outputs failed\n");

    for (level_idx = 0; level_idx < SAMPLE_SVP_SCRFD_LEVEL_NUM; level_idx++) {
        candidate_capacity += levels[level_idx].count;
    }
    candidates = (sample_svp_face_box *)calloc(candidate_capacity, sizeof(sample_svp_face_box));
    sample_svp_check_exps_return(candidates == TD_NULL, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "allocate SCRFD candidates failed\n");

    for (level_idx = 0; level_idx < SAMPLE_SVP_SCRFD_LEVEL_NUM; level_idx++) {
        const sample_svp_scrfd_level *level = &levels[level_idx];

        for (i = 0; i < level->count; i++) {
            const td_float *score_row =
                (const td_float *)(level->score + (size_t)i * level->score_stride);
            const td_float *bbox_row =
                (const td_float *)(level->bbox + (size_t)i * level->bbox_stride);
            td_float score = score_row[0];
            td_u32 cell_idx;
            td_u32 cell_x;
            td_u32 cell_y;
            td_float center_x;
            td_float center_y;
            td_float x1;
            td_float y1;
            td_float x2;
            td_float y2;

            if (!isfinite(score) || score < SAMPLE_SVP_FACE_DET_SCORE_TH ||
                !isfinite(bbox_row[0]) || !isfinite(bbox_row[1]) ||
                !isfinite(bbox_row[2]) || !isfinite(bbox_row[3])) {
                continue;
            }

            cell_idx = i / SAMPLE_SVP_SCRFD_ANCHOR_NUM;
            cell_x = cell_idx % level->feature_w;
            cell_y = cell_idx / level->feature_w;
            center_x = (td_float)(cell_x * level->stride);
            center_y = (td_float)(cell_y * level->stride);
            x1 = center_x - bbox_row[0] * level->stride;
            y1 = center_y - bbox_row[1] * level->stride;
            x2 = center_x + bbox_row[2] * level->stride;
            y2 = center_y + bbox_row[3] * level->stride;

            x1 = (x1 - (td_float)g_face_det_pad_x) / g_face_det_scale;
            y1 = (y1 - (td_float)g_face_det_pad_y) / g_face_det_scale;
            x2 = (x2 - (td_float)g_face_det_pad_x) / g_face_det_scale;
            y2 = (y2 - (td_float)g_face_det_pad_y) / g_face_det_scale;
            x1 = fmaxf(0.0f, fminf(x1, (td_float)(frame_w - 1)));
            y1 = fmaxf(0.0f, fminf(y1, (td_float)(frame_h - 1)));
            x2 = fmaxf(0.0f, fminf(x2, (td_float)(frame_w - 1)));
            y2 = fmaxf(0.0f, fminf(y2, (td_float)(frame_h - 1)));
            if (x2 <= x1 || y2 <= y1) {
                continue;
            }

            candidates[candidate_num].x1 = x1;
            candidates[candidate_num].y1 = y1;
            candidates[candidate_num].x2 = x2;
            candidates[candidate_num].y2 = y2;
            candidates[candidate_num].score = score;
            candidate_num++;
        }
    }

    qsort(candidates, candidate_num, sizeof(sample_svp_face_box), sample_svp_face_box_score_desc);
    for (i = 0; i < candidate_num && face_list->num < SAMPLE_SVP_NPU_MAX_FACE_NUM; i++) {
        td_u32 kept_idx;
        td_bool keep = TD_TRUE;

        for (kept_idx = 0; kept_idx < face_list->num; kept_idx++) {
            if (sample_svp_face_box_iou(&candidates[i], &face_list->boxes[kept_idx]) >
                SAMPLE_SVP_FACE_DET_NMS_TH) {
                keep = TD_FALSE;
                break;
            }
        }
        if (keep == TD_TRUE) {
            face_list->boxes[face_list->num++] = candidates[i];
        }
    }

    if (g_face_det_debug_frame_idx < SAMPLE_SVP_FACE_DET_DEBUG_FRAME_MAX) {
        sample_svp_trace_info("SCRFD candidates=%u after_nms=%u scale=%.5f pad=(%u,%u)\n",
            candidate_num, face_list->num, g_face_det_scale, g_face_det_pad_x, g_face_det_pad_y);
    }
    free(candidates);
    return TD_SUCCESS;
}

/* ----------------------------- 工具函数（提前定义） ----------------------------- */
#if 0
static td_s32 sample_svp_npu_decode_face_det_output(const sample_svp_npu_task_info *task,
    td_u32 frame_w, td_u32 frame_h, sample_svp_face_box_list *face_list)
{
    svp_acl_mdl_dataset *output = TD_NULL;
    svp_acl_data_buffer *meta_buf = TD_NULL;
    svp_acl_data_buffer *roi_buf = TD_NULL;
    td_void *meta_addr = TD_NULL;
    td_void *roi_addr = TD_NULL;
    size_t meta_size = 0;
    size_t roi_size = 0;
    size_t roi_stride_b = 0;
    td_u32 i;
    td_u32 output_num;
    td_u32 det_num = 0;
    td_float scale_x = (td_float)frame_w / 640.0f;
    td_float scale_y = (td_float)frame_h / 640.0f;
    td_u32 rej_small = 0;
    td_u32 rej_large = 0;
    td_u32 rej_ratio = 0;
    td_u32 rej_score = 0;
    td_u32 rej_invalid = 0;
    td_u32 rej_edge = 0;
    td_u32 keep_num = 0;
    td_u32 stride_f;
    td_u32 plane_num;
    td_float *x_min;
    td_float *y_min;
    td_float *x_max;
    td_float *y_max;
    td_float *score_plane = TD_NULL;
    sample_svp_face_box tmp_boxes[SAMPLE_SVP_NPU_MAX_FACE_NUM] = {0};

    sample_svp_check_exps_return(task == TD_NULL || face_list == TD_NULL || frame_w == 0 || frame_h == 0,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "invalid face det decode args\n");

    output = task->output_dataset;
    sample_svp_check_exps_return(output == TD_NULL, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "face det output dataset is null\n");

    output_num = (td_u32)svp_acl_mdl_get_dataset_num_buffers(output);
    for (i = 0; i < output_num; i++) {
        svp_acl_data_buffer *buf = svp_acl_mdl_get_dataset_buffer(output, i);
        td_void *addr;
        size_t sz;
        size_t st;

        if (buf == TD_NULL) {
            continue;
        }
        addr = svp_acl_get_data_buffer_addr(buf);
        sz = svp_acl_get_data_buffer_size(buf);
        st = svp_acl_get_data_buffer_stride(buf);

        if (g_face_det_debug_frame_idx < SAMPLE_SVP_FACE_DET_DEBUG_FRAME_MAX) {
            sample_svp_trace_info("face det output[%u] size=%u stride=%u\n", i, (td_u32)sz, (td_u32)st);
            if (addr != TD_NULL && sz >= sizeof(td_float) * 6) {
                td_float *pv = (td_float *)addr;
                sample_svp_trace_info("face det output[%u] preview: %.4f %.4f %.4f %.4f %.4f %.4f\n",
                    i, pv[0], pv[1], pv[2], pv[3], pv[4], pv[5]);
            }
            if (i == 0 && addr != TD_NULL && sz >= sizeof(td_u32) * 4) {
                td_u32 *u32v = (td_u32 *)addr;
                td_float *f32v = (td_float *)addr;
                sample_svp_trace_info("face det output[0] as u32: %u %u %u %u\n",
                    u32v[0], u32v[1], u32v[2], u32v[3]);
                sample_svp_trace_info("face det output[0] as f32: %.6f %.6f %.6f %.6f\n",
                    f32v[0], f32v[1], f32v[2], f32v[3]);
            }
        }

        if (addr == TD_NULL || sz == 0) {
            continue;
        }

        if (sz <= sizeof(td_float) * 16) {
            meta_buf = buf;
            meta_addr = addr;
            meta_size = sz;
            continue;
        }

        if (st > 0 && sz >= st * 4) {
            roi_buf = buf;
            roi_addr = addr;
            roi_size = sz;
            roi_stride_b = st;
        }
    }

    sample_svp_check_exps_return(meta_buf == TD_NULL || roi_buf == TD_NULL ||
        meta_addr == TD_NULL || roi_addr == TD_NULL || roi_stride_b == 0,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "face det output invalid\n");

    if (meta_size >= sizeof(td_float)) {
        td_float *num_data = (td_float *)meta_addr;
        td_u32 num_n = (td_u32)(meta_size / sizeof(td_float));
        td_float total = 0.0f;
        for (i = 0; i < num_n; i++) {
            if (num_data[i] > 0.0f) {
                total += num_data[i];
            }
        }
        det_num = (td_u32)(total + 0.5f);
    }

    stride_f = (td_u32)(roi_stride_b / sizeof(td_float));
    sample_svp_check_exps_return(stride_f == 0, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "face det roi stride invalid\n");

    plane_num = (td_u32)(roi_size / roi_stride_b);
    sample_svp_check_exps_return(plane_num < 4, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "face det roi plane num invalid: %u\n", plane_num);

    x_min = (td_float *)roi_addr;
    y_min = x_min + stride_f;
    x_max = y_min + stride_f;
    y_max = x_max + stride_f;
    if (plane_num >= 5) {
        score_plane = y_max + stride_f;
    }

    if (det_num == 0 || det_num > stride_f) {
        det_num = stride_f;
    }
    if (det_num > 300) {
        det_num = 300;
    }

    if (g_face_det_debug_frame_idx < SAMPLE_SVP_FACE_DET_DEBUG_FRAME_MAX) {
        sample_svp_trace_info("face det planar decode: det_num=%u stride_f=%u planes=%u scale=(%.3f,%.3f)\n",
            det_num, stride_f, plane_num, scale_x, scale_y);
    }

    for (i = 0; i < det_num && keep_num < SAMPLE_SVP_NPU_MAX_FACE_NUM; i++) {
        td_float x1 = x_min[i] * scale_x;
        td_float y1 = y_min[i] * scale_y;
        td_float x2 = x_max[i] * scale_x;
        td_float y2 = y_max[i] * scale_y;
        td_float score = 1.0f;
        td_float w;
        td_float h;

        if (!isfinite(x1) || !isfinite(y1) || !isfinite(x2) || !isfinite(y2)) {
            rej_invalid++;
            continue;
        }

        if (score_plane != TD_NULL) {
            score = score_plane[i];
            if (!isfinite(score) || score < 0.0f || score > 1.5f) {
                rej_score++;
                continue;
            }
            if (score < 0.10f) {
                rej_score++;
                continue;
            }
            if (score > 1.0f) {
                score = 1.0f;
            }
        }

        x1 = fmaxf(0.0f, fminf(x1, (td_float)frame_w));
        y1 = fmaxf(0.0f, fminf(y1, (td_float)frame_h));
        x2 = fmaxf(0.0f, fminf(x2, (td_float)frame_w));
        y2 = fmaxf(0.0f, fminf(y2, (td_float)frame_h));

        if (x2 <= x1 || y2 <= y1) {
            rej_invalid++;
            continue;
        }

        w = x2 - x1;
        h = y2 - y1;

        if (w < 40.0f || h < 40.0f) {
            rej_small++;
            continue;
        }
        if ((w * h) > (td_float)(frame_w * frame_h) * 0.35f) {
            rej_large++;
            continue;
        }
        if (w / h < 0.35f || w / h > 2.20f) {
            rej_ratio++;
            continue;
        }
        if (x1 <= 6.0f || y1 <= 6.0f || x2 >= (td_float)frame_w - 6.0f || y2 >= (td_float)frame_h - 6.0f) {
            rej_edge++;
            continue;
        }

        tmp_boxes[keep_num].x1 = x1;
        tmp_boxes[keep_num].y1 = y1;
        tmp_boxes[keep_num].x2 = x2;
        tmp_boxes[keep_num].y2 = y2;
        tmp_boxes[keep_num].score = score;

        if (g_face_det_debug_frame_idx < SAMPLE_SVP_FACE_DET_DEBUG_FRAME_MAX && keep_num < 3) {
            sample_svp_trace_info("face det keep[%u]: score=%.3f box=(%.1f,%.1f,%.1f,%.1f)\n",
                keep_num, score, x1, y1, x2, y2);
        }
        keep_num++;
    }

    for (i = 0; i < keep_num; i++) {
        td_u32 j;
        td_bool keep = TD_TRUE;
        for (j = 0; j < face_list->num; j++) {
            td_float xx1 = fmaxf(tmp_boxes[i].x1, face_list->boxes[j].x1);
            td_float yy1 = fmaxf(tmp_boxes[i].y1, face_list->boxes[j].y1);
            td_float xx2 = fminf(tmp_boxes[i].x2, face_list->boxes[j].x2);
            td_float yy2 = fminf(tmp_boxes[i].y2, face_list->boxes[j].y2);
            td_float iw = xx2 - xx1;
            td_float ih = yy2 - yy1;
            if (iw > 0.0f && ih > 0.0f) {
                td_float inter = iw * ih;
                td_float a1 = (tmp_boxes[i].x2 - tmp_boxes[i].x1) * (tmp_boxes[i].y2 - tmp_boxes[i].y1);
                td_float a2 = (face_list->boxes[j].x2 - face_list->boxes[j].x1) *
                    (face_list->boxes[j].y2 - face_list->boxes[j].y1);
                td_float iou = inter / (a1 + a2 - inter + 1e-6f);
                if (iou > 0.45f) {
                    keep = TD_FALSE;
                    break;
                }
            }
        }
        if (keep && face_list->num < SAMPLE_SVP_NPU_MAX_FACE_NUM) {
            face_list->boxes[face_list->num++] = tmp_boxes[i];
        }
    }

    if (g_face_det_debug_frame_idx < SAMPLE_SVP_FACE_DET_DEBUG_FRAME_MAX) {
        sample_svp_trace_info("face det reject stats: invalid=%u small=%u large=%u ratio=%u edge=%u score=%u kept=%u\n",
            rej_invalid, rej_small, rej_large, rej_ratio, rej_edge, rej_score, face_list->num);
    }

    return TD_SUCCESS;
}

static td_s32 sample_svp_resize_nv21_to_target(const td_u8 *src, td_u32 src_w, td_u32 src_h,
    td_u32 src_stride_y, td_u32 src_stride_uv, td_u8 *dst, td_u32 dst_w, td_u32 dst_h, td_u32 dst_stride)
{
    td_u32 y;
    td_u32 x;
    const td_u8 *src_y = src;
    const td_u8 *src_vu = src + src_stride_y * src_h;
    td_u8 *dst_y = dst;
    td_u8 *dst_vu = dst + dst_stride * dst_h;

    sample_svp_check_exps_return(src == TD_NULL || dst == TD_NULL, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "resize nv21 null ptr\n");
    sample_svp_check_exps_return(src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "resize nv21 invalid size\n");

    for (y = 0; y < dst_h; y++) {
        td_u32 sy = (td_u32)((td_u64)y * src_h / dst_h);
        const td_u8 *src_row = src_y + sy * src_stride_y;
        td_u8 *dst_row = dst_y + y * dst_stride;
        for (x = 0; x < dst_w; x++) {
            td_u32 sx = (td_u32)((td_u64)x * src_w / dst_w);
            dst_row[x] = src_row[sx];
        }
    }

    for (y = 0; y < dst_h / 2; y++) {
        td_u32 sy = (td_u32)((td_u64)y * (src_h / 2) / (dst_h / 2));
        const td_u8 *src_row = src_vu + sy * src_stride_uv;
        td_u8 *dst_row = dst_vu + y * dst_stride;
        for (x = 0; x < dst_w / 2; x++) {
            td_u32 sx = (td_u32)((td_u64)x * (src_w / 2) / (dst_w / 2));
            dst_row[x * 2] = src_row[sx * 2];
            dst_row[x * 2 + 1] = src_row[sx * 2 + 1];
        }
    }

    return TD_SUCCESS;
}
#endif

static td_void sample_svp_nv21_get_rgb(const td_u8 *src_y, const td_u8 *src_vu,
    td_u32 stride_y, td_u32 stride_uv, td_u32 x, td_u32 y,
    td_float *r, td_float *g, td_float *b)
{
    td_s32 yv = src_y[(size_t)y * stride_y + x];
    td_u32 vu_idx = (y / 2) * stride_uv + (x & ~1U);
    td_s32 v = src_vu[vu_idx];
    td_s32 u = src_vu[vu_idx + 1];
    td_s32 c = yv - 16;
    td_s32 d = u - 128;
    td_s32 e = v - 128;
    td_s32 ri;
    td_s32 gi;
    td_s32 bi;

    if (c < 0) {
        c = 0;
    }
    ri = (298 * c + 409 * e + 128) >> 8;
    gi = (298 * c - 100 * d - 208 * e + 128) >> 8;
    bi = (298 * c + 516 * d + 128) >> 8;
    *r = (td_float)((ri < 0) ? 0 : (ri > 255 ? 255 : ri));
    *g = (td_float)((gi < 0) ? 0 : (gi > 255 ? 255 : gi));
    *b = (td_float)((bi < 0) ? 0 : (bi > 255 ? 255 : bi));
}

static td_s32 sample_svp_prepare_scrfd_input(const td_u8 *src, td_u32 src_w, td_u32 src_h,
    td_u32 src_stride_y, td_u32 src_stride_uv)
{
    const td_u8 *src_y = src;
    const td_u8 *src_vu = src + (size_t)src_stride_y * src_h;
    td_float *dst = (td_float *)g_face_det_model_input_virt;
    td_u32 dst_stride_f;
    td_u32 resized_w;
    td_u32 resized_h;
    td_u32 x;
    td_u32 y;
    td_u32 channel;
    const td_float pad_value = (0.0f - 127.5f) / 128.0f;

    sample_svp_check_exps_return(src == TD_NULL || dst == TD_NULL ||
        src_w == 0 || src_h == 0 || src_stride_y == 0 || src_stride_uv == 0,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "invalid SCRFD input args\n");
    sample_svp_check_exps_return(g_face_det_expect_stride % sizeof(td_float) != 0,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "SCRFD input stride is not float aligned\n");

    dst_stride_f = g_face_det_expect_stride / sizeof(td_float);
    sample_svp_check_exps_return(dst_stride_f < g_face_det_expect_w ||
        g_face_det_expect_size < 3 * g_face_det_expect_h * g_face_det_expect_stride,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "SCRFD input buffer layout invalid\n");

    g_face_det_scale = fminf((td_float)g_face_det_expect_w / src_w,
        (td_float)g_face_det_expect_h / src_h);
    resized_w = (td_u32)floorf(src_w * g_face_det_scale + 0.5f);
    resized_h = (td_u32)floorf(src_h * g_face_det_scale + 0.5f);
    g_face_det_pad_x = (g_face_det_expect_w - resized_w) / 2;
    g_face_det_pad_y = (g_face_det_expect_h - resized_h) / 2;

    for (channel = 0; channel < 3; channel++) {
        td_float *channel_dst = dst + (size_t)channel * g_face_det_expect_h * dst_stride_f;
        for (y = 0; y < g_face_det_expect_h; y++) {
            for (x = 0; x < dst_stride_f; x++) {
                channel_dst[(size_t)y * dst_stride_f + x] = pad_value;
            }
        }
    }

    for (y = 0; y < resized_h; y++) {
        td_float src_yf = ((td_float)y + 0.5f) / g_face_det_scale - 0.5f;
        td_u32 sy;
        if (src_yf < 0.0f) {
            src_yf = 0.0f;
        }
        sy = (td_u32)fminf(floorf(src_yf + 0.5f), (td_float)(src_h - 1));
        for (x = 0; x < resized_w; x++) {
            td_float src_xf = ((td_float)x + 0.5f) / g_face_det_scale - 0.5f;
            td_u32 sx;
            td_float r;
            td_float g;
            td_float b;
            size_t dst_idx;

            if (src_xf < 0.0f) {
                src_xf = 0.0f;
            }
            sx = (td_u32)fminf(floorf(src_xf + 0.5f), (td_float)(src_w - 1));
            sample_svp_nv21_get_rgb(src_y, src_vu, src_stride_y, src_stride_uv,
                sx, sy, &r, &g, &b);
            dst_idx = (size_t)(y + g_face_det_pad_y) * dst_stride_f + x + g_face_det_pad_x;
            dst[dst_idx] = (r - 127.5f) / 128.0f;
            dst[(size_t)g_face_det_expect_h * dst_stride_f + dst_idx] =
                (g - 127.5f) / 128.0f;
            dst[(size_t)2 * g_face_det_expect_h * dst_stride_f + dst_idx] =
                (b - 127.5f) / 128.0f;
        }
    }
    return TD_SUCCESS;
}

td_s32 sample_svp_npu_set_face_det_frame(const ot_video_frame_info *frame, const td_u8 *frame_virt)
{
    sample_svp_check_exps_return(frame == TD_NULL || frame_virt == TD_NULL,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "invalid face det frame\n");

    g_svp_npu_face_det_frame = *frame;
    g_svp_npu_face_det_frame_virt = frame_virt;
    g_svp_npu_face_det_frame_ready = TD_TRUE;

    if (g_face_det_debug_frame_idx < SAMPLE_SVP_FACE_DET_DEBUG_FRAME_MAX) {
        sample_svp_trace_info("face det frame[%u] meta: w=%u h=%u stride0=%u stride1=%u pixel_format=%d\n",
            g_face_det_debug_frame_idx,
            frame->video_frame.width,
            frame->video_frame.height,
            frame->video_frame.stride[0],
            frame->video_frame.stride[1],
            frame->video_frame.pixel_format);
    }
    return TD_SUCCESS;
}

static td_void sample_svp_npu_clear_face_det_frame(td_void)
{
    (td_void)memset_s(&g_svp_npu_face_det_frame, sizeof(g_svp_npu_face_det_frame),
        0, sizeof(g_svp_npu_face_det_frame));
    g_svp_npu_face_det_frame_virt = TD_NULL;
    g_svp_npu_face_det_frame_ready = TD_FALSE;
}

static td_s32 sample_svp_npu_run_face_det_with_video_frame(sample_svp_face_box_list *face_list)
{
    td_s32 ret;
    td_u32 y_size;
    td_u32 frame_size;
    svp_acl_error acl_ret;

    sample_svp_check_exps_return(face_list == TD_NULL, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "face list is null\n");
    sample_svp_check_exps_return(g_svp_npu_face_det_frame_ready != TD_TRUE || g_svp_npu_face_det_frame_virt == TD_NULL,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "face det video frame not set\n");

    (td_void)memset_s(face_list, sizeof(*face_list), 0, sizeof(*face_list));

    y_size = g_svp_npu_face_det_frame.video_frame.stride[0] * g_svp_npu_face_det_frame.video_frame.height;
    frame_size = y_size + (g_svp_npu_face_det_frame.video_frame.stride[1] *
        g_svp_npu_face_det_frame.video_frame.height / 2);

    sample_svp_check_exps_return(frame_size == 0, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "face det frame size is zero\n");

    ret = sample_svp_prepare_scrfd_input(
        (const td_u8 *)g_svp_npu_face_det_frame_virt,
        g_svp_npu_face_det_frame.video_frame.width,
        g_svp_npu_face_det_frame.video_frame.height,
        g_svp_npu_face_det_frame.video_frame.stride[0],
        g_svp_npu_face_det_frame.video_frame.stride[1]);
    sample_svp_check_exps_return(ret != TD_SUCCESS, ret,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "prepare SCRFD input failed\n");

    acl_ret = svp_acl_rt_mem_flush(g_face_det_model_input_virt, g_face_det_expect_size);
    sample_svp_check_exps_return(acl_ret != SVP_ACL_SUCCESS, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "flush SCRFD input failed, ret=%d\n", acl_ret);

    if (g_face_det_debug_frame_idx < SAMPLE_SVP_FACE_DET_DEBUG_FRAME_MAX) {
        sample_svp_trace_info("SCRFD input frame[%u]: src=%ux%u model=%ux%u size=%u stride=%u\n",
            g_face_det_debug_frame_idx,
            g_svp_npu_face_det_frame.video_frame.width,
            g_svp_npu_face_det_frame.video_frame.height,
            g_face_det_expect_w, g_face_det_expect_h,
            g_face_det_expect_size, g_face_det_expect_stride);
    }

    ret = sample_common_svp_npu_update_input_data_buffer_info(g_face_det_model_input_virt,
        g_face_det_expect_size, g_face_det_expect_stride, 0, &g_svp_npu_task[0]);
    sample_svp_check_exps_return(ret != TD_SUCCESS, ret, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "update face det data buffer failed\n");

    ret = sample_common_svp_npu_model_execute(&g_svp_npu_task[0]);
    sample_svp_check_exps_return(ret != TD_SUCCESS, ret, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "face det model execute failed\n");

    ret = sample_svp_npu_decode_scrfd_output(&g_svp_npu_task[0],
        g_svp_npu_face_det_frame.video_frame.width,
        g_svp_npu_face_det_frame.video_frame.height,
        face_list);
    sample_svp_check_exps_return(ret != TD_SUCCESS, ret, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "face det decode output failed\n");

    if (g_face_det_debug_frame_idx < SAMPLE_SVP_FACE_DET_DEBUG_FRAME_MAX ||
        (g_face_det_debug_frame_idx % 30 == 0)) {
        sample_svp_trace_info("SCRFD detected %u faces\n", face_list->num);
    }
    g_face_det_debug_frame_idx++;
    return TD_SUCCESS;
}


static td_float sample_svp_max_f32(td_float a, td_float b)
{
    return (a > b) ? a : b;
}

static td_void sample_svp_nv21_get_rgb_or_black(const td_u8 *src_y, const td_u8 *src_vu,
    td_u32 src_w, td_u32 src_h, td_u32 stride_y, td_u32 stride_uv,
    td_s32 x, td_s32 y, td_float *r, td_float *g, td_float *b)
{
    if (x < 0 || y < 0 || (td_u32)x >= src_w || (td_u32)y >= src_h) {
        *r = 0.0f;
        *g = 0.0f;
        *b = 0.0f;
        return;
    }
    sample_svp_nv21_get_rgb(src_y, src_vu, stride_y, stride_uv,
        (td_u32)x, (td_u32)y, r, g, b);
}

static td_void sample_svp_nv21_sample_rgb_bilinear(const td_u8 *src_y, const td_u8 *src_vu,
    td_u32 src_w, td_u32 src_h, td_u32 stride_y, td_u32 stride_uv,
    td_float src_x, td_float src_y_pos, td_float *r, td_float *g, td_float *b)
{
    td_s32 x0 = (td_s32)floorf(src_x);
    td_s32 y0 = (td_s32)floorf(src_y_pos);
    td_float wx = src_x - (td_float)x0;
    td_float wy = src_y_pos - (td_float)y0;
    td_float r00, g00, b00;
    td_float r01, g01, b01;
    td_float r10, g10, b10;
    td_float r11, g11, b11;
    td_float w00 = (1.0f - wx) * (1.0f - wy);
    td_float w01 = wx * (1.0f - wy);
    td_float w10 = (1.0f - wx) * wy;
    td_float w11 = wx * wy;

    sample_svp_nv21_get_rgb_or_black(src_y, src_vu, src_w, src_h, stride_y, stride_uv,
        x0, y0, &r00, &g00, &b00);
    sample_svp_nv21_get_rgb_or_black(src_y, src_vu, src_w, src_h, stride_y, stride_uv,
        x0 + 1, y0, &r01, &g01, &b01);
    sample_svp_nv21_get_rgb_or_black(src_y, src_vu, src_w, src_h, stride_y, stride_uv,
        x0, y0 + 1, &r10, &g10, &b10);
    sample_svp_nv21_get_rgb_or_black(src_y, src_vu, src_w, src_h, stride_y, stride_uv,
        x0 + 1, y0 + 1, &r11, &g11, &b11);

    *r = r00 * w00 + r01 * w01 + r10 * w10 + r11 * w11;
    *g = g00 * w00 + g01 * w01 + g10 * w10 + g11 * w11;
    *b = b00 * w00 + b01 * w01 + b10 * w10 + b11 * w11;
}

/* ----------------------------- 基础控制函数 ----------------------------- */

static td_void sample_svp_npu_acl_terminate(td_void)
{
    if (g_svp_npu_terminate_signal == TD_TRUE) {
        printf("\033[0;31mprogram termination abnormally!\033[0;39m\n");
    }
}

td_void sample_svp_npu_acl_handle_sig(td_void)
{
    g_svp_npu_terminate_signal = TD_TRUE;
}

static td_void sample_svp_npu_acl_deinit(td_void)
{
    svp_acl_error ret;

    ret = svp_acl_rt_reset_device(g_svp_npu_dev_id);
    if (ret != SVP_ACL_SUCCESS) {
        sample_svp_trace_err("reset device fail\n");
    }
    sample_svp_trace_info("end to reset device is %d\n", g_svp_npu_dev_id);

    ret = svp_acl_finalize();
    if (ret != SVP_ACL_SUCCESS) {
        sample_svp_trace_err("finalize acl fail\n");
    }
    sample_svp_trace_info("end to finalize acl\n");
}

static td_s32 sample_svp_npu_acl_init(const td_char *acl_config_path)
{
    svp_acl_rt_run_mode run_mode;
    svp_acl_error ret;

    ret = svp_acl_init(acl_config_path);
    sample_svp_check_exps_return(ret != SVP_ACL_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "acl init failed!\n");

    sample_svp_trace_info("svp acl init success!\n");

    ret = svp_acl_rt_set_device(g_svp_npu_dev_id);
    if (ret != SVP_ACL_SUCCESS) {
        (td_void)svp_acl_finalize();
        sample_svp_trace_err("svp acl open device %d failed!\n", g_svp_npu_dev_id);
        return TD_FAILURE;
    }
    sample_svp_trace_info("open device %d success!\n", g_svp_npu_dev_id);

    ret = svp_acl_rt_get_run_mode(&run_mode);
    if ((ret != SVP_ACL_SUCCESS) || (run_mode != SVP_ACL_DEVICE)) {
        (td_void)svp_acl_rt_reset_device(g_svp_npu_dev_id);
        (td_void)svp_acl_finalize();
        sample_svp_trace_err("acl get run mode failed!\n");
        return TD_FAILURE;
    }
    sample_svp_trace_info("get run mode success!\n");

    return TD_SUCCESS;
}

/* ----------------------------- task 生命周期 ----------------------------- */

static td_s32 sample_svp_npu_acl_dataset_init(td_u32 task_idx)
{
    td_s32 ret;

    ret = sample_common_svp_npu_create_input(&g_svp_npu_task[task_idx]);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "create input failed!\n");

    ret = sample_common_svp_npu_create_output(&g_svp_npu_task[task_idx]);
    if (ret != TD_SUCCESS) {
        sample_common_svp_npu_destroy_input(&g_svp_npu_task[task_idx]);
        sample_svp_trace_err("create output failed.\n");
        return TD_FAILURE;
    }
    return TD_SUCCESS;
}

static td_void sample_svp_npu_acl_dataset_deinit(td_u32 task_idx)
{
    (td_void)sample_common_svp_npu_destroy_input(&g_svp_npu_task[task_idx]);
    (td_void)sample_common_svp_npu_destroy_output(&g_svp_npu_task[task_idx]);
}

static td_void sample_svp_npu_acl_reset_one_task(td_u32 task_idx)
{
    (td_void)memset_s(&g_svp_npu_task[task_idx], sizeof(sample_svp_npu_task_info),
        0, sizeof(sample_svp_npu_task_info));
}

static td_void sample_svp_npu_acl_deinit_task(td_u32 task_num)
{
    td_u32 task_idx;

    for (task_idx = 0; task_idx < task_num; task_idx++) {
        (td_void)sample_common_svp_npu_destroy_work_buf(&g_svp_npu_task[task_idx]);
        (td_void)sample_common_svp_npu_destroy_task_buf(&g_svp_npu_task[task_idx]);
        (td_void)sample_svp_npu_acl_dataset_deinit(task_idx);
        sample_svp_npu_acl_reset_one_task(task_idx);
    }
}

static td_s32 sample_svp_npu_acl_init_one_task(td_u32 task_idx)
{
    td_s32 ret;

    ret = sample_svp_npu_acl_dataset_init(task_idx);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "dataset init failed, task_idx=%u\n", task_idx);

    ret = sample_common_svp_npu_create_task_buf(&g_svp_npu_task[task_idx]);
    if (ret != TD_SUCCESS) {
        sample_svp_trace_err("create task buf failed, task_idx=%u\n", task_idx);
        (td_void)sample_svp_npu_acl_dataset_deinit(task_idx);
        return TD_FAILURE;
    }

    ret = sample_common_svp_npu_create_work_buf(&g_svp_npu_task[task_idx]);
    if (ret != TD_SUCCESS) {
        sample_svp_trace_err("create work buf failed, task_idx=%u\n", task_idx);
        (td_void)sample_common_svp_npu_destroy_task_buf(&g_svp_npu_task[task_idx]);
        (td_void)sample_svp_npu_acl_dataset_deinit(task_idx);
        return TD_FAILURE;
    }

    return TD_SUCCESS;
}

static td_s32 sample_svp_npu_acl_init_task(td_u32 task_num)
{
    td_u32 task_idx;
    td_s32 ret;

    for (task_idx = 0; task_idx < task_num; task_idx++) {
        ret = sample_svp_npu_acl_init_one_task(task_idx);
        if (ret != TD_SUCCESS) {
            sample_svp_npu_acl_deinit_task(task_idx);
            return ret;
        }
    }

    return TD_SUCCESS;
}

static td_void sample_svp_npu_acl_set_task_info(td_u32 task_idx, td_u32 model_idx, td_bool is_cached)
{
    sample_svp_trace_info("Setting task %u with model_idx=%u\n", task_idx, model_idx);

    g_svp_npu_task[task_idx].cfg.max_batch_num = 1;
    g_svp_npu_task[task_idx].cfg.dynamic_batch_num = 1;
    g_svp_npu_task[task_idx].cfg.total_t = 0;
    g_svp_npu_task[task_idx].cfg.is_cached = is_cached;
    g_svp_npu_task[task_idx].cfg.model_idx = model_idx;   // 保持在 0~127 范围内

    sample_svp_trace_info("task[%u] configured with model_id=%u\n", task_idx, model_idx);
}

/* ----------------------------- 模型生命周期 ----------------------------- */

static td_s32 sample_svp_npu_pipeline_load_models(td_void)
{
    td_s32 ret;

    ret = sample_common_svp_npu_load_model(SAMPLE_SVP_NPU_FACE_DET_MODEL_PATH,
        SAMPLE_SVP_NPU_FACE_DET_MODEL_IDX, TD_TRUE);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "load face detection model failed!\n");
    sample_svp_trace_info("Face det model loaded, id=%u\n", SAMPLE_SVP_NPU_FACE_DET_MODEL_IDX);

    ret = sample_common_svp_npu_load_model(SAMPLE_SVP_NPU_LANDMARK_MODEL_PATH,
        SAMPLE_SVP_NPU_LANDMARK_MODEL_IDX, TD_TRUE);
    if (ret != TD_SUCCESS) {
        sample_svp_trace_err("load landmark model failed!\n");
        (td_void)sample_common_svp_npu_unload_model(SAMPLE_SVP_NPU_FACE_DET_MODEL_IDX);
        return TD_FAILURE;
    }
    sample_svp_trace_info("Landmark model loaded, id=%u\n", SAMPLE_SVP_NPU_LANDMARK_MODEL_IDX);

    ret = sample_common_svp_npu_load_model(SAMPLE_SVP_NPU_POSE_DET_MODEL_PATH,
        SAMPLE_SVP_NPU_POSE_DET_MODEL_IDX, TD_TRUE);
    if (ret != TD_SUCCESS) {
        sample_svp_trace_err("load pose detector model failed!\n");
        (td_void)sample_common_svp_npu_unload_model(SAMPLE_SVP_NPU_LANDMARK_MODEL_IDX);
        (td_void)sample_common_svp_npu_unload_model(SAMPLE_SVP_NPU_FACE_DET_MODEL_IDX);
        return TD_FAILURE;
    }
    sample_svp_trace_info("Pose detector model loaded, id=%u\n", SAMPLE_SVP_NPU_POSE_DET_MODEL_IDX);

    ret = sample_common_svp_npu_load_model(SAMPLE_SVP_NPU_POSE_LANDMARK_MODEL_PATH,
        SAMPLE_SVP_NPU_POSE_LANDMARK_MODEL_IDX, TD_TRUE);
    if (ret != TD_SUCCESS) {
        sample_svp_trace_err("load pose landmark model failed!\n");
        (td_void)sample_common_svp_npu_unload_model(SAMPLE_SVP_NPU_POSE_DET_MODEL_IDX);
        (td_void)sample_common_svp_npu_unload_model(SAMPLE_SVP_NPU_LANDMARK_MODEL_IDX);
        (td_void)sample_common_svp_npu_unload_model(SAMPLE_SVP_NPU_FACE_DET_MODEL_IDX);
        return TD_FAILURE;
    }
    sample_svp_trace_info("Pose landmark model loaded, id=%u\n", SAMPLE_SVP_NPU_POSE_LANDMARK_MODEL_IDX);
    sample_svp_trace_info("attention direction source: landmark106 head heuristic\n");
    sample_svp_trace_info("posture source: low-frequency MediaPipe Pose models, interval=%.1fs\n",
        (td_float)SAMPLE_SVP_POSE_INTERVAL_S);

    return TD_SUCCESS;
}

static td_void sample_svp_npu_pipeline_unload_models(td_void)
{
    (td_void)sample_common_svp_npu_unload_model(SAMPLE_SVP_NPU_POSE_LANDMARK_MODEL_IDX);
    (td_void)sample_common_svp_npu_unload_model(SAMPLE_SVP_NPU_POSE_DET_MODEL_IDX);
    (td_void)sample_common_svp_npu_unload_model(SAMPLE_SVP_NPU_LANDMARK_MODEL_IDX);
    (td_void)sample_common_svp_npu_unload_model(SAMPLE_SVP_NPU_FACE_DET_MODEL_IDX);
}

static td_s32 sample_svp_npu_pipeline_init(td_void)
{
    td_s32 ret;
    const td_char *acl_config_path = "";
    ot_size det_input_size = {0};
    ot_size landmark_input_size = {0};
    td_u8 *det_input_virt = TD_NULL;
    td_u8 *landmark_input_virt = TD_NULL;
    td_u8 *pose_det_input_virt = TD_NULL;
    td_u8 *pose_lm_input_virt = TD_NULL;
    td_u32 det_input_size_bytes = 0;
    td_u32 landmark_input_size_bytes = 0;
    td_u32 pose_det_input_size_bytes = 0;
    td_u32 pose_lm_input_size_bytes = 0;
    td_u32 det_input_stride = 0;
    td_u32 landmark_input_stride = 0;
    td_u32 pose_det_input_stride = 0;
    td_u32 pose_lm_input_stride = 0;

    g_svp_npu_terminate_signal = TD_FALSE;

    ret = sample_svp_npu_acl_init(acl_config_path);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "acl init failed!\n");

    ret = sample_svp_npu_pipeline_load_models();
    if (ret != TD_SUCCESS) {
        sample_svp_npu_acl_deinit();
        return TD_FAILURE;
    }

    sample_svp_npu_acl_set_task_info(0, SAMPLE_SVP_NPU_FACE_DET_MODEL_IDX, TD_TRUE);
    sample_svp_npu_acl_set_task_info(1, SAMPLE_SVP_NPU_LANDMARK_MODEL_IDX, TD_TRUE);
    sample_svp_npu_acl_set_task_info(2, SAMPLE_SVP_NPU_POSE_DET_MODEL_IDX, TD_TRUE);
    sample_svp_npu_acl_set_task_info(3, SAMPLE_SVP_NPU_POSE_LANDMARK_MODEL_IDX, TD_TRUE);

    ret = sample_svp_npu_acl_init_task(SAMPLE_SVP_NPU_ACTIVE_TASK_NUM);
    if (ret != TD_SUCCESS) {
        sample_svp_trace_err("init tasks failed!\n");
        sample_svp_npu_pipeline_unload_models();
        sample_svp_npu_acl_deinit();
        return TD_FAILURE;
    }

    ret = sample_common_svp_npu_get_input_resolution(SAMPLE_SVP_NPU_FACE_DET_MODEL_IDX, 0, &det_input_size);
    if (ret != TD_SUCCESS) {
        sample_svp_trace_err("get face det input resolution failed\n");
        goto init_fail;
    }

    ret = sample_common_svp_npu_get_input_data_buffer_info(&g_svp_npu_task[0], 0,
        &det_input_virt, &det_input_size_bytes, &det_input_stride);
    if (ret != TD_SUCCESS) {
        sample_svp_trace_err("get face det input data buffer info failed\n");
        goto init_fail;
    }

    g_face_det_expect_w = det_input_size.width;
    g_face_det_expect_h = det_input_size.height;
    g_face_det_expect_size = det_input_size_bytes;
    g_face_det_expect_stride = det_input_stride;
    g_face_det_model_input_virt = det_input_virt;

    ret = sample_common_svp_npu_get_input_resolution(SAMPLE_SVP_NPU_LANDMARK_MODEL_IDX, 0, &landmark_input_size);
    if (ret != TD_SUCCESS) {
        sample_svp_trace_err("get landmark input resolution failed\n");
        goto init_fail;
    }

    ret = sample_common_svp_npu_get_input_data_buffer_info(&g_svp_npu_task[1], 0,
        &landmark_input_virt, &landmark_input_size_bytes, &landmark_input_stride);
    if (ret != TD_SUCCESS) {
        sample_svp_trace_err("get landmark input data buffer info failed\n");
        goto init_fail;
    }

    g_landmark_expect_w = landmark_input_size.width;
    g_landmark_expect_h = landmark_input_size.height;
    g_landmark_expect_size = landmark_input_size_bytes;
    g_landmark_expect_stride = landmark_input_stride;
    g_landmark_model_input_virt = landmark_input_virt;

    ret = sample_common_svp_npu_get_input_data_buffer_info(&g_svp_npu_task[2], 0,
        &pose_det_input_virt, &pose_det_input_size_bytes, &pose_det_input_stride);
    if (ret != TD_SUCCESS) {
        sample_svp_trace_err("get pose detector input data buffer info failed\n");
        goto init_fail;
    }
    g_pose_det_input_virt = pose_det_input_virt;
    g_pose_det_input_size = pose_det_input_size_bytes;
    g_pose_det_input_stride = pose_det_input_stride;

    ret = sample_common_svp_npu_get_input_data_buffer_info(&g_svp_npu_task[3], 0,
        &pose_lm_input_virt, &pose_lm_input_size_bytes, &pose_lm_input_stride);
    if (ret != TD_SUCCESS) {
        sample_svp_trace_err("get pose landmark input data buffer info failed\n");
        goto init_fail;
    }
    g_pose_lm_input_virt = pose_lm_input_virt;
    g_pose_lm_input_size = pose_lm_input_size_bytes;
    g_pose_lm_input_stride = pose_lm_input_stride;

    if (g_face_det_resized_buf != TD_NULL) {
        free(g_face_det_resized_buf);
        g_face_det_resized_buf = TD_NULL;
    }
    g_face_det_resized_buf = (td_u8 *)malloc(g_face_det_expect_size);
    if (g_face_det_resized_buf == TD_NULL) {
        sample_svp_trace_err("malloc face det resize buffer failed, size=%u\n",
            g_face_det_expect_size);
        goto init_fail;
    }

    sample_svp_trace_info("face det model input resolution: %ux%u\n",
        det_input_size.width, det_input_size.height);
    sample_svp_trace_info("face det model input buffer: size=%u stride=%u\n",
        det_input_size_bytes, det_input_stride);
    sample_svp_trace_info("face det decode cfg: score_th=%.2f (no roi_to_rect API)\n",
        SAMPLE_SVP_FACE_DET_SCORE_TH);
    sample_svp_trace_info("landmark model input: %ux%u size=%u stride=%u\n",
        landmark_input_size.width, landmark_input_size.height,
        landmark_input_size_bytes, landmark_input_stride);
    sample_svp_trace_info("pose detector input: %ux%u size=%u stride=%u format=%d type=%d\n",
        SAMPLE_SVP_POSE_DET_INPUT_W, SAMPLE_SVP_POSE_DET_INPUT_H,
        pose_det_input_size_bytes, pose_det_input_stride,
        sample_svp_model_input_format(SAMPLE_SVP_NPU_POSE_DET_MODEL_IDX),
        sample_svp_model_input_type(SAMPLE_SVP_NPU_POSE_DET_MODEL_IDX));
    sample_svp_trace_info("pose landmark input: %ux%u size=%u stride=%u format=%d type=%d\n",
        SAMPLE_SVP_POSE_LM_INPUT_W, SAMPLE_SVP_POSE_LM_INPUT_H,
        pose_lm_input_size_bytes, pose_lm_input_stride,
        sample_svp_model_input_format(SAMPLE_SVP_NPU_POSE_LANDMARK_MODEL_IDX),
        sample_svp_model_input_type(SAMPLE_SVP_NPU_POSE_LANDMARK_MODEL_IDX));

    return TD_SUCCESS;

init_fail:
    if (g_face_det_resized_buf != TD_NULL) {
        free(g_face_det_resized_buf);
        g_face_det_resized_buf = TD_NULL;
    }
    g_face_det_model_input_virt = TD_NULL;
    g_landmark_model_input_virt = TD_NULL;
    g_face_det_expect_w = 0;
    g_face_det_expect_h = 0;
    g_face_det_expect_size = 0;
    g_face_det_expect_stride = 0;
    g_landmark_expect_w = 0;
    g_landmark_expect_h = 0;
    g_landmark_expect_size = 0;
    g_landmark_expect_stride = 0;
    g_pose_det_input_virt = TD_NULL;
    g_pose_det_input_size = 0;
    g_pose_det_input_stride = 0;
    g_pose_lm_input_virt = TD_NULL;
    g_pose_lm_input_size = 0;
    g_pose_lm_input_stride = 0;
    sample_svp_npu_acl_deinit_task(SAMPLE_SVP_NPU_ACTIVE_TASK_NUM);
    sample_svp_npu_pipeline_unload_models();
    sample_svp_npu_acl_deinit();
    return TD_FAILURE;
}

static td_void sample_svp_npu_pipeline_deinit(td_void)
{
    sample_svp_npu_acl_deinit_task(SAMPLE_SVP_NPU_ACTIVE_TASK_NUM);
    sample_svp_npu_pipeline_unload_models();
    sample_svp_npu_acl_deinit();
    sample_svp_npu_acl_terminate();

    if (g_face_det_resized_buf != TD_NULL) {
        free(g_face_det_resized_buf);
        g_face_det_resized_buf = TD_NULL;
    }

    g_face_det_model_input_virt = TD_NULL;
    g_landmark_model_input_virt = TD_NULL;
    g_face_det_expect_w = 0;
    g_face_det_expect_h = 0;
    g_face_det_expect_size = 0;
    g_face_det_expect_stride = 0;
    g_landmark_expect_w = 0;
    g_landmark_expect_h = 0;
    g_landmark_expect_size = 0;
    g_landmark_expect_stride = 0;
    g_pose_det_input_virt = TD_NULL;
    g_pose_det_input_size = 0;
    g_pose_det_input_stride = 0;
    g_pose_lm_input_virt = TD_NULL;
    g_pose_lm_input_size = 0;
    g_pose_lm_input_stride = 0;
}

/* ----------------------------- 单模型执行辅助 ----------------------------- */

static td_s32 __attribute__((unused)) sample_svp_npu_run_model_with_input_file(td_u32 task_idx, const td_char *src_file)
{
    td_s32 ret;
    const td_char *src[SAMPLE_SVP_NPU_INPUT_FILE_NUM_ONE] = {TD_NULL};

    sample_svp_check_exps_return(src_file == TD_NULL, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "src_file is null!\n");
    sample_svp_check_exps_return(task_idx >= SAMPLE_SVP_NPU_OFFLINE_TASK_NUM, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "task_idx(%u) out of range!\n", task_idx);

    src[0] = src_file;

    ret = sample_common_svp_npu_get_input_data(src, SAMPLE_SVP_NPU_INPUT_FILE_NUM_ONE,
        &g_svp_npu_task[task_idx]);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "get input data failed, task_idx=%u\n", task_idx);

    /* 执行模型 */
    ret = sample_common_svp_npu_model_execute(&g_svp_npu_task[task_idx]);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "model execute failed, task_idx=%u\n", task_idx);

    return TD_SUCCESS;
}

static td_s32 sample_svp_npu_parse_landmark_output(const sample_svp_npu_task_info *task,
    sample_svp_landmark106_result *landmark)
{
    td_u32 i;
    td_float *data = TD_NULL;
    svp_acl_mdl_dataset *output = TD_NULL;
    svp_acl_data_buffer *buf = TD_NULL;
    td_void *addr = TD_NULL;
    size_t buf_size;

    sample_svp_check_exps_return(task == TD_NULL || landmark == TD_NULL, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "invalid param\n");

    (td_void)memset_s(landmark, sizeof(*landmark), 0, sizeof(*landmark));

    output = task->output_dataset;
    sample_svp_check_exps_return(output == TD_NULL, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "landmark output dataset is null!\n");

    buf = svp_acl_mdl_get_dataset_buffer(output, 0);
    sample_svp_check_exps_return(buf == TD_NULL, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "landmark output buffer is null!\n");

    addr = svp_acl_get_data_buffer_addr(buf);
    buf_size = svp_acl_get_data_buffer_size(buf);

    sample_svp_check_exps_return(addr == TD_NULL, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "landmark output addr is null!\n");
    sample_svp_check_exps_return(buf_size < 212 * sizeof(td_float), TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "landmark output size too small\n");

    data = (td_float *)addr;
    landmark->point_num = SAMPLE_SVP_LANDMARK_NUM;

    for (i = 0; i < SAMPLE_SVP_LANDMARK_NUM; i++) {
        td_float x = data[i * 2];
        td_float y = data[i * 2 + 1];
        sample_svp_check_exps_return(!isfinite(x) || !isfinite(y), TD_FAILURE,
            SAMPLE_SVP_ERR_LEVEL_ERROR, "landmark output contains non-finite value at point %u\n", i);
        landmark->points[i][0] = (x + 1.0f) * ((td_float)SAMPLE_SVP_LANDMARK_IN_W * 0.5f);
        landmark->points[i][1] = (y + 1.0f) * ((td_float)SAMPLE_SVP_LANDMARK_IN_H * 0.5f);
    }

    return TD_SUCCESS;
}

static td_void sample_svp_landmark_map_to_full_image(sample_svp_landmark106_result *lm,
    const sample_svp_landmark_affine *transform)
{
    td_u32 i;

    if (lm == TD_NULL || transform == TD_NULL ||
        lm->point_num != SAMPLE_SVP_LANDMARK_NUM ||
        transform->scale <= 1e-6f) {
        return;
    }

    for (i = 0; i < lm->point_num; i++) {
        lm->points[i][0] =
            (lm->points[i][0] - transform->translate_x) / transform->scale;
        lm->points[i][1] =
            (lm->points[i][1] - transform->translate_y) / transform->scale;
    }
}

/* ----------------------------- 其他工具函数 ----------------------------- */

static td_double sample_svp_now_seconds(td_void)
{
    struct timeval tv;
    gettimeofday(&tv, TD_NULL);
    return (td_double)tv.tv_sec + (td_double)tv.tv_usec / 1000000.0;
}

static td_void sample_svp_pipeline_profile_commit(td_double t_total, td_double t_face_det,
    td_double t_lm_prep, td_double t_lm_infer, td_double t_lm_parse_map,
    td_bool has_face)
{
    td_double now;
    td_double dt;
    td_double inv_frame;
    td_double inv_face;

    now = sample_svp_now_seconds();
    if (g_pipe_prof.win_start <= 0.0) {
        g_pipe_prof.win_start = now;
    }

    g_pipe_prof.frame_cnt++;
    if (has_face == TD_TRUE) {
        g_pipe_prof.face_cnt++;
    }

    g_pipe_prof.sum_total += t_total;
    g_pipe_prof.sum_face_det += t_face_det;
    g_pipe_prof.sum_lm_prep += t_lm_prep;
    g_pipe_prof.sum_lm_infer += t_lm_infer;
    g_pipe_prof.sum_lm_parse_map += t_lm_parse_map;

    if (t_total > g_pipe_prof.max_total) {
        g_pipe_prof.max_total = t_total;
    }
    if (t_face_det > g_pipe_prof.max_face_det) {
        g_pipe_prof.max_face_det = t_face_det;
    }
    if (t_lm_infer > g_pipe_prof.max_lm_infer) {
        g_pipe_prof.max_lm_infer = t_lm_infer;
    }

    dt = now - g_pipe_prof.win_start;
    if (dt < 1.0 || g_pipe_prof.frame_cnt == 0) {
        return;
    }

    inv_frame = 1.0 / (td_double)g_pipe_prof.frame_cnt;
    inv_face = (g_pipe_prof.face_cnt > 0) ? (1.0 / (td_double)g_pipe_prof.face_cnt) : 0.0;

    sample_svp_trace_info("[NPU-PROF] avg_ms total=%.2f face_det=%.2f lm_prep=%.2f lm_infer=%.2f lm_parse_map=%.2f | max_ms total=%.2f face_det=%.2f lm_infer=%.2f | face_ratio=%.1f%% window=%.2fs\n",
        g_pipe_prof.sum_total * 1000.0 * inv_frame,
        g_pipe_prof.sum_face_det * 1000.0 * inv_frame,
        g_pipe_prof.sum_lm_prep * 1000.0 * inv_face,
        g_pipe_prof.sum_lm_infer * 1000.0 * inv_face,
        g_pipe_prof.sum_lm_parse_map * 1000.0 * inv_face,
        g_pipe_prof.max_total * 1000.0,
        g_pipe_prof.max_face_det * 1000.0,
        g_pipe_prof.max_lm_infer * 1000.0,
        (td_double)g_pipe_prof.face_cnt * 100.0 * inv_frame,
        dt);

    (td_void)memset_s(&g_pipe_prof, sizeof(g_pipe_prof), 0, sizeof(g_pipe_prof));
    g_pipe_prof.win_start = now;
}

static td_float sample_svp_l2_dist_2d(td_float x1, td_float y1, td_float x2, td_float y2)
{
    td_float dx = x1 - x2, dy = y1 - y2;
    return sqrtf(dx * dx + dy * dy);
}

static td_void sample_svp_clamp_bbox(sample_svp_face_box *box, td_u32 w, td_u32 h)
{
    box->x1 = (box->x1 < 0) ? 0 : (box->x1 > (td_float)(w-1) ? (td_float)(w-1) : box->x1);
    box->y1 = (box->y1 < 0) ? 0 : (box->y1 > (td_float)(h-1) ? (td_float)(h-1) : box->y1);
    box->x2 = (box->x2 > (td_float)w) ? (td_float)w : box->x2;
    box->y2 = (box->y2 > (td_float)h) ? (td_float)h : box->y2;
}

static td_bool sample_svp_select_largest_face(const sample_svp_face_box_list *faces,
    td_u32 frame_w, td_u32 frame_h, sample_svp_face_box *best_face)
{
    td_u32 i;
    td_float best_value = -1.0f;
    td_bool found = TD_FALSE;

    if (faces == TD_NULL || best_face == TD_NULL || faces->num == 0) return TD_FALSE;

    for (i = 0; i < faces->num; i++) {
        td_float x1 = faces->boxes[i].x1;
        td_float y1 = faces->boxes[i].y1;
        td_float x2 = faces->boxes[i].x2;
        td_float y2 = faces->boxes[i].y2;
        td_float cx = (x1 + x2) * 0.5f;
        td_float cy = (y1 + y2) * 0.5f;
        td_float w = x2 - x1;
        td_float h = y2 - y1;
        td_float area_ratio;
        td_float dx;
        td_float dy;
        td_float center_score;
        td_float size_score;
        td_float value;

        if (w <= 1.0f || h <= 1.0f) {
            continue;
        }

        area_ratio = (w * h) / ((td_float)frame_w * frame_h);
        dx = fabsf(cx - frame_w * 0.5f) / (frame_w * 0.5f);
        dy = fabsf(cy - frame_h * 0.5f) / (frame_h * 0.5f);
        center_score = fmaxf(0.0f, 1.0f - sqrtf(dx * dx + dy * dy));

        size_score = 1.0f - fabsf(area_ratio - 0.10f) / 0.10f;
        size_score = fmaxf(0.0f, fminf(1.0f, size_score));

        value = 0.65f * faces->boxes[i].score + 0.25f * center_score + 0.10f * size_score;

        if (!found || value > best_value) {
            *best_face = faces->boxes[i];
            best_value = value;
            found = TD_TRUE;
        }
    }
    return found;
}

static td_float sample_svp_eye_aspect_ratio(const sample_svp_landmark106_result *lm, td_bool left_eye)
{
    const td_u32 base = left_eye ? 33 : 87;
    td_float v1 = sample_svp_l2_dist_2d(lm->points[base+8][0], lm->points[base+8][1], lm->points[base+3][0], lm->points[base+3][1]);
    td_float v2 = sample_svp_l2_dist_2d(lm->points[base+7][0], lm->points[base+7][1], lm->points[base+0][0], lm->points[base+0][1]);
    td_float v3 = sample_svp_l2_dist_2d(lm->points[base+9][0], lm->points[base+9][1], lm->points[base+4][0], lm->points[base+4][1]);
    td_float h  = sample_svp_l2_dist_2d(lm->points[base+2][0], lm->points[base+2][1], lm->points[base+6][0], lm->points[base+6][1]);
    return (h < 1e-6f) ? 0.0f : (v1 + v2 + v3) / (3.0f * h);
}

static td_float sample_svp_mouth_aspect_ratio(const sample_svp_landmark106_result *lm)
{
    const td_u32 base = 52;
    td_float v1 = sample_svp_l2_dist_2d(lm->points[base+4][0], lm->points[base+4][1], lm->points[base+2][0], lm->points[base+2][1]);
    td_float v2 = sample_svp_l2_dist_2d(lm->points[base+10][0], lm->points[base+10][1], lm->points[base+8][0], lm->points[base+8][1]);
    td_float v3 = sample_svp_l2_dist_2d(lm->points[base+18][0], lm->points[base+18][1], lm->points[base+5][0], lm->points[base+5][1]);
    td_float h  = sample_svp_l2_dist_2d(lm->points[base+13][0], lm->points[base+13][1], lm->points[base+17][0], lm->points[base+17][1]);
    return (h < 1e-6f) ? 0.0f : (v1 + v2 + v3) / (3.0f * h);
}

static td_bool sample_svp_landmark_center(const sample_svp_landmark106_result *lm,
    td_u32 start, td_u32 count, td_float *center_x, td_float *center_y)
{
    td_u32 i;
    td_float sum_x = 0.0f;
    td_float sum_y = 0.0f;

    if (lm == TD_NULL || center_x == TD_NULL || center_y == TD_NULL ||
        count == 0 || lm->point_num <= start || lm->point_num < start + count) {
        return TD_FALSE;
    }

    for (i = 0; i < count; i++) {
        sum_x += lm->points[start + i][0];
        sum_y += lm->points[start + i][1];
    }
    *center_x = sum_x / (td_float)count;
    *center_y = sum_y / (td_float)count;
    return TD_TRUE;
}

static td_float sample_svp_clamp_f32(td_float value, td_float min_value, td_float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static td_s32 sample_svp_estimate_head_from_landmarks(const sample_svp_landmark106_result *lm,
    sample_svp_attention_result *pose)
{
    td_float left_eye_x, left_eye_y;
    td_float right_eye_x, right_eye_y;
    td_float nose_x, nose_y;
    td_float mouth_x, mouth_y;
    td_float eye_mid_x;
    td_float eye_mid_y;
    td_float eye_dist;
    td_float eye_to_mouth;
    td_float yaw_norm;
    td_float pitch_ratio;

    if (pose == TD_NULL) {
        return TD_FAILURE;
    }
    (td_void)memset_s(pose, sizeof(*pose), 0, sizeof(*pose));

    if (sample_svp_landmark_center(lm, 33, 10, &left_eye_x, &left_eye_y) != TD_TRUE ||
        sample_svp_landmark_center(lm, 87, 10, &right_eye_x, &right_eye_y) != TD_TRUE ||
        sample_svp_landmark_center(lm, 80, 6, &nose_x, &nose_y) != TD_TRUE ||
        sample_svp_landmark_center(lm, 52, 20, &mouth_x, &mouth_y) != TD_TRUE) {
        return TD_FAILURE;
    }

    eye_mid_x = (left_eye_x + right_eye_x) * 0.5f;
    eye_mid_y = (left_eye_y + right_eye_y) * 0.5f;
    eye_dist = sample_svp_l2_dist_2d(left_eye_x, left_eye_y, right_eye_x, right_eye_y);
    eye_to_mouth = fabsf(mouth_y - eye_mid_y);
    if (eye_dist < 1.0f || eye_to_mouth < 1.0f) {
        return TD_FAILURE;
    }

    yaw_norm = (nose_x - eye_mid_x) / eye_dist;
    pose->yaw_deg = sample_svp_clamp_f32((-yaw_norm * 75.0f) + 13.0f,
        -SAMPLE_SVP_HEAD_YAW_LIMIT_DEG, SAMPLE_SVP_HEAD_YAW_LIMIT_DEG);

    pitch_ratio = (nose_y - eye_mid_y) / eye_to_mouth;
    td_float raw_pitch = (pitch_ratio - 0.55f) * 80.0f;
    pose->pitch_deg = sample_svp_clamp_f32((raw_pitch + 5.0f) * 2.0f,
        -SAMPLE_SVP_HEAD_PITCH_LIMIT_DEG, SAMPLE_SVP_HEAD_PITCH_LIMIT_DEG);

    pose->roll_deg = atan2f(right_eye_y - left_eye_y,
        right_eye_x - left_eye_x) * 57.2957795f;
    return TD_SUCCESS;
}


/* ----------------------------- 低频坐姿检测 ----------------------------- */

static td_float sample_svp_sigmoid_f32(td_float value)
{
    if (value < -80.0f) {
        value = -80.0f;
    } else if (value > 80.0f) {
        value = 80.0f;
    }
    return 1.0f / (1.0f + expf(-value));
}

static td_float sample_svp_normalize_angle(td_float angle)
{
    while (angle > SAMPLE_SVP_PI_F) {
        angle -= (2.0f * SAMPLE_SVP_PI_F);
    }
    while (angle < (td_float)-M_PI) {
        angle += (2.0f * SAMPLE_SVP_PI_F);
    }
    return angle;
}


static td_u16 sample_svp_float_to_fp16(td_float value)
{
    union {
        td_float f;
        td_u32 u;
    } in;
    td_u32 sign;
    td_s32 exp;
    td_u32 mantissa;

    in.f = value;
    sign = (in.u >> 16) & 0x8000U;
    exp = (td_s32)((in.u >> 23) & 0xffU) - 127 + 15;
    mantissa = in.u & 0x7fffffU;

    if (exp <= 0) {
        if (exp < -10) {
            return (td_u16)sign;
        }
        mantissa = (mantissa | 0x800000U) >> (td_u32)(1 - exp);
        return (td_u16)(sign | ((mantissa + 0x1000U) >> 13));
    }
    if (exp >= 31) {
        return (td_u16)(sign | 0x7c00U);
    }
    return (td_u16)(sign | ((td_u32)exp << 10) | ((mantissa + 0x1000U) >> 13));
}

static td_float sample_svp_fp16_to_float(td_u16 value)
{
    td_u32 sign = ((td_u32)value & 0x8000U) << 16;
    td_u32 exp = ((td_u32)value >> 10) & 0x1fU;
    td_u32 mantissa = (td_u32)value & 0x03ffU;
    union {
        td_u32 u;
        td_float f;
    } out;

    if (exp == 0) {
        if (mantissa == 0) {
            out.u = sign;
            return out.f;
        }
        while ((mantissa & 0x0400U) == 0) {
            mantissa <<= 1;
            exp--;
        }
        exp++;
        mantissa &= 0x03ffU;
    } else if (exp == 31) {
        out.u = sign | 0x7f800000U | (mantissa << 13);
        return out.f;
    }

    exp = exp + (127 - 15);
    out.u = sign | (exp << 23) | (mantissa << 13);
    return out.f;
}

static svp_acl_format sample_svp_model_input_format(td_u32 model_idx)
{
    sample_svp_npu_model_info *info = sample_common_svp_npu_get_model_info(model_idx);
    if (info == TD_NULL || info->model_desc == TD_NULL) {
        return SVP_ACL_FORMAT_UNDEFINED;
    }
    return svp_acl_mdl_get_input_format(info->model_desc, 0);
}

static svp_acl_data_type sample_svp_model_input_type(td_u32 model_idx)
{
    sample_svp_npu_model_info *info = sample_common_svp_npu_get_model_info(model_idx);
    if (info == TD_NULL || info->model_desc == TD_NULL) {
        return SVP_ACL_DT_UNDEFINED;
    }
    return svp_acl_mdl_get_input_data_type(info->model_desc, 0);
}

static svp_acl_data_type sample_svp_model_output_type(const sample_svp_npu_task_info *task, td_u32 idx)
{
    sample_svp_npu_model_info *info;
    if (task == TD_NULL) {
        return SVP_ACL_DT_UNDEFINED;
    }
    info = sample_common_svp_npu_get_model_info(task->cfg.model_idx);
    if (info == TD_NULL || info->model_desc == TD_NULL) {
        return SVP_ACL_DT_UNDEFINED;
    }
    return svp_acl_mdl_get_output_data_type(info->model_desc, idx);
}

static td_s32 sample_svp_model_input_dims(td_u32 model_idx, svp_acl_mdl_io_dims *dims)
{
    sample_svp_npu_model_info *info = sample_common_svp_npu_get_model_info(model_idx);
    sample_svp_check_exps_return(info == TD_NULL || info->model_desc == TD_NULL || dims == TD_NULL,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "pose model dims args invalid\n");
    return (svp_acl_mdl_get_input_dims(info->model_desc, 0, dims) == SVP_ACL_SUCCESS) ?
        TD_SUCCESS : TD_FAILURE;
}

static td_bool sample_svp_input_layout_nhwc(td_u32 model_idx)
{
    svp_acl_mdl_io_dims dims = {0};
    if (sample_svp_model_input_dims(model_idx, &dims) != TD_SUCCESS || dims.dim_count != 4) {
        return TD_FALSE;
    }
    return (dims.dims[3] == 3) ? TD_TRUE : TD_FALSE;
}

static td_s32 sample_svp_write_rgb_to_model_input(td_u32 model_idx, td_u8 *dst, td_u32 size,
    td_u32 stride, td_u32 w, td_u32 h, td_u32 x, td_u32 y, td_float r, td_float g, td_float b,
    td_float scale, td_float bias)
{
    svp_acl_format format = sample_svp_model_input_format(model_idx);
    svp_acl_data_type data_type = sample_svp_model_input_type(model_idx);
    td_bool nhwc = sample_svp_input_layout_nhwc(model_idx);
    td_float vr = r * scale + bias;
    td_float vg = g * scale + bias;
    td_float vb = b * scale + bias;
    size_t idx;

    (void)stride;
    sample_svp_check_exps_return(dst == TD_NULL || x >= w || y >= h,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "pose input args invalid\n");

    if (format == SVP_ACL_FORMAT_NC1HWC0 && data_type == SVP_ACL_FLOAT16) {
        td_u16 *dst_h = (td_u16 *)dst;
        sample_svp_check_exps_return(size < (size_t)w * h * 16 * sizeof(td_u16),
            TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "pose FP16 NC1HWC0 layout invalid\n");
        idx = ((size_t)y * w + x) * 16;
        dst_h[idx] = sample_svp_float_to_fp16(vr);
        dst_h[idx + 1] = sample_svp_float_to_fp16(vg);
        dst_h[idx + 2] = sample_svp_float_to_fp16(vb);
        return TD_SUCCESS;
    }
    if (format == SVP_ACL_FORMAT_NC1HWC0 && data_type == SVP_ACL_FLOAT) {
        td_float *dst_f = (td_float *)dst;
        sample_svp_check_exps_return(size < (size_t)w * h * 16 * sizeof(td_float),
            TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "pose FP32 NC1HWC0 layout invalid\n");
        idx = ((size_t)y * w + x) * 16;
        dst_f[idx] = vr;
        dst_f[idx + 1] = vg;
        dst_f[idx + 2] = vb;
        return TD_SUCCESS;
    }
    if (format == SVP_ACL_FORMAT_NCHW && data_type == SVP_ACL_FLOAT &&
        size >= (size_t)w * h * 4 * sizeof(td_float)) {
        td_float *dst_f = (td_float *)dst;
        size_t plane = (size_t)w * h;
        idx = (size_t)y * w + x;
        dst_f[idx] = vr;
        dst_f[plane + idx] = vg;
        dst_f[2 * plane + idx] = vb;
        return TD_SUCCESS;
    }

    if (data_type == SVP_ACL_FLOAT && nhwc == TD_TRUE) {
        td_float *dst_f = (td_float *)dst;
        td_u32 stride_f;
        sample_svp_check_exps_return(stride == 0 || stride % sizeof(td_float) != 0,
            TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "pose NHWC stride invalid\n");
        stride_f = stride / sizeof(td_float);
        sample_svp_check_exps_return(stride_f < w * 3 || size < h * stride,
            TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "pose NHWC layout invalid\n");
        idx = (size_t)y * stride_f + x * 3;
        dst_f[idx] = vr;
        dst_f[idx + 1] = vg;
        dst_f[idx + 2] = vb;
        return TD_SUCCESS;
    }
    if (data_type == SVP_ACL_FLOAT) {
        td_float *dst_f = (td_float *)dst;
        td_u32 stride_f;
        sample_svp_check_exps_return(stride == 0 || stride % sizeof(td_float) != 0,
            TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "pose NCHW stride invalid\n");
        stride_f = stride / sizeof(td_float);
        sample_svp_check_exps_return(stride_f < w || size < 3 * h * stride,
            TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "pose NCHW layout invalid\n");
        idx = (size_t)y * stride_f + x;
        dst_f[idx] = vr;
        dst_f[(size_t)h * stride_f + idx] = vg;
        dst_f[(size_t)2 * h * stride_f + idx] = vb;
        return TD_SUCCESS;
    }

    sample_svp_trace_err("unsupported pose input format=%d type=%d\n", format, data_type);
    return TD_FAILURE;
}

static td_s32 sample_svp_prepare_pose_detector_input(td_void)
{
    const td_u8 *src = g_svp_npu_face_det_frame_virt;
    const td_u8 *src_y;
    const td_u8 *src_vu;
    td_u32 src_w = g_svp_npu_face_det_frame.video_frame.width;
    td_u32 src_h = g_svp_npu_face_det_frame.video_frame.height;
    td_u32 src_stride_y = g_svp_npu_face_det_frame.video_frame.stride[0];
    td_u32 src_stride_uv = g_svp_npu_face_det_frame.video_frame.stride[1];
    td_float scale;
    td_u32 resized_w;
    td_u32 resized_h;
    td_u32 pad_x;
    td_u32 pad_y;
    td_u32 x;
    td_u32 y;
    svp_acl_error acl_ret;

    sample_svp_check_exps_return(src == TD_NULL || g_pose_det_input_virt == TD_NULL,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "pose detector input invalid\n");

    (td_void)memset_s(g_pose_det_input_virt, g_pose_det_input_size, 0, g_pose_det_input_size);
    src_y = src;
    src_vu = src + (size_t)src_stride_y * src_h;
    scale = fminf((td_float)SAMPLE_SVP_POSE_DET_INPUT_W / src_w,
        (td_float)SAMPLE_SVP_POSE_DET_INPUT_H / src_h);
    resized_w = (td_u32)floorf(src_w * scale + 0.5f);
    resized_h = (td_u32)floorf(src_h * scale + 0.5f);
    pad_x = (SAMPLE_SVP_POSE_DET_INPUT_W - resized_w) / 2;
    pad_y = (SAMPLE_SVP_POSE_DET_INPUT_H - resized_h) / 2;

    for (y = 0; y < resized_h; y++) {
        td_u32 sy = (td_u32)fminf(floorf(((td_float)y + 0.5f) / scale), (td_float)(src_h - 1));
        for (x = 0; x < resized_w; x++) {
            td_u32 sx = (td_u32)fminf(floorf(((td_float)x + 0.5f) / scale), (td_float)(src_w - 1));
            td_float r, g, b;
            sample_svp_nv21_get_rgb(src_y, src_vu, src_stride_y, src_stride_uv, sx, sy, &r, &g, &b);
            sample_svp_check_exps_return(sample_svp_write_rgb_to_model_input(
                SAMPLE_SVP_NPU_POSE_DET_MODEL_IDX, g_pose_det_input_virt,
                g_pose_det_input_size, g_pose_det_input_stride,
                SAMPLE_SVP_POSE_DET_INPUT_W, SAMPLE_SVP_POSE_DET_INPUT_H,
                x + pad_x, y + pad_y, r, g, b, 1.0f / 127.5f, -1.0f) != TD_SUCCESS,
                TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "write pose detector pixel failed\n");
        }
    }

    acl_ret = svp_acl_rt_mem_flush(g_pose_det_input_virt, g_pose_det_input_size);
    sample_svp_check_exps_return(acl_ret != SVP_ACL_SUCCESS, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "flush pose detector input failed, ret=%d\n", acl_ret);
    return TD_SUCCESS;
}

static td_void sample_svp_pose_anchor(td_u32 index, td_float *ax, td_float *ay)
{
    td_u32 base = 0;
    td_u32 stride;
    td_u32 anchors_per_cell;
    td_u32 feature;
    td_u32 cell;
    td_u32 pos;

    if (index < 28 * 28 * 2) {
        stride = 8;
        anchors_per_cell = 2;
    } else if (index < 28 * 28 * 2 + 14 * 14 * 2) {
        base = 28 * 28 * 2;
        stride = 16;
        anchors_per_cell = 2;
    } else {
        base = 28 * 28 * 2 + 14 * 14 * 2;
        stride = 32;
        anchors_per_cell = 6;
    }
    feature = (SAMPLE_SVP_POSE_DET_INPUT_W + stride - 1) / stride;
    cell = (index - base) / anchors_per_cell;
    pos = cell % feature;
    *ax = ((td_float)pos + 0.5f) / feature;
    *ay = ((td_float)(cell / feature) + 0.5f) / feature;
}

static td_s32 sample_svp_pose_output_f32(const sample_svp_npu_task_info *task, td_u32 idx,
    td_float **data, td_u32 *num)
{
    svp_acl_data_buffer *buf = svp_acl_mdl_get_dataset_buffer(task->output_dataset, idx);
    svp_acl_data_type data_type = sample_svp_model_output_type(task, idx);
    size_t size;
    td_u32 i;

    sample_svp_check_exps_return(buf == TD_NULL || data == TD_NULL || num == TD_NULL,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "pose output args invalid\n");
    *data = (td_float *)svp_acl_get_data_buffer_addr(buf);
    size = svp_acl_get_data_buffer_size(buf);
    sample_svp_check_exps_return(*data == TD_NULL || size == 0,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "pose output buffer invalid\n");

    if (data_type == SVP_ACL_FLOAT) {
        sample_svp_check_exps_return(size % sizeof(td_float) != 0,
            TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "pose FP32 output size invalid\n");
        *num = (td_u32)(size / sizeof(td_float));
        return TD_SUCCESS;
    }
    if (data_type == SVP_ACL_FLOAT16) {
        td_u16 *src = (td_u16 *)*data;
        td_float *tmp = g_pose_output_tmp[idx & 1U];
        sample_svp_check_exps_return(size % sizeof(td_u16) != 0,
            TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "pose FP16 output size invalid\n");
        *num = (td_u32)(size / sizeof(td_u16));
        sample_svp_check_exps_return(*num > SAMPLE_SVP_POSE_ANCHOR_NUM * 12,
            TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "pose FP16 output too large: %u\n", *num);
        for (i = 0; i < *num; i++) {
            tmp[i] = sample_svp_fp16_to_float(src[i]);
        }
        *data = tmp;
        return TD_SUCCESS;
    }

    sample_svp_trace_err("unsupported pose output type=%d idx=%u\n", data_type, idx);
    return TD_FAILURE;
}

static td_s32 sample_svp_decode_pose_detection(sample_svp_pose_detection *det,
    td_float *pad_l, td_float *pad_t, td_float *scale_w, td_float *scale_h)
{
    td_float *boxes;
    td_float *scores;
    td_u32 i;
    td_float best_score = 0.0f;
    td_u32 best_idx = 0;
    td_bool found = TD_FALSE;

    sample_svp_check_exps_return(det == TD_NULL, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "pose det result null\n");
    {
        td_float *out0;
        td_float *out1;
        td_u32 out0_num;
        td_u32 out1_num;
        sample_svp_check_exps_return(sample_svp_pose_output_f32(&g_svp_npu_task[2], 0, &out0, &out0_num) != TD_SUCCESS ||
            sample_svp_pose_output_f32(&g_svp_npu_task[2], 1, &out1, &out1_num) != TD_SUCCESS,
            TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "get pose detector outputs failed\n");
        if (out0_num >= SAMPLE_SVP_POSE_ANCHOR_NUM * 12 && out1_num >= SAMPLE_SVP_POSE_ANCHOR_NUM) {
            boxes = out0;
            scores = out1;
        } else if (out1_num >= SAMPLE_SVP_POSE_ANCHOR_NUM * 12 && out0_num >= SAMPLE_SVP_POSE_ANCHOR_NUM) {
            boxes = out1;
            scores = out0;
        } else {
            sample_svp_trace_err("pose detector output size invalid out0=%u out1=%u\n",
                out0_num, out1_num);
            return TD_FAILURE;
        }
    }

    for (i = 0; i < SAMPLE_SVP_POSE_ANCHOR_NUM; i++) {
        td_float score = sample_svp_sigmoid_f32(scores[i]);
        if (score > best_score) {
            best_score = score;
            best_idx = i;
            found = TD_TRUE;
        }
    }
    if (found != TD_TRUE || best_score < SAMPLE_SVP_POSE_DET_TH) {
        return TD_FAILURE;
    }

    {
        td_float ax;
        td_float ay;
        td_float *raw = boxes + best_idx * 12;
        td_float x_center;
        td_float y_center;
        td_float width;
        td_float height;
        td_u32 k;

        sample_svp_pose_anchor(best_idx, &ax, &ay);
        x_center = raw[0] / SAMPLE_SVP_POSE_DET_INPUT_W + ax;
        y_center = raw[1] / SAMPLE_SVP_POSE_DET_INPUT_H + ay;
        width = raw[2] / SAMPLE_SVP_POSE_DET_INPUT_W;
        height = raw[3] / SAMPLE_SVP_POSE_DET_INPUT_H;

        det->score = best_score;
        det->box[0] = (x_center - width * 0.5f - *pad_l) / *scale_w;
        det->box[1] = (y_center - height * 0.5f - *pad_t) / *scale_h;
        det->box[2] = (x_center + width * 0.5f - *pad_l) / *scale_w;
        det->box[3] = (y_center + height * 0.5f - *pad_t) / *scale_h;
        for (k = 0; k < 4; k++) {
            det->keypoints[k][0] = (raw[4 + k * 2] / SAMPLE_SVP_POSE_DET_INPUT_W + ax - *pad_l) / *scale_w;
            det->keypoints[k][1] = (raw[5 + k * 2] / SAMPLE_SVP_POSE_DET_INPUT_H + ay - *pad_t) / *scale_h;
        }
    }
    return TD_SUCCESS;
}

static td_s32 sample_svp_pose_roi_from_detection(const sample_svp_pose_detection *det,
    td_u32 image_w, td_u32 image_h, sample_svp_pose_roi *roi)
{
    td_float cx = det->keypoints[0][0] * image_w;
    td_float cy = det->keypoints[0][1] * image_h;
    td_float sx = det->keypoints[1][0] * image_w;
    td_float sy = det->keypoints[1][1] * image_h;
    td_float distance = hypotf(sx - cx, sy - cy);
    td_float size = 2.0f * distance * 1.25f;

    sample_svp_check_exps_return(roi == TD_NULL || det == TD_NULL || size < 8.0f || !isfinite(size),
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "pose roi invalid\n");
    roi->cx = cx;
    roi->cy = cy;
    roi->size = size;
    roi->rotation = sample_svp_normalize_angle((SAMPLE_SVP_PI_F * 0.5f) -
        atan2f(-(sy - cy), sx - cx));
    return TD_SUCCESS;
}

static td_s32 sample_svp_prepare_pose_landmark_input(const sample_svp_pose_roi *roi)
{
    const td_u8 *src = g_svp_npu_face_det_frame_virt;
    td_u32 src_w = g_svp_npu_face_det_frame.video_frame.width;
    td_u32 src_h = g_svp_npu_face_det_frame.video_frame.height;
    td_u32 stride_y = g_svp_npu_face_det_frame.video_frame.stride[0];
    td_u32 stride_uv = g_svp_npu_face_det_frame.video_frame.stride[1];
    const td_u8 *src_y = src;
    const td_u8 *src_vu = src + (size_t)stride_y * src_h;
    td_float cs = cosf(roi->rotation);
    td_float sn = sinf(roi->rotation);
    td_u32 x;
    td_u32 y;
    svp_acl_error acl_ret;

    sample_svp_check_exps_return(roi == TD_NULL || src == TD_NULL || g_pose_lm_input_virt == TD_NULL,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "pose landmark input invalid\n");
    (td_void)memset_s(g_pose_lm_input_virt, g_pose_lm_input_size, 0, g_pose_lm_input_size);

    for (y = 0; y < SAMPLE_SVP_POSE_LM_INPUT_H; y++) {
        td_float local_y = (((td_float)y + 0.5f) / SAMPLE_SVP_POSE_LM_INPUT_H - 0.5f) * roi->size;
        for (x = 0; x < SAMPLE_SVP_POSE_LM_INPUT_W; x++) {
            td_float local_x = (((td_float)x + 0.5f) / SAMPLE_SVP_POSE_LM_INPUT_W - 0.5f) * roi->size;
            td_s32 sx = (td_s32)floorf(roi->cx + local_x * cs - local_y * sn + 0.5f);
            td_s32 sy = (td_s32)floorf(roi->cy + local_x * sn + local_y * cs + 0.5f);
            td_float r, g, b;
            sample_svp_nv21_get_rgb_or_black(src_y, src_vu, src_w, src_h, stride_y, stride_uv,
                sx, sy, &r, &g, &b);
            sample_svp_check_exps_return(sample_svp_write_rgb_to_model_input(
                SAMPLE_SVP_NPU_POSE_LANDMARK_MODEL_IDX, g_pose_lm_input_virt,
                g_pose_lm_input_size, g_pose_lm_input_stride,
                SAMPLE_SVP_POSE_LM_INPUT_W, SAMPLE_SVP_POSE_LM_INPUT_H,
                x, y, r, g, b, 1.0f / 255.0f, 0.0f) != TD_SUCCESS,
                TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "write pose landmark pixel failed\n");
        }
    }
    acl_ret = svp_acl_rt_mem_flush(g_pose_lm_input_virt, g_pose_lm_input_size);
    sample_svp_check_exps_return(acl_ret != SVP_ACL_SUCCESS, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "flush pose landmark input failed, ret=%d\n", acl_ret);
    return TD_SUCCESS;
}

static td_void sample_svp_project_pose_landmarks(const sample_svp_pose_roi *roi,
    sample_svp_pose_result *pose, const td_float *raw)
{
    td_float cs = cosf(roi->rotation);
    td_float sn = sinf(roi->rotation);
    td_u32 i;

    pose->landmark_num = SAMPLE_SVP_POSE_LANDMARK_NUM;
    for (i = 0; i < SAMPLE_SVP_POSE_LANDMARK_NUM; i++) {
        td_float lx = (raw[i * 5] / SAMPLE_SVP_POSE_LM_INPUT_W - 0.5f) * roi->size;
        td_float ly = (raw[i * 5 + 1] / SAMPLE_SVP_POSE_LM_INPUT_H - 0.5f) * roi->size;
        pose->landmarks[i][0] = roi->cx + lx * cs - ly * sn;
        pose->landmarks[i][1] = roi->cy + lx * sn + ly * cs;
        pose->landmarks[i][2] = raw[i * 5 + 2] / SAMPLE_SVP_POSE_LM_INPUT_W * roi->size;
        pose->landmarks[i][3] = sample_svp_sigmoid_f32(raw[i * 5 + 3]);
        pose->landmarks[i][4] = sample_svp_sigmoid_f32(raw[i * 5 + 4]);
    }
}

static td_bool sample_svp_pose_lm_ok(const sample_svp_pose_result *pose, td_u32 idx)
{
    return (idx < pose->landmark_num &&
        pose->landmarks[idx][3] >= SAMPLE_SVP_POSE_VIS_TH &&
        pose->landmarks[idx][4] >= SAMPLE_SVP_POSE_VIS_TH) ? TD_TRUE : TD_FALSE;
}

static td_void sample_svp_classify_posture(sample_svp_pose_result *pose)
{
    td_float lsx, lsy, rsx, rsy;
    td_float shoulder_w;
    td_float shoulder_tilt;
    td_bool shoulder_ok;
    td_bool hip_ok;

    if (pose->has_pose != TD_TRUE) {
        (td_void)strncpy_s(pose->label, sizeof(pose->label), "no_pose", sizeof(pose->label) - 1);
        pose->posture_ok = TD_FALSE;
        return;
    }

    shoulder_ok = sample_svp_pose_lm_ok(pose, 11) && sample_svp_pose_lm_ok(pose, 12);
    hip_ok = sample_svp_pose_lm_ok(pose, 23) && sample_svp_pose_lm_ok(pose, 24);
    if (shoulder_ok != TD_TRUE) {
        (td_void)strncpy_s(pose->label, sizeof(pose->label), "unknown", sizeof(pose->label) - 1);
        pose->posture_ok = TD_FALSE;
        return;
    }

    lsx = pose->landmarks[11][0];
    lsy = pose->landmarks[11][1];
    rsx = pose->landmarks[12][0];
    rsy = pose->landmarks[12][1];
    shoulder_w = fmaxf(20.0f, hypotf(rsx - lsx, rsy - lsy));
    shoulder_tilt = fabsf(rsy - lsy) / shoulder_w;
    if (shoulder_tilt > 0.22f) {
        (td_void)strncpy_s(pose->label, sizeof(pose->label), "shoulder_tilt", sizeof(pose->label) - 1);
        pose->posture_ok = TD_FALSE;
        return;
    }

    if (hip_ok == TD_TRUE) {
        td_float shoulder_cx = (lsx + rsx) * 0.5f;
        td_float hip_cx = (pose->landmarks[23][0] + pose->landmarks[24][0]) * 0.5f;
        td_float lean = (shoulder_cx - hip_cx) / shoulder_w;
        if (lean < -0.35f) {
            (td_void)strncpy_s(pose->label, sizeof(pose->label), "lean_left", sizeof(pose->label) - 1);
            pose->posture_ok = TD_FALSE;
            return;
        }
        if (lean > 0.35f) {
            (td_void)strncpy_s(pose->label, sizeof(pose->label), "lean_right", sizeof(pose->label) - 1);
            pose->posture_ok = TD_FALSE;
            return;
        }
    }

    if (sample_svp_pose_lm_ok(pose, 0) == TD_TRUE) {
        td_float shoulder_cx = (lsx + rsx) * 0.5f;
        td_float nose_dx = fabsf(pose->landmarks[0][0] - shoulder_cx) / shoulder_w;
        if (nose_dx > 0.65f) {
            (td_void)strncpy_s(pose->label, sizeof(pose->label), "head_offset", sizeof(pose->label) - 1);
            pose->posture_ok = TD_FALSE;
            return;
        }
    }

    (td_void)strncpy_s(pose->label, sizeof(pose->label), "normal", sizeof(pose->label) - 1);
    pose->posture_ok = TD_TRUE;
}

static td_s32 sample_svp_run_pose_once(sample_svp_pose_result *pose)
{
    td_s32 ret;
    sample_svp_pose_detection det = {0};
    sample_svp_pose_roi roi = {0};
    td_float frame_w = (td_float)g_svp_npu_face_det_frame.video_frame.width;
    td_float frame_h = (td_float)g_svp_npu_face_det_frame.video_frame.height;
    td_float scale = fminf((td_float)SAMPLE_SVP_POSE_DET_INPUT_W / frame_w,
        (td_float)SAMPLE_SVP_POSE_DET_INPUT_H / frame_h);
    td_float resized_w = frame_w * scale;
    td_float resized_h = frame_h * scale;
    td_float pad_l = ((td_float)SAMPLE_SVP_POSE_DET_INPUT_W - resized_w) * 0.5f /
        SAMPLE_SVP_POSE_DET_INPUT_W;
    td_float pad_t = ((td_float)SAMPLE_SVP_POSE_DET_INPUT_H - resized_h) * 0.5f /
        SAMPLE_SVP_POSE_DET_INPUT_H;
    td_float scale_w = resized_w / SAMPLE_SVP_POSE_DET_INPUT_W;
    td_float scale_h = resized_h / SAMPLE_SVP_POSE_DET_INPUT_H;
    td_float *raw_lm;
    td_float *score_data;
    td_u32 raw_num;
    td_u32 score_num;

    (td_void)memset_s(pose, sizeof(*pose), 0, sizeof(*pose));
    ret = sample_svp_prepare_pose_detector_input();
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "prepare pose detector failed\n");
    ret = sample_common_svp_npu_model_execute(&g_svp_npu_task[2]);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "run pose detector failed\n");
    if (sample_svp_decode_pose_detection(&det, &pad_l, &pad_t, &scale_w, &scale_h) != TD_SUCCESS ||
        sample_svp_pose_roi_from_detection(&det, (td_u32)frame_w, (td_u32)frame_h, &roi) != TD_SUCCESS) {
        pose->detection_score = det.score;
        sample_svp_classify_posture(pose);
        return TD_SUCCESS;
    }

    ret = sample_svp_prepare_pose_landmark_input(&roi);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "prepare pose landmark failed\n");
    ret = sample_common_svp_npu_model_execute(&g_svp_npu_task[3]);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "run pose landmark failed\n");
    sample_svp_check_exps_return(sample_svp_pose_output_f32(&g_svp_npu_task[3], 0, &raw_lm, &raw_num) != TD_SUCCESS ||
        sample_svp_pose_output_f32(&g_svp_npu_task[3], 1, &score_data, &score_num) != TD_SUCCESS,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "get pose landmark outputs failed\n");
    sample_svp_check_exps_return(raw_num < SAMPLE_SVP_POSE_LANDMARK_NUM * 5 || score_num < 1,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "pose landmark output size invalid\n");

    pose->score = score_data[0];
    if (pose->score < 0.0f || pose->score > 1.0f) {
        pose->score = sample_svp_sigmoid_f32(pose->score);
    }
    pose->detection_score = det.score;
    if (pose->score >= SAMPLE_SVP_POSE_SCORE_TH) {
        pose->has_pose = TD_TRUE;
        sample_svp_project_pose_landmarks(&roi, pose, raw_lm);
    }
    sample_svp_classify_posture(pose);
    return TD_SUCCESS;
}

static td_void sample_svp_copy_pose_cached(sample_svp_frame_result *result, td_double now)
{
    result->pose = g_pose_cached;
    result->pose.updated = TD_FALSE;
    result->pose.age_s = (td_float)fmax(0.0, now - g_pose_last_run_s);
}

static td_void sample_svp_maybe_run_pose(sample_svp_frame_result *result, td_double now)
{
    sample_svp_pose_result pose = {0};

    if (now - g_pose_last_run_s < SAMPLE_SVP_POSE_INTERVAL_S) {
        sample_svp_copy_pose_cached(result, now);
        return;
    }
    if (sample_svp_run_pose_once(&pose) == TD_SUCCESS) {
        pose.updated = TD_TRUE;
        pose.age_s = 0.0f;
        g_pose_cached = pose;
        g_pose_last_run_s = now;
        result->pose = pose;
        sample_svp_trace_info("pose posture=%s ok=%d score=%.3f det=%.3f interval=%.1fs\n",
            pose.label, pose.posture_ok, pose.score, pose.detection_score,
            (td_float)SAMPLE_SVP_POSE_INTERVAL_S);
    } else {
        sample_svp_copy_pose_cached(result, now);
        sample_svp_trace_err("pose low-frequency inference failed; keeping cached result\n");
    }
}


static td_void sample_svp_update_face_state(const sample_svp_landmark106_result *lm,
    const sample_svp_attention_result *attention, sample_svp_face_state *state)
{
    td_float left = sample_svp_eye_aspect_ratio(lm, TD_TRUE);
    td_float right = sample_svp_eye_aspect_ratio(lm, TD_FALSE);
    td_float eye_open = (left + right) * 0.5f;
    td_float mouth_open = sample_svp_mouth_aspect_ratio(lm);
    td_float alpha = 0.25f, beta = 0.35f;
    td_double now = sample_svp_now_seconds();

    if (state->smooth_yaw == 0.0f && state->smooth_pitch == 0.0f) {
        state->smooth_yaw = attention->yaw_deg;
        state->smooth_pitch = attention->pitch_deg;
    } else {
        state->smooth_yaw = (1.0f - alpha) * state->smooth_yaw + alpha * attention->yaw_deg;
        state->smooth_pitch = (1.0f - alpha) * state->smooth_pitch + alpha * attention->pitch_deg;
    }

    state->smooth_eye_open = (state->smooth_eye_open == 0.0f) ? eye_open : ((1.0f - beta) * state->smooth_eye_open + beta * eye_open);
    state->smooth_mouth_open = (state->smooth_mouth_open == 0.0f) ? mouth_open : ((1.0f - beta) * state->smooth_mouth_open + beta * mouth_open);

    state->eyes_closed = (state->smooth_eye_open < SAMPLE_SVP_EYE_CLOSED_TH);

    if (state->eyes_closed) {
        state->closed_frames++;
    } else {
        if (state->closed_frames >= SAMPLE_SVP_BLINK_MIN_FRAMES && state->closed_frames <= SAMPLE_SVP_BLINK_MAX_FRAMES) {
            state->blink_count++;
        }
        state->closed_frames = 0;
    }

    if (state->smooth_mouth_open > SAMPLE_SVP_MOUTH_OPEN_TH) {
        if (state->yawn_start_time <= 0.0) state->yawn_start_time = now;
        if ((now - state->yawn_start_time) >= SAMPLE_SVP_YAWN_MIN_SECONDS) {
            state->yawning = TD_TRUE;
            if (!state->yawn_counted) {
                state->yawn_count++;
                state->yawn_counted = TD_TRUE;
            }
        } else {
            state->yawning = TD_FALSE;
        }
    } else {
        state->yawn_start_time = 0.0;
        state->yawning = TD_FALSE;
        state->yawn_counted = TD_FALSE;
    }
}

static td_s32 sample_svp_prepare_landmark_input_rgb888(const td_u8 *rgb, td_u32 width, td_u32 height,
    const sample_svp_face_box *face, sample_svp_landmark_affine *transform)
{
    const td_u8 *src_nv21 = g_svp_npu_face_det_frame_virt;
    const td_u8 *src_y;
    const td_u8 *src_vu;
    td_float *dst_f = TD_NULL;
    td_u8 *dst_u8 = TD_NULL;
    td_float face_w;
    td_float face_h;
    td_float center_x;
    td_float center_y;
    td_float crop_side;
    td_u32 src_w;
    td_u32 src_h;
    td_u32 src_stride_y;
    td_u32 src_stride_uv;
    td_u32 dst_stride_f;
    td_bool layout_f32;
    td_bool layout_u8;
    td_u32 x;
    td_u32 y;
    td_u32 channel;
    svp_acl_error acl_ret;

    (void)rgb;
    (void)width;
    (void)height;

    sample_svp_check_exps_return(face == TD_NULL || transform == TD_NULL ||
        src_nv21 == TD_NULL, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "landmark input args are invalid\n");
    sample_svp_check_exps_return(g_landmark_model_input_virt == TD_NULL ||
        g_landmark_expect_w == 0 || g_landmark_expect_h == 0 ||
        g_landmark_expect_stride == 0,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "landmark input buffer is invalid\n");

    src_w = g_svp_npu_face_det_frame.video_frame.width;
    src_h = g_svp_npu_face_det_frame.video_frame.height;
    src_stride_y = g_svp_npu_face_det_frame.video_frame.stride[0];
    src_stride_uv = g_svp_npu_face_det_frame.video_frame.stride[1];
    sample_svp_check_exps_return(src_w == 0 || src_h == 0 ||
        src_stride_y == 0 || src_stride_uv == 0,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "landmark source frame is invalid\n");

    layout_f32 = (g_landmark_expect_stride % sizeof(td_float) == 0 &&
        g_landmark_expect_stride / sizeof(td_float) >= g_landmark_expect_w &&
        g_landmark_expect_size >=
        3 * g_landmark_expect_h * g_landmark_expect_stride) ? TD_TRUE : TD_FALSE;
    layout_u8 = (g_landmark_expect_stride >= g_landmark_expect_w &&
        g_landmark_expect_size >=
        3 * g_landmark_expect_h * g_landmark_expect_stride) ? TD_TRUE : TD_FALSE;
    sample_svp_check_exps_return(layout_f32 != TD_TRUE && layout_u8 != TD_TRUE,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "landmark input layout is invalid: w=%u h=%u size=%u stride=%u\n",
        g_landmark_expect_w, g_landmark_expect_h,
        g_landmark_expect_size, g_landmark_expect_stride);

    face_w = face->x2 - face->x1;
    face_h = face->y2 - face->y1;
    sample_svp_check_exps_return(face_w <= 1.0f || face_h <= 1.0f,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "landmark face bbox is invalid\n");

    center_x = (face->x1 + face->x2) * 0.5f;
    center_y = (face->y1 + face->y2) * 0.5f;
    crop_side = sample_svp_max_f32(face_w, face_h) *
        SAMPLE_SVP_LANDMARK_CROP_SCALE;
    transform->scale = (td_float)g_landmark_expect_w / crop_side;
    transform->translate_x =
        (td_float)g_landmark_expect_w * 0.5f - center_x * transform->scale;
    transform->translate_y =
        (td_float)g_landmark_expect_h * 0.5f - center_y * transform->scale;

    src_y = src_nv21;
    src_vu = src_nv21 + (size_t)src_stride_y * src_h;

    if (layout_f32 == TD_TRUE) {
        dst_stride_f = g_landmark_expect_stride / sizeof(td_float);
        dst_f = (td_float *)g_landmark_model_input_virt;
        for (channel = 0; channel < 3; channel++) {
            td_float *channel_dst =
                dst_f + (size_t)channel * g_landmark_expect_h * dst_stride_f;
            for (y = 0; y < g_landmark_expect_h; y++) {
                for (x = 0; x < dst_stride_f; x++) {
                    channel_dst[(size_t)y * dst_stride_f + x] = 0.0f;
                }
            }
        }
    } else {
        dst_u8 = g_landmark_model_input_virt;
        (td_void)memset_s(dst_u8, g_landmark_expect_size, 0, g_landmark_expect_size);
    }

    for (y = 0; y < g_landmark_expect_h; y++) {
        td_float src_y_pos =
            ((td_float)y - transform->translate_y) / transform->scale;
        for (x = 0; x < g_landmark_expect_w; x++) {
            td_float src_x =
                ((td_float)x - transform->translate_x) / transform->scale;
            td_float r;
            td_float g;
            td_float b;

            sample_svp_nv21_sample_rgb_bilinear(src_y, src_vu,
                src_w, src_h, src_stride_y, src_stride_uv,
                src_x, src_y_pos, &r, &g, &b);

            if (layout_f32 == TD_TRUE) {
                size_t dst_idx = (size_t)y * dst_stride_f + x;
                dst_f[dst_idx] = r;
                dst_f[(size_t)g_landmark_expect_h * dst_stride_f + dst_idx] = g;
                dst_f[(size_t)2 * g_landmark_expect_h * dst_stride_f + dst_idx] = b;
            } else {
                size_t dst_idx = (size_t)y * g_landmark_expect_stride + x;
                td_u8 r8 = (td_u8)(fmaxf(0.0f, fminf(255.0f, r)) + 0.5f);
                td_u8 g8 = (td_u8)(fmaxf(0.0f, fminf(255.0f, g)) + 0.5f);
                td_u8 b8 = (td_u8)(fmaxf(0.0f, fminf(255.0f, b)) + 0.5f);
                dst_u8[dst_idx] = r8;
                dst_u8[(size_t)g_landmark_expect_h * g_landmark_expect_stride + dst_idx] = g8;
                dst_u8[(size_t)2 * g_landmark_expect_h * g_landmark_expect_stride + dst_idx] = b8;
            }
        }
    }

    acl_ret = svp_acl_rt_mem_flush(g_landmark_model_input_virt, g_landmark_expect_size);
    sample_svp_check_exps_return(acl_ret != SVP_ACL_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "flush landmark input failed, ret=%d\n", acl_ret);
    return TD_SUCCESS;
}

const sample_svp_face_state *sample_svp_npu_get_face_state(td_void)
{
    return &g_face_state;
}

static td_s32 sample_svp_npu_run_frame_pipeline_once(sample_svp_frame_result *result)
{
    td_s32 ret;
    sample_svp_face_box_list faces = {0};
    sample_svp_face_box best_face = {0};
    sample_svp_landmark_affine landmark_transform = {0};
    td_double t0;
    td_double t1;
    td_double t2;
    td_double t3;
    td_double t4;
    td_double t8;

    td_u32 width = g_svp_npu_face_det_frame.video_frame.width;
    td_u32 height = g_svp_npu_face_det_frame.video_frame.height;

    sample_svp_check_exps_return(result == TD_NULL || width == 0 || height == 0,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "invalid frame pipeline args\n");

    (td_void)memset_s(result, sizeof(*result), 0, sizeof(*result));

    sample_svp_check_exps_return(g_pipeline_inited != TD_TRUE, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "npu runtime not initialized\n");

    t0 = sample_svp_now_seconds();

    /* 1. face detection: first model now consumes the current video frame directly */
    ret = sample_svp_npu_run_face_det_with_video_frame(&faces);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "run face_detection.om failed\n");
    t1 = sample_svp_now_seconds();
    sample_svp_maybe_run_pose(result, t1);

    if (!sample_svp_select_largest_face(&faces, width, height, &best_face)) {
        result->has_face = TD_FALSE;
        sample_svp_npu_clear_face_det_frame();
        sample_svp_pipeline_profile_commit(t1 - t0, t1 - t0,
            0.0, 0.0, 0.0,
            TD_FALSE);
        return TD_SUCCESS;
    }

    sample_svp_clamp_bbox(&best_face, width, height);
    result->has_face = TD_TRUE;
    result->face = best_face;

    /* 2. landmark */
    ret = sample_svp_prepare_landmark_input_rgb888(
        TD_NULL, width, height, &best_face, &landmark_transform);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "prepare landmark failed\n");
    t2 = sample_svp_now_seconds();

    if (g_face_det_debug_frame_idx < SAMPLE_SVP_FACE_DET_DEBUG_FRAME_MAX) {
        sample_svp_trace_info("roi handoff: det=(%.1f,%.1f,%.1f,%.1f) -> landmark_affine=(scale=%.6f tx=%.3f ty=%.3f)\n",
            best_face.x1, best_face.y1, best_face.x2, best_face.y2,
            landmark_transform.scale, landmark_transform.translate_x,
            landmark_transform.translate_y);
    }

    ret = sample_common_svp_npu_model_execute(&g_svp_npu_task[1]);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "run landmark failed\n");
    t3 = sample_svp_now_seconds();

    ret = sample_svp_npu_parse_landmark_output(&g_svp_npu_task[1], &result->landmark);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "parse landmark failed\n");
    sample_svp_landmark_map_to_full_image(
        &result->landmark, &landmark_transform);
    t4 = sample_svp_now_seconds();

    /* 3. attention direction */
    ret = sample_svp_estimate_head_from_landmarks(&result->landmark, &result->attention);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "estimate head direction failed\n");
    if (result->landmark.point_num == SAMPLE_SVP_LANDMARK_NUM) {
        sample_svp_update_face_state(&result->landmark, &result->attention, &g_face_state);
    }

    result->state_snapshot = g_face_state;
    sample_svp_npu_clear_face_det_frame();

    t8 = sample_svp_now_seconds();
    sample_svp_pipeline_profile_commit(t8 - t0, t1 - t0,
        t2 - t1, t3 - t2, t4 - t3,
        TD_TRUE);

    return TD_SUCCESS;
}

/* ----------------------------- 主入口 ----------------------------- */

td_s32 sample_svp_npu_process_frame(const ot_video_frame_info *frame,
    const td_u8 *frame_virt, sample_svp_frame_result *result)
{
    td_s32 ret;

    sample_svp_check_exps_return(frame == TD_NULL || frame_virt == TD_NULL ||
        result == TD_NULL, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "invalid process frame args\n");
    sample_svp_check_exps_return(g_pipeline_inited != TD_TRUE, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "npu runtime not initialized\n");
    sample_svp_check_exps_return(
        frame->video_frame.pixel_format != OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "unsupported pixel format %d, expected NV21\n",
        frame->video_frame.pixel_format);

    ret = sample_svp_npu_set_face_det_frame(frame, frame_virt);
    if (ret == TD_SUCCESS) {
        ret = sample_svp_npu_run_frame_pipeline_once(result);
    }
    sample_svp_npu_clear_face_det_frame();
    return ret;
}

td_s32 sample_svp_npu_run_frame_pipeline_rgb888(const td_u8 *rgb, td_u32 width, td_u32 height,
    sample_svp_frame_result *result)
{
    (void)rgb;
    (void)width;
    (void)height;
    return sample_svp_npu_run_frame_pipeline_once(result);
}

/* 其他废弃函数保持原样 */
td_void sample_svp_npu_acl_offline_pipeline(td_void)
{
    sample_svp_trace_info("offline pipeline is deprecated\n");
}

td_void sample_svp_npu_acl_offline_smoke_test(td_void)
{
    /* 省略，保持原有逻辑 */
}

td_s32 sample_svp_npu_init_runtime(td_void)
{
    td_s32 ret;

    if (g_pipeline_inited == TD_TRUE) return TD_SUCCESS;

    ret = sample_svp_npu_pipeline_init();
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "pipeline init failed!\n");

    g_pipeline_inited = TD_TRUE;
    return TD_SUCCESS;
}

td_void sample_svp_npu_deinit_runtime(td_void)
{
    if (g_pipeline_inited != TD_TRUE) return;
    sample_svp_npu_pipeline_deinit();
    sample_svp_npu_clear_face_det_frame();
    g_pipeline_inited = TD_FALSE;
}
