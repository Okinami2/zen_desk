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

#ifndef __MEDIA_VDEC_H__
#define __MEDIA_VDEC_H__

#include "sample_comm.h"

td_s32 sample_uvc_media_get_frame(ot_video_frame_info *frame, td_s32 milli_sec);
td_s32 sample_uvc_media_release_frame(const ot_video_frame_info *frame);
td_s32 sample_uvc_media_init(const td_char *type_name, td_u32 width, td_u32 height);
td_s32 sample_uvc_media_exit(td_void);
td_s32 sample_uvc_media_send_data(td_void *data, td_u32 size, td_u32 stride,
    const ot_size *pic_size, const td_char *type_name);
td_s32 sample_uvc_media_stop_receive_data(td_void);
td_s32 sample_uvc_media_get_frame(ot_video_frame_info *frame, td_s32 milli_sec);
td_s32 sample_uvc_media_release_frame(const ot_video_frame_info *frame);
td_void sample_uvc_media_set_preview_enable(td_bool enable);
void nv21_to_rgb888_safe(const uint8_t *y_plane, const uint8_t *vu_plane,
    int width, int height, int y_stride, int vu_stride, uint8_t *rgb);
#endif /* end of #ifndef __MEDIA_VDEC_H__ */
