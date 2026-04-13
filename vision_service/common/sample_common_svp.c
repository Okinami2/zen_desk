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

#include "sample_common_svp.h"

static td_bool g_sample_svp_init_flag = TD_FALSE;

/*
 * 最小离线系统初始化：
 * 1. 退出旧 SYS/VB
 * 2. 重新配置一个最小 VB
 * 3. 初始化 SYS
 *
 * 这里不再区分在线 VI 场景，vi_en 参数仅为兼容旧接口保留。
 */
static td_s32 sample_comm_svp_sys_init(td_bool vi_en)
{
    td_s32 ret;
    ot_vb_cfg vb_cfg;

    (td_void)vi_en;

    ret = ss_mpi_sys_exit();
    if (ret != TD_SUCCESS) {
        sample_svp_trace_warning("ss_mpi_sys_exit ret=%#x\n", ret);
    }

    ret = ss_mpi_vb_exit();
    if (ret != TD_SUCCESS) {
        sample_svp_trace_warning("ss_mpi_vb_exit ret=%#x\n", ret);
    }

    (td_void)memset_s(&vb_cfg, sizeof(vb_cfg), 0, sizeof(vb_cfg));

    vb_cfg.max_pool_cnt = SAMPLE_SVP_VB_POOL_NUM;
    vb_cfg.common_pool[0].blk_size = SAMPLE_SVP_D1_PAL_WIDTH * SAMPLE_SVP_D1_PAL_HEIGHT;
    vb_cfg.common_pool[0].blk_cnt  = 1;

    ret = ss_mpi_vb_set_cfg(&vb_cfg);
    sample_svp_check_exps_return(ret != TD_SUCCESS, ret, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "ss_mpi_vb_set_cfg failed, ret=%#x\n", ret);

    ret = ss_mpi_vb_init();
    sample_svp_check_exps_return(ret != TD_SUCCESS, ret, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "ss_mpi_vb_init failed, ret=%#x\n", ret);

    ret = ss_mpi_sys_init();
    sample_svp_check_exps_return(ret != TD_SUCCESS, ret, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "ss_mpi_sys_init failed, ret=%#x\n", ret);

    return TD_SUCCESS;
}

static td_s32 sample_comm_svp_sys_exit(td_void)
{
    td_s32 ret;

    ret = ss_mpi_sys_exit();
    sample_svp_check_exps_return(ret != TD_SUCCESS, ret, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "ss_mpi_sys_exit failed, ret=%#x\n", ret);

    ret = ss_mpi_vb_exit();
    sample_svp_check_exps_return(ret != TD_SUCCESS, ret, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "ss_mpi_vb_exit failed, ret=%#x\n", ret);

    return TD_SUCCESS;
}

td_s32 sample_common_svp_check_sys_init(td_bool vi_en)
{
    if (g_sample_svp_init_flag == TD_FALSE) {
        if (sample_comm_svp_sys_init(vi_en) != TD_SUCCESS) {
            sample_svp_trace_err("Svp mpi init failed!\n");
            return TD_FALSE;
        }
        g_sample_svp_init_flag = TD_TRUE;
    }

    sample_svp_trace_info("Svp mpi init ok!\n");
    return TD_TRUE;
}

td_void sample_common_svp_check_sys_exit(td_void)
{
    td_s32 ret;

    if (g_sample_svp_init_flag == TD_TRUE) {
        ret = sample_comm_svp_sys_exit();
        if (ret != TD_SUCCESS) {
            sample_svp_trace_err("svp mpi exit failed!\n");
        }
    }

    g_sample_svp_init_flag = TD_FALSE;
    sample_svp_trace_info("Svp mpi exit ok!\n");
}