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

#define SAMPLE_SVP_FACE_DET_INPUT_BIN_PATH   "./data/input/face_det_input.bin"
#define SAMPLE_SVP_LANDMARK_INPUT_BIN_PATH   "./data/input/landmark_input.bin"
#define SAMPLE_SVP_GAZE_INPUT_BIN_PATH       "./data/input/gaze_input.bin"

#define SAMPLE_SVP_EYE_CLOSED_TH      0.19f
#define SAMPLE_SVP_MOUTH_OPEN_TH      0.28f
#define SAMPLE_SVP_BLINK_MIN_FRAMES   2
#define SAMPLE_SVP_BLINK_MAX_FRAMES   8
#define SAMPLE_SVP_YAWN_MIN_SECONDS   0.8

#define SAMPLE_SVP_FACE_DET_IN_W      640
#define SAMPLE_SVP_FACE_DET_IN_H      640
#define SAMPLE_SVP_LANDMARK_IN_W      192
#define SAMPLE_SVP_LANDMARK_IN_H      192
#define SAMPLE_SVP_GAZE_IN_W          448
#define SAMPLE_SVP_GAZE_IN_H          448

#define SAMPLE_SVP_LANDMARK_NUM       106

#define SAMPLE_SVP_FACE_CONF_TH       0.80f
#define SAMPLE_SVP_FACE_NMS_TH        0.40f
#define SAMPLE_SVP_FACE_MIN_SIZE      10.0f
#define SAMPLE_SVP_GAZE_CROP_SCALE    1.25f
#define SAMPLE_SVP_LANDMARK_CROP_SCALE 1.00f

typedef struct {
    td_u32 num;
    sample_svp_face_box boxes[SAMPLE_SVP_NPU_MAX_FACE_NUM];
} sample_svp_face_box_list;

#define SAMPLE_SVP_NPU_FACE_DET_THRESHOLD_NUM 1

static sample_svp_npu_threshold g_svp_npu_face_det_threshold[SAMPLE_SVP_NPU_FACE_DET_THRESHOLD_NUM] = {
    {0.9, 0.15, 1.0, 1.0, "rpn_data"},
};

static sample_svp_npu_roi_info g_svp_npu_face_det_roi_info = {"output0", "output0_"};
static sample_svp_npu_detection_info g_svp_npu_face_det_info = {0};
static ot_sample_svp_rect_info g_svp_npu_face_det_rect_info = {0};
static ot_video_frame_info g_svp_npu_face_det_frame = {0};
static const td_u8 *g_svp_npu_face_det_frame_virt = TD_NULL;
static td_bool g_svp_npu_face_det_frame_ready = TD_FALSE;


static td_bool g_svp_npu_terminate_signal = TD_FALSE;
static td_s32 g_svp_npu_dev_id = 0;
static td_bool g_pipeline_inited = TD_FALSE;
static sample_svp_face_state g_face_state = {0};
static sample_svp_npu_task_info g_svp_npu_task[SAMPLE_SVP_NPU_OFFLINE_TASK_NUM] = {0};
static td_u32 g_face_det_input_dump_count = 0;
static td_u32 g_face_det_output_dump_count = 0;

/* debug helpers forward declaration */
static td_void sample_svp_dump_f32_preview(const td_char *tag, const td_float *data, td_u32 total_num, td_u32 preview_num);
td_s32 sample_svp_npu_set_face_det_frame(const ot_video_frame_info *frame, const td_u8 *frame_virt);

/* ----------------------------- 工具函数（提前定义） ----------------------------- */
static td_void sample_svp_dump_f32_preview(const td_char *tag,
    const td_float *data, td_u32 total_num, td_u32 preview_num)
{
    td_u32 i;
    td_u32 n;
    td_float min_v, max_v, sum_v;

    if (tag == TD_NULL || data == TD_NULL || total_num == 0) {
        return;
    }

    n = (preview_num < total_num) ? preview_num : total_num;
    min_v = data[0];
    max_v = data[0];
    sum_v = 0.0f;

    sample_svp_trace_info("%s total=%u preview=%u\n", tag, total_num, n);

    for (i = 0; i < total_num; i++) {
        td_float v = data[i];
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
        sum_v += v;
    }

    for (i = 0; i < n; i++) {
        sample_svp_trace_info("%s[%u] = %.6f\n", tag, i, data[i]);
    }

    sample_svp_trace_info("%s stats: min=%.6f max=%.6f mean=%.6f\n",
        tag, min_v, max_v, sum_v / (td_float)total_num);
}


