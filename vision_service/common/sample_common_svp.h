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

#ifndef SAMPLE_COMMON_SVP_H
#define SAMPLE_COMMON_SVP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include "ss_mpi_sys.h"
#include "ss_mpi_vb.h"
#include "ot_common.h"
#include "ot_common_svp.h"
#include "sample_comm.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

#define OT_SVP_TIMEOUT              2000
#define SAMPLE_SVP_VB_POOL_NUM      2
#define SAMPLE_SVP_D1_PAL_HEIGHT    576
#define SAMPLE_SVP_D1_PAL_WIDTH     704

typedef enum {
    SAMPLE_SVP_ERR_LEVEL_DEBUG   = 0x0,
    SAMPLE_SVP_ERR_LEVEL_INFO    = 0x1,
    SAMPLE_SVP_ERR_LEVEL_NOTICE  = 0x2,
    SAMPLE_SVP_ERR_LEVEL_WARNING = 0x3,
    SAMPLE_SVP_ERR_LEVEL_ERROR   = 0x4,
    SAMPLE_SVP_ERR_LEVEL_CRIT    = 0x5,
    SAMPLE_SVP_ERR_LEVEL_ALERT   = 0x6,
    SAMPLE_SVP_ERR_LEVEL_FATAL   = 0x7,

    SAMPLE_SVP_ERR_LEVEL_BUTT
} sample_svp_err_level;

#define sample_svp_printf(level_str, msg, ...) \
do { \
    fprintf(stderr, "[level]:%s,[func]:%s [line]:%d [info]:" msg, \
        level_str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
} while (0)

#define sample_svp_printf_red(level_str, msg, ...) \
do { \
    fprintf(stderr, "\033[0;31m[level]:%s,[func]:%s [line]:%d [info]:" msg "\033[0;39m\n", \
        level_str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
} while (0)

#define sample_svp_trace_fatal(msg, ...)      sample_svp_printf_red("Fatal", msg, ##__VA_ARGS__)
#define sample_svp_trace_alert(msg, ...)      sample_svp_printf_red("Alert", msg, ##__VA_ARGS__)
#define sample_svp_trace_critical(msg, ...)   sample_svp_printf_red("Critical", msg, ##__VA_ARGS__)
#define sample_svp_trace_err(msg, ...)        sample_svp_printf_red("Error", msg, ##__VA_ARGS__)
#define sample_svp_trace_warning(msg, ...)    sample_svp_printf("Warning", msg, ##__VA_ARGS__)
#define sample_svp_trace_notic(msg, ...)      sample_svp_printf("Notice", msg, ##__VA_ARGS__)
#define sample_svp_trace_info(msg, ...)       sample_svp_printf("Info", msg, ##__VA_ARGS__)
#define sample_svp_trace_debug(msg, ...)      sample_svp_printf("Debug", msg, ##__VA_ARGS__)

#define sample_svp_check_exps_goto(exps, label, level, msg, ...) \
do { \
    if ((exps)) { \
        sample_svp_trace_err(msg, ##__VA_ARGS__); \
        goto label; \
    } \
} while (0)

#define sample_svp_check_exps_return_void(exps, level, msg, ...) \
do { \
    if ((exps)) { \
        sample_svp_trace_err(msg, ##__VA_ARGS__); \
        return; \
    } \
} while (0)

#define sample_svp_check_exps_return(exps, ret, level, msg, ...) \
do { \
    if ((exps)) { \
        sample_svp_trace_err(msg, ##__VA_ARGS__); \
        return (ret); \
    } \
} while (0)

#define sample_svp_check_exps_trace(exps, level, msg, ...) \
do { \
    if ((exps)) { \
        sample_svp_trace_err(msg, ##__VA_ARGS__); \
    } \
} while (0)

#define sample_svp_check_exps_continue(exps, level, msg, ...) \
do { \
    if ((exps)) { \
        sample_svp_trace_err(msg, ##__VA_ARGS__); \
        continue; \
    } \
} while (0)

#define sample_svp_convert_addr_to_ptr(type, addr) ((type *)(td_uintptr_t)(addr))
#define sample_svp_convert_ptr_to_addr(type, addr) ((type)(td_uintptr_t)(addr))

#define sample_svp_mmz_free(phys, virt) \
do { \
    if (((phys) != 0) && ((virt) != 0)) { \
        ss_mpi_sys_mmz_free((td_phys_addr_t)(phys), (td_void *)(td_uintptr_t)(virt)); \
        (phys) = 0; \
        (virt) = 0; \
    } \
} while (0)

#define sample_svp_close_file(fp) \
do { \
    if ((fp) != TD_NULL) { \
        fclose((fp)); \
        (fp) = TD_NULL; \
    } \
} while (0)

/* system init for offline svp/npu */
td_s32 sample_common_svp_check_sys_init(td_bool vi_en);

/* system exit for offline svp/npu */
td_void sample_common_svp_check_sys_exit(td_void);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif /* SAMPLE_COMMON_SVP_H */