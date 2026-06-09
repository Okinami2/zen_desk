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

#define SAMPLE_SVP_NPU_OFFLINE_TASK_NUM      3
#define SAMPLE_SVP_NPU_FACE_DET_MODEL_IDX    0
#define SAMPLE_SVP_NPU_LANDMARK_MODEL_IDX    1
#define SAMPLE_SVP_NPU_GAZE_MODEL_IDX        2

#define SAMPLE_SVP_NPU_INPUT_FILE_NUM_ONE    1
#define SAMPLE_SVP_NPU_PATH_LEN              256
#define SAMPLE_SVP_NPU_MAX_FACE_NUM          16

#define SAMPLE_SVP_NPU_FACE_DET_MODEL_PATH   "./data/model/face_detection.om"
#define SAMPLE_SVP_NPU_LANDMARK_MODEL_PATH   "./data/model/landmark106.om"
#define SAMPLE_SVP_NPU_GAZE_MODEL_PATH       "./data/model/gaze.om"

#define SAMPLE_SVP_LANDMARK_INPUT_BIN_PATH   "./data/input/landmark_input.bin"
#define SAMPLE_SVP_GAZE_INPUT_BIN_PATH       "./data/input/gaze_input.bin"

#define SAMPLE_SVP_EYE_CLOSED_TH      0.19f
#define SAMPLE_SVP_MOUTH_OPEN_TH      0.28f
#define SAMPLE_SVP_BLINK_MIN_FRAMES   2
#define SAMPLE_SVP_BLINK_MAX_FRAMES   8
#define SAMPLE_SVP_YAWN_MIN_SECONDS   0.8

#define SAMPLE_SVP_LANDMARK_IN_W      192
#define SAMPLE_SVP_LANDMARK_IN_H      192
#define SAMPLE_SVP_GAZE_IN_W          448
#define SAMPLE_SVP_GAZE_IN_H          448

#define SAMPLE_SVP_LANDMARK_NUM       106

#define SAMPLE_SVP_GAZE_CROP_SCALE    1.25f
#define SAMPLE_SVP_LANDMARK_CROP_SCALE 1.50f

typedef struct {
    td_u32 num;
    sample_svp_face_box boxes[SAMPLE_SVP_NPU_MAX_FACE_NUM];
} sample_svp_face_box_list;

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
static td_u32 g_gaze_expect_w = 0;
static td_u32 g_gaze_expect_h = 0;
static td_u32 g_gaze_expect_size = 0;
static td_u32 g_gaze_expect_stride = 0;
static td_u8 *g_gaze_model_input_virt = TD_NULL;