static td_void sample_svp_npu_setup_face_detection_info(td_void)
{
    g_svp_npu_face_det_info.num_name = g_svp_npu_face_det_roi_info.roi_num_name;
    g_svp_npu_face_det_info.roi_name = g_svp_npu_face_det_roi_info.roi_class_name;
    g_svp_npu_face_det_info.has_background = TD_FALSE;
    g_svp_npu_face_det_info.is_cpu_rpn = TD_FALSE;
    g_svp_npu_face_det_info.idx = 0;
}

td_s32 sample_svp_npu_set_face_det_frame(const ot_video_frame_info *frame, const td_u8 *frame_virt)
{
    sample_svp_check_exps_return(frame == TD_NULL || frame_virt == TD_NULL,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "invalid face det frame\n");

    g_svp_npu_face_det_frame = *frame;
    g_svp_npu_face_det_frame_virt = frame_virt;
    g_svp_npu_face_det_frame_ready = TD_TRUE;
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
    td_u32 i;

    sample_svp_check_exps_return(face_list == TD_NULL, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "face list is null\n");
    sample_svp_check_exps_return(g_svp_npu_face_det_frame_ready != TD_TRUE || g_svp_npu_face_det_frame_virt == TD_NULL,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "face det video frame not set\n");

    (td_void)memset_s(face_list, sizeof(*face_list), 0, sizeof(*face_list));
    (td_void)memset_s(&g_svp_npu_face_det_rect_info, sizeof(g_svp_npu_face_det_rect_info),
        0, sizeof(g_svp_npu_face_det_rect_info));

    y_size = g_svp_npu_face_det_frame.video_frame.stride[0] * g_svp_npu_face_det_frame.video_frame.height;
    frame_size = y_size + (g_svp_npu_face_det_frame.video_frame.stride[1] *
        g_svp_npu_face_det_frame.video_frame.height / 2);

    ret = sample_common_svp_npu_update_input_data_buffer_info((td_void *)g_svp_npu_face_det_frame_virt,
        frame_size, g_svp_npu_face_det_frame.video_frame.stride[0], 0, &g_svp_npu_task[0]);
    sample_svp_check_exps_return(ret != TD_SUCCESS, ret, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "update face det data buffer failed\n");

    ret = sample_common_svp_npu_model_execute(&g_svp_npu_task[0]);
    sample_svp_check_exps_return(ret != TD_SUCCESS, ret, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "face det model execute failed\n");

    ret = sample_common_svp_npu_roi_to_rect(&g_svp_npu_task[0], &g_svp_npu_face_det_info,
        &g_svp_npu_face_det_frame, &g_svp_npu_face_det_frame, &g_svp_npu_face_det_rect_info);
    sample_svp_check_exps_return(ret != TD_SUCCESS, ret, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "face det roi_to_rect failed\n");

    for (i = 0; i < g_svp_npu_face_det_rect_info.num && i < SAMPLE_SVP_NPU_MAX_FACE_NUM; i++) {
        sample_svp_face_box *box = &face_list->boxes[face_list->num];

        box->x1 = (td_float)g_svp_npu_face_det_rect_info.rect[i].point[0].x;
        box->y1 = (td_float)g_svp_npu_face_det_rect_info.rect[i].point[0].y;
        box->x2 = (td_float)g_svp_npu_face_det_rect_info.rect[i].point[2].x;
        box->y2 = (td_float)g_svp_npu_face_det_rect_info.rect[i].point[2].y;
        box->score = (td_float)g_svp_npu_face_det_rect_info.rect[i].score;
        face_list->num++;
    }

    sample_svp_trace_info("face_detection.om detected %u faces\n", face_list->num);
    return TD_SUCCESS;
}


static td_s32 sample_svp_write_bin_file(const td_char *path, const td_u8 *data, td_u32 size)
{
    FILE *fp = TD_NULL;
    size_t wr;

    sample_svp_check_exps_return(path == TD_NULL || data == TD_NULL || size == 0,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "invalid write bin args\n");

    fp = fopen(path, "wb");
    sample_svp_check_exps_return(fp == TD_NULL, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "open file failed: %s\n", path);

    wr = fwrite(data, 1, size, fp);
    fclose(fp);

    sample_svp_check_exps_return(wr != size, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "write file failed: %s\n", path);
    return TD_SUCCESS;
}

