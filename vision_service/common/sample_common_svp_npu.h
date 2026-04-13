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

#ifndef SAMPLE_COMMON_SVP_NPU_H
#define SAMPLE_COMMON_SVP_NPU_H

#include "ot_type.h"
#include "ot_common.h"
#include "ot_common_svp.h"
#include "svp_acl.h"
#include "svp_acl_mdl.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

#define SAMPLE_SVP_NPU_MAX_TASK_NUM        16
#define SAMPLE_SVP_NPU_MAX_MODEL_NUM       3
#define SAMPLE_SVP_NPU_EXTRA_INPUT_NUM     2
#define SAMPLE_SVP_NPU_BYTE_BIT_NUM        8
#define SAMPLE_SVP_NPU_SHOW_TOP_NUM        5
#define SAMPLE_SVP_NPU_MAX_NAME_LEN        64
#define SAMPLE_SVP_NPU_MAX_MEM_SIZE        0xFFFFFFFFU
#define SAMPLE_SVP_NPU_THRESHOLD_NUM       4
#define SAMPLE_SVP_NPU_H_DIM_IDX           2
#define SAMPLE_SVP_NPU_W_DIM_IDX           3
#define SAMPLE_SVP_NPU_DIM_NUM             4

typedef struct {
    td_u32 model_id;
    td_bool is_load_flag;
    td_ulong model_mem_size;
    td_void *model_mem_ptr;
    svp_acl_mdl_desc *model_desc;
    size_t input_num;
    size_t output_num;
    size_t dynamic_batch_idx;
} sample_svp_npu_model_info;

typedef struct {
    td_u32 max_batch_num;
    td_u32 dynamic_batch_num;
    td_u32 total_t;
    td_bool is_cached;
    td_u32 model_idx;
} sample_svp_npu_task_cfg;

typedef struct {
    sample_svp_npu_task_cfg cfg;
    svp_acl_mdl_dataset *input_dataset;
    svp_acl_mdl_dataset *output_dataset;
    td_void *task_buf_ptr;
    size_t task_buf_size;
    size_t task_buf_stride;
    td_void *work_buf_ptr;
    size_t work_buf_size;
    size_t work_buf_stride;
} sample_svp_npu_task_info;

typedef struct {
    td_void *work_buf_ptr;
    size_t work_buf_size;
    size_t work_buf_stride;
} sample_svp_npu_shared_work_buf;

typedef struct {
    td_float score;
    td_u32 class_id;
} sample_svp_npu_top_n_result;

/* acl init */
td_s32 sample_common_svp_npu_acl_init(const td_char *acl_config_path, td_s32 dev_id);

/* acl deinit */
td_void sample_common_svp_npu_acl_deinit(td_s32 dev_id);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif /* SAMPLE_COMMON_SVP_NPU_H */