typedef struct {
    td_u64 frame_cnt;
    td_u64 face_cnt;
    td_double win_start;
    td_double sum_total;
    td_double sum_face_det;
    td_double sum_lm_prep;
    td_double sum_lm_infer;
    td_double sum_lm_parse_map;
    td_double sum_gaze_prep;
    td_double sum_gaze_infer;
    td_double sum_gaze_parse;
    td_double max_total;
    td_double max_face_det;
    td_double max_lm_infer;
    td_double max_gaze_infer;
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

static td_float sample_svp_min_f32(td_float a, td_float b)
{
    return (a < b) ? a : b;
}

static td_void sample_svp_expand_square_bbox(sample_svp_face_box *box, td_u32 width, td_u32 height,
    td_float scale)
{
    td_float cx, cy, side;

    if (box == TD_NULL) {
        return;
    }

    cx = (box->x1 + box->x2) * 0.5f;
    cy = (box->y1 + box->y2) * 0.5f;
    side = sample_svp_max_f32(box->x2 - box->x1, box->y2 - box->y1) * scale;
    side = sample_svp_max_f32(side, 2.0f);

    box->x1 = cx - side * 0.5f;
    box->y1 = cy - side * 0.5f;
    box->x2 = cx + side * 0.5f;
    box->y2 = cy + side * 0.5f;

    if (box->x1 < 0.0f) {
        box->x2 -= box->x1;
        box->x1 = 0.0f;
    }
    if (box->y1 < 0.0f) {
        box->y2 -= box->y1;
        box->y1 = 0.0f;
    }
    if (box->x2 > (td_float)width) {
        td_float diff = box->x2 - (td_float)width;
        box->x1 -= diff;
        box->x2 = (td_float)width;
    }
    if (box->y2 > (td_float)height) {
        td_float diff = box->y2 - (td_float)height;
        box->y1 -= diff;
        box->y2 = (td_float)height;
    }

    box->x1 = sample_svp_max_f32(0.0f, box->x1);
    box->y1 = sample_svp_max_f32(0.0f, box->y1);
    box->x2 = sample_svp_min_f32((td_float)width, box->x2);
    box->y2 = sample_svp_min_f32((td_float)height, box->y2);
}

static td_void sample_svp_make_square_bbox_unclipped(sample_svp_face_box *box, td_float scale)
{
    td_float cx;
    td_float cy;
    td_float side;

    if (box == TD_NULL) {
        return;
    }

    cx = (box->x1 + box->x2) * 0.5f;
    cy = (box->y1 + box->y2) * 0.5f;
    side = sample_svp_max_f32(box->x2 - box->x1, box->y2 - box->y1) * scale;
    side = sample_svp_max_f32(side, 2.0f);
    box->x1 = cx - side * 0.5f;
    box->y1 = cy - side * 0.5f;
    box->x2 = cx + side * 0.5f;
    box->y2 = cy + side * 0.5f;
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

static td_s32 sample_svp_crop_resize_rgb888_to_fp32_nchw(const td_u8 *rgb, td_u32 width, td_u32 height,
    const sample_svp_face_box *crop_box, td_u32 target_w, td_u32 target_h,
    td_u8 *dst_input_virt, td_u32 dst_input_size, td_float pixel_scale, td_bool bilinear)
{
    const td_u8 *src_nv21 = g_svp_npu_face_det_frame_virt;
    const td_u8 *src_y = TD_NULL;
    const td_u8 *src_vu = TD_NULL;
    td_u32 src_w;
    td_u32 src_h;
    td_u32 src_stride_y;
    td_u32 src_stride_uv;
    td_float *dst = TD_NULL;
    td_u32 y, x;
    td_float crop_w;
    td_float crop_h;

    sample_svp_check_exps_return(src_nv21 == TD_NULL || crop_box == TD_NULL || dst_input_virt == TD_NULL,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "invalid crop args\n");

    src_w = g_svp_npu_face_det_frame.video_frame.width;
    src_h = g_svp_npu_face_det_frame.video_frame.height;
    src_stride_y = g_svp_npu_face_det_frame.video_frame.stride[0];
    src_stride_uv = g_svp_npu_face_det_frame.video_frame.stride[1];
    sample_svp_check_exps_return(src_w == 0 || src_h == 0 || src_stride_y == 0 || src_stride_uv == 0,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "invalid source frame meta for crop\n");

    src_y = src_nv21;
    src_vu = src_nv21 + (size_t)src_stride_y * src_h;
    sample_svp_check_exps_return(dst_input_size < target_w * target_h * 3 * (td_u32)sizeof(td_float),
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "input buffer too small for crop\n");
    dst = (td_float *)dst_input_virt;

    crop_w = sample_svp_max_f32(1.0f, crop_box->x2 - crop_box->x1);
    crop_h = sample_svp_max_f32(1.0f, crop_box->y2 - crop_box->y1);

    for (y = 0; y < target_h; y++) {
        td_float sy = crop_box->y1 + (td_float)y * crop_h / (td_float)target_h;
        for (x = 0; x < target_w; x++) {
            td_float sx = crop_box->x1 + (td_float)x * crop_w / (td_float)target_w;
            td_float r;
            td_float g;
            td_float b;

            if (bilinear == TD_TRUE) {
                sample_svp_nv21_sample_rgb_bilinear(src_y, src_vu, src_w, src_h,
                    src_stride_y, src_stride_uv, sx, sy, &r, &g, &b);
            } else {
                sample_svp_nv21_get_rgb_or_black(src_y, src_vu, src_w, src_h,
                    src_stride_y, src_stride_uv, (td_s32)floorf(sx), (td_s32)floorf(sy),
                    &r, &g, &b);
            }

            dst[0 * target_h * target_w + y * target_w + x] = (td_float)r * pixel_scale;
            dst[1 * target_h * target_w + y * target_w + x] = (td_float)g * pixel_scale;
            dst[2 * target_h * target_w + y * target_w + x] = (td_float)b * pixel_scale;
        }
    }

    return TD_SUCCESS;
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

    ret = sample_common_svp_npu_load_model(SAMPLE_SVP_NPU_GAZE_MODEL_PATH,
        SAMPLE_SVP_NPU_GAZE_MODEL_IDX, TD_TRUE);
    if (ret != TD_SUCCESS) {
        sample_svp_trace_err("load gaze model failed!\n");
        (td_void)sample_common_svp_npu_unload_model(SAMPLE_SVP_NPU_LANDMARK_MODEL_IDX);
        (td_void)sample_common_svp_npu_unload_model(SAMPLE_SVP_NPU_FACE_DET_MODEL_IDX);
        return TD_FAILURE;
    }
    sample_svp_trace_info("Gaze model loaded, id=%u\n", SAMPLE_SVP_NPU_GAZE_MODEL_IDX);

    return TD_SUCCESS;
}

static td_void sample_svp_npu_pipeline_unload_models(td_void)
{
    (td_void)sample_common_svp_npu_unload_model(SAMPLE_SVP_NPU_GAZE_MODEL_IDX);
    (td_void)sample_common_svp_npu_unload_model(SAMPLE_SVP_NPU_LANDMARK_MODEL_IDX);
    (td_void)sample_common_svp_npu_unload_model(SAMPLE_SVP_NPU_FACE_DET_MODEL_IDX);
}

static td_s32 sample_svp_npu_pipeline_init(td_void)
{
    td_s32 ret;
    const td_char *acl_config_path = "";
    ot_size det_input_size = {0};
    ot_size landmark_input_size = {0};
    ot_size gaze_input_size = {0};
    td_u8 *det_input_virt = TD_NULL;
    td_u8 *landmark_input_virt = TD_NULL;
    td_u8 *gaze_input_virt = TD_NULL;
    td_u32 det_input_size_bytes = 0;
    td_u32 landmark_input_size_bytes = 0;
    td_u32 gaze_input_size_bytes = 0;
    td_u32 det_input_stride = 0;
    td_u32 landmark_input_stride = 0;
    td_u32 gaze_input_stride = 0;

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
    sample_svp_npu_acl_set_task_info(2, SAMPLE_SVP_NPU_GAZE_MODEL_IDX, TD_TRUE);

    ret = sample_svp_npu_acl_init_task(SAMPLE_SVP_NPU_OFFLINE_TASK_NUM);
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

    ret = sample_common_svp_npu_get_input_resolution(SAMPLE_SVP_NPU_GAZE_MODEL_IDX, 0, &gaze_input_size);
    if (ret != TD_SUCCESS) {
        sample_svp_trace_err("get gaze input resolution failed\n");
        goto init_fail;
    }

    ret = sample_common_svp_npu_get_input_data_buffer_info(&g_svp_npu_task[2], 0,
        &gaze_input_virt, &gaze_input_size_bytes, &gaze_input_stride);
    if (ret != TD_SUCCESS) {
        sample_svp_trace_err("get gaze input data buffer info failed\n");
        goto init_fail;
    }

    g_gaze_expect_w = gaze_input_size.width;
    g_gaze_expect_h = gaze_input_size.height;
    g_gaze_expect_size = gaze_input_size_bytes;
    g_gaze_expect_stride = gaze_input_stride;
    g_gaze_model_input_virt = gaze_input_virt;

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

    return TD_SUCCESS;

init_fail:
    if (g_face_det_resized_buf != TD_NULL) {
        free(g_face_det_resized_buf);
        g_face_det_resized_buf = TD_NULL;
    }
    g_face_det_model_input_virt = TD_NULL;
    g_landmark_model_input_virt = TD_NULL;
    g_gaze_model_input_virt = TD_NULL;
    g_face_det_expect_w = 0;
    g_face_det_expect_h = 0;
    g_face_det_expect_size = 0;
    g_face_det_expect_stride = 0;
    sample_svp_npu_acl_deinit_task(SAMPLE_SVP_NPU_OFFLINE_TASK_NUM);
    sample_svp_npu_pipeline_unload_models();
    sample_svp_npu_acl_deinit();
    return TD_FAILURE;
}

static td_void sample_svp_npu_pipeline_deinit(td_void)
{
    sample_svp_npu_acl_deinit_task(SAMPLE_SVP_NPU_OFFLINE_TASK_NUM);
    sample_svp_npu_pipeline_unload_models();
    sample_svp_npu_acl_deinit();
    sample_svp_npu_acl_terminate();

    if (g_face_det_resized_buf != TD_NULL) {
        free(g_face_det_resized_buf);
        g_face_det_resized_buf = TD_NULL;
    }

    g_face_det_model_input_virt = TD_NULL;
    g_landmark_model_input_virt = TD_NULL;
    g_gaze_model_input_virt = TD_NULL;
    g_face_det_expect_w = 0;
    g_face_det_expect_h = 0;
    g_face_det_expect_size = 0;
    g_face_det_expect_stride = 0;
    g_landmark_expect_w = 0;
    g_landmark_expect_h = 0;
    g_landmark_expect_size = 0;
    g_landmark_expect_stride = 0;
    g_gaze_expect_w = 0;
    g_gaze_expect_h = 0;
    g_gaze_expect_size = 0;
    g_gaze_expect_stride = 0;
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
    const sample_svp_face_box *face)
{
    td_u32 i;
    td_float face_w, face_h;

    if (lm == TD_NULL || face == TD_NULL || lm->point_num != SAMPLE_SVP_LANDMARK_NUM) return;

    face_w = face->x2 - face->x1;
    face_h = face->y2 - face->y1;
    if (face_w <= 1e-6f || face_h <= 1e-6f) return;

    for (i = 0; i < lm->point_num; i++) {
        lm->points[i][0] = face->x1 + (lm->points[i][0] / (td_float)SAMPLE_SVP_LANDMARK_IN_W) * face_w;
        lm->points[i][1] = face->y1 + (lm->points[i][1] / (td_float)SAMPLE_SVP_LANDMARK_IN_H) * face_h;
    }
}

static td_s32 sample_svp_argmax_f32(const td_float *data, td_u32 num)
{
    td_u32 i, best_idx = 0;
    td_float best_val = data ? data[0] : 0.0f;

    for (i = 1; i < num; i++) {
        if (data[i] > best_val) {
            best_val = data[i];
            best_idx = i;
        }
    }
    return (td_s32)best_idx;
}

static td_s32 sample_svp_npu_parse_gaze_output(const sample_svp_npu_task_info *task,
    sample_svp_gaze_result *gaze)
{
    svp_acl_mdl_dataset *output = TD_NULL;
    svp_acl_data_buffer *buf_yaw = TD_NULL, *buf_pitch = TD_NULL;
    td_void *addr_yaw = TD_NULL, *addr_pitch = TD_NULL;
    td_float *yaw_data = TD_NULL, *pitch_data = TD_NULL;
    size_t yaw_size, pitch_size;
    td_s32 yaw_idx, pitch_idx;

    sample_svp_check_exps_return(task == TD_NULL || gaze == TD_NULL, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "invalid param\n");

    (td_void)memset_s(gaze, sizeof(*gaze), 0, sizeof(*gaze));

    output = task->output_dataset;
    sample_svp_check_exps_return(output == TD_NULL, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "gaze output dataset is null!\n");

    td_u32 gaze_num_outputs = svp_acl_mdl_get_dataset_num_buffers(output);
    if (gaze_num_outputs < 2) {
        sample_svp_trace_err("gaze output num < 2, got %u\n", gaze_num_outputs);
        return TD_FAILURE;
    }

    buf_yaw = svp_acl_mdl_get_dataset_buffer(output, 0);
    buf_pitch = svp_acl_mdl_get_dataset_buffer(output, 1);
    sample_svp_check_exps_return(buf_yaw == TD_NULL || buf_pitch == TD_NULL, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "gaze output buffer null\n");

    addr_yaw = svp_acl_get_data_buffer_addr(buf_yaw);
    addr_pitch = svp_acl_get_data_buffer_addr(buf_pitch);
    yaw_size = svp_acl_get_data_buffer_size(buf_yaw);
    pitch_size = svp_acl_get_data_buffer_size(buf_pitch);

    sample_svp_check_exps_return(addr_yaw == TD_NULL || addr_pitch == TD_NULL ||
        yaw_size < 90 * sizeof(td_float) || pitch_size < 90 * sizeof(td_float),
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "gaze output size invalid\n");

    yaw_data = (td_float *)addr_yaw;
    pitch_data = (td_float *)addr_pitch;

    yaw_idx = sample_svp_argmax_f32(yaw_data, 90);
    pitch_idx = sample_svp_argmax_f32(pitch_data, 90);

    gaze->yaw_deg = (td_float)(yaw_idx - 45);
    gaze->pitch_deg = (td_float)(pitch_idx - 45);

    return TD_SUCCESS;
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
    td_double t_gaze_prep, td_double t_gaze_infer, td_double t_gaze_parse,
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
    g_pipe_prof.sum_gaze_prep += t_gaze_prep;
    g_pipe_prof.sum_gaze_infer += t_gaze_infer;
    g_pipe_prof.sum_gaze_parse += t_gaze_parse;

    if (t_total > g_pipe_prof.max_total) {
        g_pipe_prof.max_total = t_total;
    }
    if (t_face_det > g_pipe_prof.max_face_det) {
        g_pipe_prof.max_face_det = t_face_det;
    }
    if (t_lm_infer > g_pipe_prof.max_lm_infer) {
        g_pipe_prof.max_lm_infer = t_lm_infer;
    }
    if (t_gaze_infer > g_pipe_prof.max_gaze_infer) {
        g_pipe_prof.max_gaze_infer = t_gaze_infer;
    }

    dt = now - g_pipe_prof.win_start;
    if (dt < 1.0 || g_pipe_prof.frame_cnt == 0) {
        return;
    }

    inv_frame = 1.0 / (td_double)g_pipe_prof.frame_cnt;
    inv_face = (g_pipe_prof.face_cnt > 0) ? (1.0 / (td_double)g_pipe_prof.face_cnt) : 0.0;

    sample_svp_trace_info("[NPU-PROF] avg_ms total=%.2f face_det=%.2f lm_prep=%.2f lm_infer=%.2f lm_parse_map=%.2f gaze_prep=%.2f gaze_infer=%.2f gaze_parse=%.2f | max_ms total=%.2f face_det=%.2f lm_infer=%.2f gaze_infer=%.2f | face_ratio=%.1f%% window=%.2fs\n",
        g_pipe_prof.sum_total * 1000.0 * inv_frame,
        g_pipe_prof.sum_face_det * 1000.0 * inv_frame,
        g_pipe_prof.sum_lm_prep * 1000.0 * inv_face,
        g_pipe_prof.sum_lm_infer * 1000.0 * inv_face,
        g_pipe_prof.sum_lm_parse_map * 1000.0 * inv_face,
        g_pipe_prof.sum_gaze_prep * 1000.0 * inv_face,
        g_pipe_prof.sum_gaze_infer * 1000.0 * inv_face,
        g_pipe_prof.sum_gaze_parse * 1000.0 * inv_face,
        g_pipe_prof.max_total * 1000.0,
        g_pipe_prof.max_face_det * 1000.0,
        g_pipe_prof.max_lm_infer * 1000.0,
        g_pipe_prof.max_gaze_infer * 1000.0,
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

static td_void sample_svp_update_face_state(const sample_svp_landmark106_result *lm,
    const sample_svp_gaze_result *gaze, sample_svp_face_state *state)
{
    td_float left = sample_svp_eye_aspect_ratio(lm, TD_TRUE);
    td_float right = sample_svp_eye_aspect_ratio(lm, TD_FALSE);
    td_float eye_open = (left + right) * 0.5f;
    td_float mouth_open = sample_svp_mouth_aspect_ratio(lm);
    td_float alpha = 0.25f, beta = 0.35f;
    td_double now = sample_svp_now_seconds();

    if (state->smooth_yaw == 0.0f && state->smooth_pitch == 0.0f) {
        state->smooth_yaw = gaze->yaw_deg;
        state->smooth_pitch = gaze->pitch_deg;
    } else {
        state->smooth_yaw = (1.0f - alpha) * state->smooth_yaw + alpha * gaze->yaw_deg;
        state->smooth_pitch = (1.0f - alpha) * state->smooth_pitch + alpha * gaze->pitch_deg;
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
    const sample_svp_face_box *face, sample_svp_face_box *used_crop)
{
    sample_svp_face_box crop_box;
    svp_acl_error acl_ret;

    (void)rgb;
    (void)width;
    (void)height;

    sample_svp_check_exps_return(face == TD_NULL, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "landmark face is null\n");
    sample_svp_check_exps_return(g_landmark_model_input_virt == TD_NULL || g_landmark_expect_w == 0 || g_landmark_expect_h == 0,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "landmark input buffer is invalid\n");

    crop_box = *face;
    sample_svp_make_square_bbox_unclipped(&crop_box, SAMPLE_SVP_LANDMARK_CROP_SCALE);
    if (used_crop != TD_NULL) {
        *used_crop = crop_box;
    }

    sample_svp_check_exps_return(sample_svp_crop_resize_rgb888_to_fp32_nchw(TD_NULL, 0, 0, &crop_box,
        g_landmark_expect_w, g_landmark_expect_h, g_landmark_model_input_virt,
        g_landmark_expect_size, 1.0f, TD_TRUE) != TD_SUCCESS,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "prepare landmark input from nv21 failed\n");

    acl_ret = svp_acl_rt_mem_flush(g_landmark_model_input_virt, g_landmark_expect_size);
    sample_svp_check_exps_return(acl_ret != SVP_ACL_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "flush landmark input failed, ret=%d\n", acl_ret);
    return TD_SUCCESS;
}

static td_s32 sample_svp_prepare_gaze_input_rgb888(const td_u8 *rgb, td_u32 width, td_u32 height,
    const sample_svp_face_box *face)
{
    sample_svp_face_box crop_box;
    svp_acl_error acl_ret;

    (void)rgb;
    (void)width;
    (void)height;

    sample_svp_check_exps_return(face == TD_NULL, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "gaze face is null\n");
    sample_svp_check_exps_return(g_gaze_model_input_virt == TD_NULL || g_gaze_expect_w == 0 || g_gaze_expect_h == 0,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "gaze input buffer is invalid\n");

    crop_box = *face;
    sample_svp_expand_square_bbox(&crop_box,
        g_svp_npu_face_det_frame.video_frame.width,
        g_svp_npu_face_det_frame.video_frame.height,
        SAMPLE_SVP_GAZE_CROP_SCALE);
    sample_svp_check_exps_return(sample_svp_crop_resize_rgb888_to_fp32_nchw(TD_NULL, 0, 0, &crop_box,
        g_gaze_expect_w, g_gaze_expect_h, g_gaze_model_input_virt,
        g_gaze_expect_size, 1.0f / 255.0f, TD_FALSE) != TD_SUCCESS,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "prepare gaze input from nv21 failed\n");

    acl_ret = svp_acl_rt_mem_flush(g_gaze_model_input_virt, g_gaze_expect_size);
    sample_svp_check_exps_return(acl_ret != SVP_ACL_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "flush gaze input failed, ret=%d\n", acl_ret);
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
    sample_svp_face_box landmark_crop = {0};
    td_double t0;
    td_double t1;
    td_double t2;
    td_double t3;
    td_double t4;
    td_double t5;
    td_double t6;
    td_double t7;
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

    if (!sample_svp_select_largest_face(&faces, width, height, &best_face)) {
        result->has_face = TD_FALSE;
        sample_svp_npu_clear_face_det_frame();
        sample_svp_pipeline_profile_commit(t1 - t0, t1 - t0,
            0.0, 0.0, 0.0,
            0.0, 0.0, 0.0,
            TD_FALSE);
        return TD_SUCCESS;
    }

    sample_svp_clamp_bbox(&best_face, width, height);
    result->has_face = TD_TRUE;
    result->face = best_face;

    /* 2. landmark */
    ret = sample_svp_prepare_landmark_input_rgb888(TD_NULL, width, height, &best_face, &landmark_crop);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "prepare landmark failed\n");
    t2 = sample_svp_now_seconds();

    if (g_face_det_debug_frame_idx < SAMPLE_SVP_FACE_DET_DEBUG_FRAME_MAX) {
        sample_svp_trace_info("roi handoff: det=(%.1f,%.1f,%.1f,%.1f) -> landmark_crop=(%.1f,%.1f,%.1f,%.1f)\n",
            best_face.x1, best_face.y1, best_face.x2, best_face.y2,
            landmark_crop.x1, landmark_crop.y1, landmark_crop.x2, landmark_crop.y2);
    }

    ret = sample_common_svp_npu_model_execute(&g_svp_npu_task[1]);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "run landmark failed\n");
    t3 = sample_svp_now_seconds();

    ret = sample_svp_npu_parse_landmark_output(&g_svp_npu_task[1], &result->landmark);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "parse landmark failed\n");
    sample_svp_landmark_map_to_full_image(&result->landmark, &landmark_crop);
    t4 = sample_svp_now_seconds();

    /* 3. gaze */
    ret = sample_svp_prepare_gaze_input_rgb888(TD_NULL, width, height, &best_face);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "prepare gaze failed\n");
    t5 = sample_svp_now_seconds();

    ret = sample_common_svp_npu_model_execute(&g_svp_npu_task[2]);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "run gaze failed\n");
    t6 = sample_svp_now_seconds();

    ret = sample_svp_npu_parse_gaze_output(&g_svp_npu_task[2], &result->gaze);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "parse gaze failed\n");
    t7 = sample_svp_now_seconds();

    if (result->landmark.point_num == SAMPLE_SVP_LANDMARK_NUM) {
        sample_svp_update_face_state(&result->landmark, &result->gaze, &g_face_state);
    }

    result->state_snapshot = g_face_state;
    sample_svp_npu_clear_face_det_frame();

    t8 = sample_svp_now_seconds();
    sample_svp_pipeline_profile_commit(t8 - t0, t1 - t0,
        t2 - t1, t3 - t2, t4 - t3,
        t5 - t4, t6 - t5, t7 - t6,
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