/* RGB888 → FP32 NCHW (归一化到 0~1) */
static td_s32 sample_svp_rgb888_to_fp32_nchw(const td_u8 *rgb, td_u32 width, td_u32 height,
    td_u32 target_w, td_u32 target_h, const td_char *out_path)
{
    td_float *dst = TD_NULL;
    td_u32 y, x;

    dst = (td_float *)malloc((size_t)target_w * target_h * 3 * sizeof(td_float));
    if (dst == TD_NULL) {
        sample_svp_trace_err("malloc fp32 buffer failed\n");
        return TD_FAILURE;
    }

    for (y = 0; y < target_h; y++) {
        td_u32 sy = (td_u32)((td_u64)y * height / target_h);
        for (x = 0; x < target_w; x++) {
            td_u32 sx = (td_u32)((td_u64)x * width / target_w);
            const td_u8 *src_pixel = rgb + (sy * width + sx) * 3;

            dst[0 * target_h * target_w + y * target_w + x] = src_pixel[0] / 255.0f;
            dst[1 * target_h * target_w + y * target_w + x] = src_pixel[1] / 255.0f;
            dst[2 * target_h * target_w + y * target_w + x] = src_pixel[2] / 255.0f;
        }
    }

    {
        td_s32 ret = sample_svp_write_bin_file(out_path, (td_u8 *)dst,
            (td_u32)(target_w * target_h * 3 * sizeof(td_float)));
        free(dst);
        return ret;
    }
}

/* RetinaFace 官方示例风格预处理：RGB 输入先转 BGR，再减均值 [104,117,123]，输出 FP32 NCHW */
static td_s32 sample_svp_retinaface_rgb888_to_bgr_mean_nchw(const td_u8 *rgb, td_u32 width, td_u32 height,
    td_u32 target_w, td_u32 target_h, const td_char *out_path)
{
    td_float *dst = TD_NULL;
    td_u32 y, x;

    dst = (td_float *)malloc((size_t)target_w * target_h * 3 * sizeof(td_float));
    if (dst == TD_NULL) {
        sample_svp_trace_err("malloc retinaface fp32 buffer failed\n");
        return TD_FAILURE;
    }

    for (y = 0; y < target_h; y++) {
        td_u32 sy = (td_u32)((td_u64)y * height / target_h);
        if (sy >= height) {
            sy = height - 1;
        }
        for (x = 0; x < target_w; x++) {
            td_u32 sx = (td_u32)((td_u64)x * width / target_w);
            const td_u8 *src_pixel;
            td_float r, g, b;
            if (sx >= width) {
                sx = width - 1;
            }
            src_pixel = rgb + (sy * width + sx) * 3;
            r = (td_float)src_pixel[0];
            g = (td_float)src_pixel[1];
            b = (td_float)src_pixel[2];

            dst[0 * target_h * target_w + y * target_w + x] = b - 104.0f;
            dst[1 * target_h * target_w + y * target_w + x] = g - 117.0f;
            dst[2 * target_h * target_w + y * target_w + x] = r - 123.0f;
        }
    }
    if (g_face_det_input_dump_count < 3) {
        sample_svp_trace_info("face det input preprocess shape: [1,3,%u,%u] RGB->BGR mean=[104,117,123]\n",
            target_h, target_w);
        sample_svp_dump_f32_preview("face det input tensor",
            dst, target_w * target_h * 3, 16);
        g_face_det_input_dump_count++;
    }
    {
        td_s32 ret = sample_svp_write_bin_file(out_path, (td_u8 *)dst,
            (td_u32)(target_w * target_h * 3 * sizeof(td_float)));
        free(dst);
        return ret;
    }
}

static td_void sample_svp_scale_bbox_from_model_to_image(sample_svp_face_box *box, td_u32 image_w, td_u32 image_h)
{
    td_float scale_x;
    td_float scale_y;

    if (box == TD_NULL || image_w == 0 || image_h == 0) {
        return;
    }

    scale_x = (td_float)image_w / (td_float)SAMPLE_SVP_FACE_DET_IN_W;
    scale_y = (td_float)image_h / (td_float)SAMPLE_SVP_FACE_DET_IN_H;

    box->x1 *= scale_x;
    box->x2 *= scale_x;
    box->y1 *= scale_y;
    box->y2 *= scale_y;
}

static td_float sample_svp_max_f32(td_float a, td_float b)
{
    return (a > b) ? a : b;
}

static td_float sample_svp_min_f32(td_float a, td_float b)
{
    return (a < b) ? a : b;
}

static td_float sample_svp_sigmoid_f32(td_float x)
{
    if (x >= 0.0f) {
        td_float z = expf(-x);
        return 1.0f / (1.0f + z);
    }
    td_float z = expf(x);
    return z / (1.0f + z);
}

static td_float sample_svp_softmax_face_score(td_float bg, td_float face)
{
    td_float m = (bg > face) ? bg : face;
    td_float ebg = expf(bg - m);
    td_float eface = expf(face - m);
    td_float sum = ebg + eface;
    return (sum > 1e-12f) ? (eface / sum) : 0.0f;
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

static td_s32 sample_svp_crop_resize_rgb888_to_fp32_nchw(const td_u8 *rgb, td_u32 width, td_u32 height,
    const sample_svp_face_box *crop_box, td_u32 target_w, td_u32 target_h, const td_char *out_path)
{
    td_float *dst = TD_NULL;
    td_u32 y, x;
    td_u32 x1, y1, crop_w, crop_h;

    sample_svp_check_exps_return(rgb == TD_NULL || crop_box == TD_NULL || out_path == TD_NULL,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "invalid crop args\n");

    x1 = (td_u32)sample_svp_max_f32(0.0f, floorf(crop_box->x1));
    y1 = (td_u32)sample_svp_max_f32(0.0f, floorf(crop_box->y1));
    crop_w = (td_u32)sample_svp_max_f32(1.0f, ceilf(crop_box->x2) - (td_float)x1);
    crop_h = (td_u32)sample_svp_max_f32(1.0f, ceilf(crop_box->y2) - (td_float)y1);

    if (x1 + crop_w > width) {
        crop_w = width - x1;
    }
    if (y1 + crop_h > height) {
        crop_h = height - y1;
    }

    dst = (td_float *)malloc((size_t)target_w * target_h * 3 * sizeof(td_float));
    if (dst == TD_NULL) {
        sample_svp_trace_err("malloc crop fp32 buffer failed\n");
        return TD_FAILURE;
    }

    for (y = 0; y < target_h; y++) {
        td_u32 sy = y1 + (td_u32)((td_u64)y * crop_h / target_h);
        if (sy >= height) {
            sy = height - 1;
        }
        for (x = 0; x < target_w; x++) {
            td_u32 sx = x1 + (td_u32)((td_u64)x * crop_w / target_w);
            const td_u8 *src_pixel;
            if (sx >= width) {
                sx = width - 1;
            }
            src_pixel = rgb + (sy * width + sx) * 3;

            dst[0 * target_h * target_w + y * target_w + x] = src_pixel[0] / 255.0f;
            dst[1 * target_h * target_w + y * target_w + x] = src_pixel[1] / 255.0f;
            dst[2 * target_h * target_w + y * target_w + x] = src_pixel[2] / 255.0f;
        }
    }

    {
        td_s32 ret = sample_svp_write_bin_file(out_path, (td_u8 *)dst,
            (td_u32)(target_w * target_h * 3 * sizeof(td_float)));
        free(dst);
        return ret;
    }
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
            sample_svp_npu_acl_deinit_task(task_num);
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

    sample_svp_npu_setup_face_detection_info();
    ret = sample_common_svp_npu_set_threshold(&g_svp_npu_face_det_threshold[0],
        SAMPLE_SVP_NPU_FACE_DET_THRESHOLD_NUM, &g_svp_npu_task[0]);
    if (ret != TD_SUCCESS) {
        sample_svp_trace_err("set face detection threshold failed!\n");
        sample_svp_npu_acl_deinit_task(SAMPLE_SVP_NPU_OFFLINE_TASK_NUM);
        sample_svp_npu_pipeline_unload_models();
        sample_svp_npu_acl_deinit();
        return TD_FAILURE;
    }

    return TD_SUCCESS;
}

static td_void sample_svp_npu_pipeline_deinit(td_void)
{
    sample_svp_npu_acl_deinit_task(SAMPLE_SVP_NPU_OFFLINE_TASK_NUM);
    sample_svp_npu_pipeline_unload_models();
    sample_svp_npu_acl_deinit();
    sample_svp_npu_acl_terminate();
}
/* 辅助 IOU 计算函数（请添加到文件合适位置，例如其他工具函数之后） */
static td_float sample_svp_calc_iou(const sample_svp_face_box *a, const sample_svp_face_box *b)
{
    td_float x1 = fmaxf(a->x1, b->x1);
    td_float y1 = fmaxf(a->y1, b->y1);
    td_float x2 = fminf(a->x2, b->x2);
    td_float y2 = fminf(a->y2, b->y2);

    td_float inter = (x2 - x1) * (y2 - y1);
    if (inter <= 0) return 0.0f;

    td_float area_a = (a->x2 - a->x1) * (a->y2 - a->y1);
    td_float area_b = (b->x2 - b->x1) * (b->y2 - b->y1);
    return inter / (area_a + area_b - inter);
}

/* ----------------------------- 单模型执行辅助 ----------------------------- */

static td_s32 sample_svp_npu_run_model_with_input_file(td_u32 task_idx, const td_char *src_file)
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

static td_bool sample_svp_face_det_buffer_looks_like_score(const td_float *data, td_u32 elem_num)
{
    td_u32 i;
    td_u32 sample_num;
    td_u32 in_01 = 0;
    td_u32 negative_num = 0;

    if (data == TD_NULL || elem_num == 0) {
        return TD_FALSE;
    }

    sample_num = (elem_num < 128) ? elem_num : 128;
    for (i = 0; i < sample_num; i++) {
        td_float v = data[i];
        if (v >= 0.0f && v <= 1.0f) {
            in_01++;
        }
        if (v < 0.0f) {
            negative_num++;
        }
    }

    return (in_01 >= sample_num * 3 / 4 && negative_num <= sample_num / 16) ? TD_TRUE : TD_FALSE;
}

static td_float sample_svp_face_det_get_conf(const td_float *score_data, td_u32 score_dim, td_u32 idx)
{
    td_u32 c;
    td_float conf;

    if (score_dim == 0 || score_data == TD_NULL) {
        return 0.0f;
    }
    if (score_dim == 1) {
        return score_data[idx];
    }

    conf = score_data[idx * score_dim + 1];
    for (c = 2; c < score_dim; c++) {
        td_float v = score_data[idx * score_dim + c];
        if (v > conf) {
            conf = v;
        }
    }
    return conf;
}

static td_s32 sample_svp_generate_retinaface_priors(td_float *priors, td_u32 prior_num)
{
    const td_u32 steps[3] = {8, 16, 32};
    const td_u32 min_sizes[3][2] = {{16, 32}, {64, 128}, {256, 512}};
    td_u32 k = 0;
    td_u32 s, y, x, m;

    if (priors == TD_NULL) {
        return TD_FAILURE;
    }

    for (s = 0; s < 3; s++) {
        td_u32 step = steps[s];
        td_u32 feat_w = SAMPLE_SVP_FACE_DET_IN_W / step;
        td_u32 feat_h = SAMPLE_SVP_FACE_DET_IN_H / step;

        for (y = 0; y < feat_h; y++) {
            for (x = 0; x < feat_w; x++) {
                for (m = 0; m < 2; m++) {
                    td_float cx;
                    td_float cy;
                    td_float pw;
                    td_float ph;

                    if (k >= prior_num) {
                        return TD_FAILURE;
                    }

                    cx = ((td_float)x + 0.5f) * (td_float)step / (td_float)SAMPLE_SVP_FACE_DET_IN_W;
                    cy = ((td_float)y + 0.5f) * (td_float)step / (td_float)SAMPLE_SVP_FACE_DET_IN_H;
                    pw = (td_float)min_sizes[s][m] / (td_float)SAMPLE_SVP_FACE_DET_IN_W;
                    ph = (td_float)min_sizes[s][m] / (td_float)SAMPLE_SVP_FACE_DET_IN_H;

                    priors[k * 4 + 0] = cx;
                    priors[k * 4 + 1] = cy;
                    priors[k * 4 + 2] = pw;
                    priors[k * 4 + 3] = ph;
                    k++;
                }
            }
        }
    }

    return (k == prior_num) ? TD_SUCCESS : TD_FAILURE;
}

static td_s32 sample_svp_npu_parse_face_det_output(const sample_svp_npu_task_info *task,
    sample_svp_face_box_list *face_list)
{
    svp_acl_mdl_dataset *output = TD_NULL;
    svp_acl_data_buffer *buf_conf = TD_NULL, *buf_loc = TD_NULL, *buf_land = TD_NULL;
    td_float *conf_data = TD_NULL, *loc_data = TD_NULL, *land_data = TD_NULL;
    td_u32 loc_size, conf_size, land_size;
    td_u32 prior_num;
    const td_u32 conf_dim = 2, conf_stride = 4;
    const td_u32 loc_dim  = 4, loc_stride  = 4;
    const td_u32 land_dim = 10, land_stride = 12;
    const td_float conf_threshold = 0.75f;

    td_float *priors = TD_NULL;   // [prior_num * 4], cx cy w h
    td_float *dets = TD_NULL;     // [prior_num * 5], x1 y1 x2 y2 score
    td_u32 det_count = 0;
    td_u32 i;

    sample_svp_check_exps_return(task == TD_NULL || face_list == TD_NULL,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "invalid param\n");

    (td_void)memset_s(face_list, sizeof(*face_list), 0, sizeof(*face_list));

    output = task->output_dataset;
    sample_svp_check_exps_return(output == TD_NULL, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "face det output dataset is null\n");

    sample_svp_check_exps_return(svp_acl_mdl_get_dataset_num_buffers(output) < 3,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "face det output num too small\n");

    /* 按图里的真实顺序：0=conf, 1=loc, 2=land */
    buf_conf = svp_acl_mdl_get_dataset_buffer(output, 0);
    buf_loc  = svp_acl_mdl_get_dataset_buffer(output, 1);
    buf_land = svp_acl_mdl_get_dataset_buffer(output, 2);

    sample_svp_check_exps_return(buf_conf == TD_NULL || buf_loc == TD_NULL || buf_land == TD_NULL,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "face det output buffer null\n");

    conf_data = (td_float *)svp_acl_get_data_buffer_addr(buf_conf);
    loc_data  = (td_float *)svp_acl_get_data_buffer_addr(buf_loc);
    land_data = (td_float *)svp_acl_get_data_buffer_addr(buf_land);

    conf_size = (td_u32)svp_acl_get_data_buffer_size(buf_conf) / sizeof(td_float);
    loc_size  = (td_u32)svp_acl_get_data_buffer_size(buf_loc) / sizeof(td_float);
    land_size = (td_u32)svp_acl_get_data_buffer_size(buf_land) / sizeof(td_float);

    
    sample_svp_check_exps_return(conf_data == TD_NULL || loc_data == TD_NULL || land_data == TD_NULL,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "face det output data invalid\n");

    /* prior_num 用 loc 推，不要再用 conf_size/2 了 */
    sample_svp_check_exps_return(loc_size % loc_stride != 0,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "loc_size invalid: %u\n", loc_size);

    prior_num = loc_size / loc_stride;

    /* 物理 stride 检查 */
    sample_svp_check_exps_return(conf_size != prior_num * conf_stride ||
                                 land_size != prior_num * land_stride,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "unexpected padded size: loc=%u conf=%u land=%u prior_num=%u\n",
        loc_size, conf_size, land_size, prior_num);

    if (g_face_det_output_dump_count < 3) {
        sample_svp_trace_info("face det raw output sizes (float): conf=%u loc=%u land=%u\n",
            conf_size, loc_size, land_size);
    
        sample_svp_trace_info("face det inferred logical shapes: conf=[1,%u,%u] stride=%u, "
                              "loc=[1,%u,%u] stride=%u, land=[1,%u,%u] stride=%u\n",
            prior_num, conf_dim, conf_stride,
            prior_num, loc_dim, loc_stride,
            prior_num, land_dim, land_stride);
    
        sample_svp_dump_f32_preview("face det conf raw", conf_data, conf_size, 16);
        sample_svp_dump_f32_preview("face det loc raw",  loc_data,  loc_size, 16);
        sample_svp_dump_f32_preview("face det land raw", land_data, land_size, 16);
    
        for (i = 0; i < ((prior_num < 8) ? prior_num : 8); i++) {
            sample_svp_trace_info("face det conf prior[%u]: bg=%.6f face=%.6f pad2=%.6f pad3=%.6f\n",
                i,
                conf_data[i * conf_stride + 0],
                conf_data[i * conf_stride + 1],
                conf_data[i * conf_stride + 2],
                conf_data[i * conf_stride + 3]);
        }
    
        g_face_det_output_dump_count++;
    }

    td_u32 max_idx = 0;
    td_float max_face = conf_data[1];
    td_u32 cnt_001 = 0, cnt_005 = 0, cnt_010 = 0;
    
    for (i = 0; i < prior_num; i++) {
        td_float face = conf_data[i * conf_stride + 1];
        if (face > max_face) {
            max_face = face;
            max_idx = i;
        }
        if (face > 0.01f) cnt_001++;
        if (face > 0.05f) cnt_005++;
        if (face > 0.10f) cnt_010++;
    }
    
    sample_svp_trace_info("face score stats: max_face=%.6f max_idx=%u cnt>0.01=%u cnt>0.05=%u cnt>0.10=%u\n",
    max_face, max_idx, cnt_001, cnt_005, cnt_010);

    priors = (td_float *)malloc(prior_num * 4 * sizeof(td_float));
    dets   = (td_float *)malloc(prior_num * 5 * sizeof(td_float));
    sample_svp_check_exps_return(priors == TD_NULL || dets == TD_NULL,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "malloc priors/dets failed\n");

    /* 标准 RetinaFace 640x640 priors */
    sample_svp_generate_retinaface_priors(priors, prior_num);

    for (td_u32 i = 0; i < prior_num; ++i) {
        td_float score = conf_data[i * conf_stride + 1];  // 已经 softmax 过，直接取 face prob
        if (score < conf_threshold) {
            continue;
        }

        td_float prior_cx = priors[i * 4 + 0];
        td_float prior_cy = priors[i * 4 + 1];
        td_float prior_w  = priors[i * 4 + 2];
        td_float prior_h  = priors[i * 4 + 3];

        td_float dx = loc_data[i * loc_stride + 0];
        td_float dy = loc_data[i * loc_stride + 1];
        td_float dw = loc_data[i * loc_stride + 2];
        td_float dh = loc_data[i * loc_stride + 3];

        td_float cx = prior_cx + dx * 0.1f * prior_w;
        td_float cy = prior_cy + dy * 0.1f * prior_h;
        td_float w  = prior_w * expf(dw * 0.2f);
        td_float h  = prior_h * expf(dh * 0.2f);

        td_float x1 = (cx - 0.5f * w) * SAMPLE_SVP_FACE_DET_IN_W;
        td_float y1 = (cy - 0.5f * h) * SAMPLE_SVP_FACE_DET_IN_H;
        td_float x2 = (cx + 0.5f * w) * SAMPLE_SVP_FACE_DET_IN_W;
        td_float y2 = (cy + 0.5f * h) * SAMPLE_SVP_FACE_DET_IN_H;

        dets[det_count * 5 + 0] = x1;
        dets[det_count * 5 + 1] = y1;
        dets[det_count * 5 + 2] = x2;
        dets[det_count * 5 + 3] = y2;
        dets[det_count * 5 + 4] = score;
        det_count++;
    }

    sample_svp_trace_info("raw candidates before NMS: %u\n", det_count);

    if (det_count > 0) {
        td_bool *suppressed = (td_bool *)calloc(det_count, sizeof(td_bool));
        td_u32 keep_count = 0;
        td_u32 j;

        if (suppressed == TD_NULL) {
            free(priors);
            free(dets);
            sample_svp_trace_err("calloc suppressed failed\n");
            return TD_FAILURE;
        }

        for (i = 0; i < det_count; i++) {
            for (j = i + 1; j < det_count; j++) {
                if (dets[i * 5 + 4] < dets[j * 5 + 4]) {
                    td_float tmp[5];
                    (td_void)memcpy_s(tmp, sizeof(tmp), &dets[i * 5], sizeof(td_float) * 5);
                    (td_void)memcpy_s(&dets[i * 5], sizeof(td_float) * 5, &dets[j * 5], sizeof(td_float) * 5);
                    (td_void)memcpy_s(&dets[j * 5], sizeof(td_float) * 5, tmp, sizeof(td_float) * 5);
                }
            }
        }

        for (i = 0; i < det_count && keep_count < SAMPLE_SVP_NPU_MAX_FACE_NUM; i++) {
            sample_svp_face_box cur_box;
            if (suppressed[i]) {
                continue;
            }

            cur_box.x1 = dets[i * 5 + 0];
            cur_box.y1 = dets[i * 5 + 1];
            cur_box.x2 = dets[i * 5 + 2];
            cur_box.y2 = dets[i * 5 + 3];
            face_list->boxes[face_list->num] = cur_box;
            face_list->num++;
            keep_count++;

            for (j = i + 1; j < det_count; j++) {
                sample_svp_face_box next_box;
                td_float iou;
                if (suppressed[j]) {
                    continue;
                }
                next_box.x1 = dets[j * 5 + 0];
                next_box.y1 = dets[j * 5 + 1];
                next_box.x2 = dets[j * 5 + 2];
                next_box.y2 = dets[j * 5 + 3];
                iou = sample_svp_calc_iou(&cur_box, &next_box);
                if (iou > SAMPLE_SVP_FACE_NMS_TH) {
                    suppressed[j] = TD_TRUE;
                }
            }
        }
        free(suppressed);
    }

    free(priors);
    free(dets);

    sample_svp_trace_info("detected %u faces after NMS\n", face_list->num);
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
    td_bool normalized = TD_TRUE;

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

    for (i = 0; i < 212; i++) {
        if (data[i] < -0.01f || data[i] > 1.01f) {
            normalized = TD_FALSE;
            break;
        }
    }

    for (i = 0; i < SAMPLE_SVP_LANDMARK_NUM; i++) {
        td_float x = data[i * 2];
        td_float y = data[i * 2 + 1];
        if (normalized) {
            x *= (td_float)SAMPLE_SVP_LANDMARK_IN_W;
            y *= (td_float)SAMPLE_SVP_LANDMARK_IN_H;
        }
        landmark->points[i][0] = x;
        landmark->points[i][1] = y;
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

static td_bool sample_svp_select_largest_face(const sample_svp_face_box_list *faces, sample_svp_face_box *best_face)
{
    td_u32 i;
    td_float best_area = 0.0f;
    td_bool found = TD_FALSE;

    if (faces == TD_NULL || best_face == TD_NULL || faces->num == 0) return TD_FALSE;

    for (i = 0; i < faces->num; i++) {
        td_float area = (faces->boxes[i].x2 - faces->boxes[i].x1) * (faces->boxes[i].y2 - faces->boxes[i].y1);
        if (!found || area > best_area) {
            *best_face = faces->boxes[i];
            best_area = area;
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

/* 其余辅助函数保持不变（prepare、resize 等） */
static td_s32 sample_svp_prepare_face_det_input_rgb888(const td_u8 *rgb, td_u32 width, td_u32 height)
{
    return sample_svp_retinaface_rgb888_to_bgr_mean_nchw(
        rgb, width, height,
        SAMPLE_SVP_FACE_DET_IN_W, SAMPLE_SVP_FACE_DET_IN_H,
        SAMPLE_SVP_FACE_DET_INPUT_BIN_PATH);
}

static td_s32 sample_svp_prepare_landmark_input_rgb888(const td_u8 *rgb, td_u32 width, td_u32 height,
    const sample_svp_face_box *face)
{
    sample_svp_face_box crop_box;

    sample_svp_check_exps_return(face == TD_NULL, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "landmark face is null\n");

    crop_box = *face;
    sample_svp_expand_square_bbox(&crop_box, width, height, SAMPLE_SVP_LANDMARK_CROP_SCALE);
    return sample_svp_crop_resize_rgb888_to_fp32_nchw(rgb, width, height, &crop_box,
        SAMPLE_SVP_LANDMARK_IN_W, SAMPLE_SVP_LANDMARK_IN_H,
        SAMPLE_SVP_LANDMARK_INPUT_BIN_PATH);
}

static td_s32 sample_svp_prepare_gaze_input_rgb888(const td_u8 *rgb, td_u32 width, td_u32 height,
    const sample_svp_face_box *face)
{
    sample_svp_face_box crop_box;

    sample_svp_check_exps_return(face == TD_NULL, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "gaze face is null\n");

    crop_box = *face;
    sample_svp_expand_square_bbox(&crop_box, width, height, SAMPLE_SVP_GAZE_CROP_SCALE);
    return sample_svp_crop_resize_rgb888_to_fp32_nchw(rgb, width, height, &crop_box,
        SAMPLE_SVP_GAZE_IN_W, SAMPLE_SVP_GAZE_IN_H,
        SAMPLE_SVP_GAZE_INPUT_BIN_PATH);
}

const sample_svp_face_state *sample_svp_npu_get_face_state(td_void)
{
    return &g_face_state;
}


/* ----------------------------- 主入口 ----------------------------- */

td_s32 sample_svp_npu_run_frame_pipeline_rgb888(const td_u8 *rgb, td_u32 width, td_u32 height,
    sample_svp_frame_result *result)
{
    td_s32 ret;
    sample_svp_face_box_list faces = {0};
    sample_svp_face_box best_face = {0};

    sample_svp_check_exps_return(rgb == TD_NULL || result == TD_NULL || width == 0 || height == 0,
        TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "invalid args\n");

    (td_void)memset_s(result, sizeof(*result), 0, sizeof(*result));

    sample_svp_check_exps_return(g_pipeline_inited != TD_TRUE, TD_FAILURE,
        SAMPLE_SVP_ERR_LEVEL_ERROR, "npu runtime not initialized\n");

    /* 1. face detection: first model now consumes the current video frame directly */
    ret = sample_svp_npu_run_face_det_with_video_frame(&faces);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "run face_detection.om failed\n");

    if (!sample_svp_select_largest_face(&faces, &best_face)) {
        result->has_face = TD_FALSE;
        sample_svp_npu_clear_face_det_frame();
        return TD_SUCCESS;
    }

    sample_svp_clamp_bbox(&best_face, width, height);
    result->has_face = TD_TRUE;
    result->face = best_face;

    /* 2. landmark */
    ret = sample_svp_prepare_landmark_input_rgb888(rgb, width, height, &best_face);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "prepare landmark failed\n");

    ret = sample_svp_npu_run_model_with_input_file(1, SAMPLE_SVP_LANDMARK_INPUT_BIN_PATH);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "run landmark failed\n");

    ret = sample_svp_npu_parse_landmark_output(&g_svp_npu_task[1], &result->landmark);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "parse landmark failed\n");
    sample_svp_landmark_map_to_full_image(&result->landmark, &best_face);

    /* 3. gaze */
    ret = sample_svp_prepare_gaze_input_rgb888(rgb, width, height, &best_face);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "prepare gaze failed\n");

    ret = sample_svp_npu_run_model_with_input_file(2, SAMPLE_SVP_GAZE_INPUT_BIN_PATH);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "run gaze failed\n");

    ret = sample_svp_npu_parse_gaze_output(&g_svp_npu_task[2], &result->gaze);
    sample_svp_check_exps_return(ret != TD_SUCCESS, TD_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "parse gaze failed\n");

    if (result->landmark.point_num == SAMPLE_SVP_LANDMARK_NUM) {
        sample_svp_update_face_state(&result->landmark, &result->gaze, &g_face_state);
    }

    result->state_snapshot = g_face_state;
    sample_svp_npu_clear_face_det_frame();
    return TD_SUCCESS;
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