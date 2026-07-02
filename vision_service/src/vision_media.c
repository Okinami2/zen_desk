#include "vision_media.h"
#include <unistd.h>

ot_vb_src g_vdec_vb_source = OT_VB_SRC_MOD;

#define REF_NUM 2
#define DISPLAY_NUM 2

static td_u32 g_input_width;
static td_u32 g_input_height;
static td_bool g_is_need_vdec = TD_TRUE;
static td_bool g_mpp_attached = TD_FALSE;

static td_s32 uvc_media_init_module_vb(sample_vdec_attr *sample_vdec, td_u32 vdec_chn_num,
    ot_payload_type type, td_u32 len)
{
    td_u32 i;
    td_s32 ret;

    for (i = 0; (i < vdec_chn_num) && (i < len); i++) {
        sample_vdec[i].type = type;
        sample_vdec[i].width = g_input_width;
        sample_vdec[i].height = g_input_height;
        sample_vdec[i].mode = sample_comm_vdec_get_lowdelay_en() ?
            OT_VDEC_SEND_MODE_COMPAT : OT_VDEC_SEND_MODE_FRAME;
        sample_vdec[i].sample_vdec_video.dec_mode = OT_VIDEO_DEC_MODE_IP;
        sample_vdec[i].sample_vdec_video.bit_width = OT_DATA_BIT_WIDTH_8;

        if (type == OT_PT_JPEG) {
            sample_vdec[i].sample_vdec_video.ref_frame_num = 0;
        } else {
            sample_vdec[i].sample_vdec_video.ref_frame_num = REF_NUM;
        }

        sample_vdec[i].display_frame_num = DISPLAY_NUM;
        sample_vdec[i].frame_buf_cnt = (type == OT_PT_JPEG) ?
            (sample_vdec[i].display_frame_num + 1) :
            (sample_vdec[i].sample_vdec_video.ref_frame_num +
             sample_vdec[i].display_frame_num + 1);

        if (type == OT_PT_JPEG) {
            sample_vdec[i].sample_vdec_picture.pixel_format =
                OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
            sample_vdec[i].sample_vdec_picture.alpha = 255;
        }
    }

    ret = sample_comm_vdec_init_vb_pool(vdec_chn_num, &sample_vdec[0], len);
    if (ret != TD_SUCCESS) {
        sample_print("init mod common vb fail for %#x!\n", ret);
    }
    return ret;
}

static td_s32 uvc_media_init_sys_and_vb(sample_vdec_attr *sample_vdec, td_u32 vdec_chn_num,
    ot_payload_type type, td_u32 len)
{
    ot_vb_cfg vb_cfg;
    ot_pic_buf_attr buf_attr = {0};
    td_s32 ret;

    buf_attr.align = 0;
    buf_attr.bit_width = OT_DATA_BIT_WIDTH_8;
    buf_attr.compress_mode = OT_COMPRESS_MODE_SEG;
    buf_attr.height = 2160;
    buf_attr.width = 3840;
    buf_attr.pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;

    (td_void)memset_s(&vb_cfg, sizeof(vb_cfg), 0, sizeof(vb_cfg));
    vb_cfg.max_pool_cnt = 1;
    vb_cfg.common_pool[0].blk_cnt = 10 * vdec_chn_num;
    vb_cfg.common_pool[0].blk_size = ot_common_get_pic_buf_size(&buf_attr);

    ret = sample_comm_sys_init(&vb_cfg);
    if (ret != TD_SUCCESS) {
        sample_print("init sys fail for %#x!\n", ret);
        sample_comm_sys_exit();
        return ret;
    }

    if (g_is_need_vdec == TD_TRUE) {
        ret = uvc_media_init_module_vb(&sample_vdec[0], vdec_chn_num, type, len);
        if (ret != TD_SUCCESS) {
            sample_comm_vdec_exit_vb_pool();
            sample_comm_sys_exit();
            return ret;
        }
    }

    return TD_SUCCESS;
}

static td_void uvc_media_config_vpss_grp_attr(ot_vpss_grp_attr *vpss_grp_attr)
{
    vpss_grp_attr->max_width = g_input_width;
    vpss_grp_attr->max_height = g_input_height;
    vpss_grp_attr->frame_rate.src_frame_rate = -1;
    vpss_grp_attr->frame_rate.dst_frame_rate = -1;
    vpss_grp_attr->pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    vpss_grp_attr->nr_en = TD_FALSE;
    vpss_grp_attr->ie_en = TD_FALSE;
    vpss_grp_attr->dci_en = TD_FALSE;
    vpss_grp_attr->dei_mode = OT_VPSS_DEI_MODE_OFF;
    vpss_grp_attr->buf_share_en = TD_FALSE;
}

static td_s32 uvc_media_vdec_bind_vpss(td_u32 vpss_grp_num)
{
    td_u32 i;
    td_s32 ret;

    for (i = 0; i < vpss_grp_num; i++) {
        ret = sample_comm_vdec_bind_vpss(i, i);
        if (ret != TD_SUCCESS) {
            sample_print("vdec bind vpss fail for %#x!\n", ret);
            return ret;
        }
    }
    return TD_SUCCESS;
}

static td_s32 uvc_media_vdec_unbind_vpss(td_u32 vpss_grp_num)
{
    td_u32 i;
    td_s32 ret = TD_SUCCESS;

    for (i = 0; i < vpss_grp_num; i++) {
        ret = sample_comm_vdec_un_bind_vpss(i, i);
        if (ret != TD_SUCCESS) {
            sample_print("vdec unbind vpss fail for %#x!\n", ret);
        }
    }
    return ret;
}

static td_void uvc_media_stop_vpss(ot_vpss_grp vpss_grp, td_bool *vpss_chn_enable, td_u32 chn_array_size)
{
    td_s32 i;

    for (i = vpss_grp; i >= 0; i--) {
        vpss_grp = i;
        sample_common_vpss_stop(vpss_grp, &vpss_chn_enable[0], chn_array_size);
    }
}

static td_s32 uvc_media_start_vdec(sample_vdec_attr *sample_vdec, td_u32 vdec_chn_num, td_u32 len)
{
    td_s32 ret;

    if (g_is_need_vdec == TD_FALSE) {
        return TD_SUCCESS;
    }

    ret = sample_comm_vdec_start(vdec_chn_num, &sample_vdec[0], len);
    if (ret != TD_SUCCESS) {
        sample_print("start VDEC fail for %#x!\n", ret);
    }
    return ret;
}

static td_void uvc_media_stop_vdec(td_u32 vdec_chn_num)
{
    if (g_is_need_vdec == TD_FALSE) {
        return;
    }

    sample_comm_vdec_stop(vdec_chn_num);
    if (g_mpp_attached != TD_TRUE) {
        sample_comm_vdec_exit_vb_pool();
    }
}

static td_s32 uvc_media_start_vpss(ot_vpss_grp *vpss_grp, td_u32 vpss_grp_num,
    td_bool *vpss_chn_enable, td_u32 arr_len)
{
    td_u32 i;
    td_s32 ret;
    ot_vpss_chn_attr vpss_chn_attr[OT_VPSS_MAX_CHN_NUM];
    ot_vpss_grp_attr vpss_grp_attr = {0};

    uvc_media_config_vpss_grp_attr(&vpss_grp_attr);
    (td_void)memset_s(vpss_chn_enable, arr_len * sizeof(td_bool), 0, arr_len * sizeof(td_bool));
    (td_void)memset_s(vpss_chn_attr, sizeof(vpss_chn_attr), 0, sizeof(vpss_chn_attr));

    /* ch1: 应用层取处理后帧 */
    vpss_chn_enable[1] = TD_TRUE;
    vpss_chn_attr[1].width = g_input_width;
    vpss_chn_attr[1].height = g_input_height;
    vpss_chn_attr[1].chn_mode = OT_VPSS_CHN_MODE_USER;
    vpss_chn_attr[1].compress_mode = OT_COMPRESS_MODE_NONE;
    vpss_chn_attr[1].pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    vpss_chn_attr[1].frame_rate.src_frame_rate = -1;
    vpss_chn_attr[1].frame_rate.dst_frame_rate = -1;
    vpss_chn_attr[1].depth = 2;
    vpss_chn_attr[1].mirror_en = TD_FALSE;
    vpss_chn_attr[1].flip_en = TD_FALSE;
    vpss_chn_attr[1].border_en = TD_FALSE;
    vpss_chn_attr[1].aspect_ratio.mode = OT_ASPECT_RATIO_NONE;

    for (i = 0; i < vpss_grp_num; i++) {
        *vpss_grp = i;
        ret = sample_common_vpss_start(*vpss_grp, &vpss_chn_enable[0],
            &vpss_grp_attr, vpss_chn_attr, OT_VPSS_MAX_CHN_NUM);
        if (ret != TD_SUCCESS) {
            sample_print("start VPSS fail for %#x!\n", ret);
            uvc_media_stop_vpss(*vpss_grp, &vpss_chn_enable[0], OT_VPSS_MAX_CHN_NUM);
            return ret;
        }
    }

    ret = uvc_media_vdec_bind_vpss(vpss_grp_num);
    if (ret != TD_SUCCESS) {
        uvc_media_vdec_unbind_vpss(vpss_grp_num);
        uvc_media_stop_vpss(*vpss_grp, &vpss_chn_enable[0], OT_VPSS_MAX_CHN_NUM);
    }

    return ret;
}

static ot_payload_type uvc_media_get_payload_type(const td_char *type_name)
{
    if (strcmp(type_name, "H264") == 0) {
        return OT_PT_H264;
    } else if (strcmp(type_name, "H265") == 0) {
        return OT_PT_H265;
    } else if (strcmp(type_name, "MJPEG") == 0) {
        return OT_PT_JPEG;
    }
    sample_print("type name error!\n");
    return OT_PT_BUTT;
}

static td_void uvc_media_update_vdec_flag(const td_char *type_name)
{
    if (type_name == TD_NULL) {
        g_is_need_vdec = TD_FALSE;
        return;
    }

    if ((strcmp(type_name, "H264") == 0) ||
        (strcmp(type_name, "H265") == 0) ||
        (strcmp(type_name, "MJPEG") == 0)) {
        g_is_need_vdec = TD_TRUE;
    } else {
        g_is_need_vdec = TD_FALSE;
    }
}

static td_bool uvc_media_is_raw_frame_type(const td_char *type_name)
{
    if (type_name == TD_NULL) {
        return TD_FALSE;
    }

    return (strcmp(type_name, "YUYV") == 0 ||
        strcmp(type_name, "NV12") == 0 ||
        strcmp(type_name, "NV21") == 0) ? TD_TRUE : TD_FALSE;
}

td_bool sample_uvc_media_direct_frame_enabled(td_void)
{
    return (g_mpp_attached == TD_TRUE && g_is_need_vdec == TD_FALSE) ? TD_TRUE : TD_FALSE;
}

td_s32 sample_uvc_media_init(const td_char *type_name, td_u32 width, td_u32 height,
    td_bool mpp_attached)
{
    td_s32 ret;
    td_u32 vdec_chn_num = 1;
    td_u32 vpss_grp_num = 1;
    sample_vdec_attr sample_vdec[OT_VDEC_MAX_CHN_NUM];
    td_bool vpss_chn_enable[OT_VPSS_MAX_CHN_NUM];
    ot_vpss_grp vpss_grp;
    ot_payload_type payload_type = OT_PT_H264;

    g_input_width = width;
    g_input_height = height;
    g_mpp_attached = mpp_attached;

    uvc_media_update_vdec_flag(type_name);

    if (g_mpp_attached == TD_TRUE && uvc_media_is_raw_frame_type(type_name) == TD_TRUE) {
        sample_print("attached raw mode: direct UVC to NV21 VB frames, no VDEC/VPSS\n");
        return TD_SUCCESS;
    }

    if (g_is_need_vdec == TD_TRUE) {
        payload_type = uvc_media_get_payload_type(type_name);
        if (payload_type == OT_PT_BUTT) {
            return TD_FAILURE;
        }
    }

    if (g_mpp_attached == TD_TRUE) {
        sample_print("attached mode: using existing SYS/VB and VDEC module VB pool\n");
    } else {
        ret = uvc_media_init_sys_and_vb(&sample_vdec[0], vdec_chn_num,
            payload_type, OT_VDEC_MAX_CHN_NUM);
        if (ret != TD_SUCCESS) {
            return ret;
        }
    }

    ret = uvc_media_start_vdec(&sample_vdec[0], vdec_chn_num, OT_VDEC_MAX_CHN_NUM);
    if (ret != TD_SUCCESS) {
        goto stop_sys;
    }

    ret = uvc_media_start_vpss(&vpss_grp, vpss_grp_num, &vpss_chn_enable[0], OT_VPSS_MAX_CHN_NUM);
    if (ret != TD_SUCCESS) {
        goto stop_vdec;
    }

    return TD_SUCCESS;

stop_vdec:
    uvc_media_stop_vdec(vdec_chn_num);
stop_sys:
    if (g_mpp_attached != TD_TRUE) {
        sample_comm_sys_exit();
    }
    return TD_FAILURE;
}

td_s32 sample_uvc_media_exit(td_void)
{
    td_u32 vdec_chn_num = 1;
    td_u32 vpss_grp_num = 1;
    ot_vpss_grp vpss_grp = 0;
    td_bool vpss_chn_enable[OT_VPSS_MAX_CHN_NUM] = {0};

    if (sample_uvc_media_direct_frame_enabled() == TD_TRUE) {
        return TD_SUCCESS;
    }

    vpss_chn_enable[1] = TD_TRUE;

    (td_void)uvc_media_vdec_unbind_vpss(vpss_grp_num);
    uvc_media_stop_vpss(vpss_grp, &vpss_chn_enable[0], OT_VPSS_MAX_CHN_NUM);
    uvc_media_stop_vdec(vdec_chn_num);
    if (g_mpp_attached != TD_TRUE) {
        sample_comm_sys_exit();
    }

    return TD_SUCCESS;
}

static td_void uvc_media_cut_stream_for_mjpeg(td_void *data, td_u32 size, td_s32 chn_id,
    td_s32 *read_num, td_u32 *start)
{
    td_s32 i;
    td_u32 len;
    td_s32 read_len = size;
    td_u8 *buf = data;
    td_bool find_start = TD_FALSE;

    for (i = 0; i < read_len - 1; i++) {
        if (buf[i] == 0xFF && buf[i + 1] == 0xD8) {
            *start = i;
            find_start = TD_TRUE;
            i += 2;
            break;
        }
    }

    for (; i < read_len - 3; i++) {
        if ((buf[i] == 0xFF) && (buf[i + 1] & 0xF0) == 0xE0) {
            len = (buf[i + 2] << 8) + buf[i + 3];
            i += 1 + len;
        } else {
            break;
        }
    }

    for (; i < read_len - 1; i++) {
        if (buf[i] == 0xFF && buf[i + 1] == 0xD9) {
            break;
        }
    }

    read_len = i + 2;
    if (find_start == TD_FALSE) {
        sample_print("chn %d can not find JPEG start code! read_len %d!\n", chn_id, read_len);
    }

    *read_num = read_len;
}

static td_void uvc_media_cut_stream_for_h264(td_void *data, td_u32 size, td_s32 chn_id, td_s32 *read_num)
{
    td_bool find_start = TD_FALSE;
    td_bool find_end = TD_FALSE;
    td_s32 i;
    td_u8 *buf = data;
    td_s32 read_len = size;
    const int min_block_len = 8;

    for (i = 0; i < read_len - min_block_len; i++) {
        int tmp = buf[i + 3] & 0x1F;
        if (buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1 &&
            (((tmp == 0x5 || tmp == 0x1) && ((buf[i + 4] & 0x80) == 0x80)) ||
            (tmp == 20 && (buf[i + 7] & 0x80) == 0x80))) {
            find_start = TD_TRUE;
            i += min_block_len;
            break;
        }
    }

    for (; i < read_len - min_block_len; i++) {
        int tmp = buf[i + 3] & 0x1F;
        if (buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1 &&
            (tmp == 15 || tmp == 7 || tmp == 8 || tmp == 6 ||
            ((tmp == 5 || tmp == 1) && ((buf[i + 4] & 0x80) == 0x80)) ||
            (tmp == 20 && (buf[i + 7] & 0x80) == 0x80))) {
            find_end = TD_TRUE;
            break;
        }
    }

    if (i > 0) {
        read_len = i;
    }

    if (find_start == TD_FALSE) {
        sample_print("chn %d can not find H264 start code! read_len %d!\n", chn_id, read_len);
    }

    if (find_end == TD_FALSE) {
        read_len = i + min_block_len;
    }

    *read_num = read_len;
}

static td_void uvc_media_cut_stream_for_h265(td_void *data, td_u32 size, td_s32 chn_id, td_s32 *read_num)
{
    td_bool find_start = TD_FALSE;
    td_bool find_end = TD_FALSE;
    td_bool new_pic = TD_FALSE;
    td_s32 i;
    td_u8 *buf = data;
    td_s32 read_len = size;

    for (i = 0; i < read_len - 6; i++) {
        td_u32 tmp = (buf[i + 3] & 0x7E) >> 1;
        new_pic = (buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1 &&
            (tmp <= 21) && ((buf[i + 5] & 0x80) == 0x80));

        if (new_pic) {
            find_start = TD_TRUE;
            i += 6;
            break;
        }
    }

    for (; i < read_len - 6; i++) {
        td_u32 tmp = (buf[i + 3] & 0x7E) >> 1;
        new_pic = (buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1 &&
            (tmp == 32 || tmp == 33 || tmp == 34 || tmp == 39 || tmp == 40 ||
            ((tmp <= 21) && (buf[i + 5] & 0x80) == 0x80)));

        if (new_pic) {
            find_end = TD_TRUE;
            break;
        }
    }

    if (i > 0) {
        read_len = i;
    }

    if (find_start == TD_FALSE) {
        sample_print("chn %d can not find H265 start code! read_len %d!\n", chn_id, read_len);
    }

    if (find_end == TD_FALSE) {
        read_len = i + 6;
    }

    *read_num = read_len;
}

static td_void uvc_media_yuyv_to_nv12(td_char *image_in, td_u32 width, td_u32 height,
    td_u32 size, td_char *image_out)
{
    td_u32 pixel_num = width * height;
    td_u32 cycle_num = size / pixel_num / 2;
    td_char *y = image_out;
    td_char *uv = image_out + pixel_num;
    td_char *start = image_in;
    td_u32 i = 0, j = 0, k = 0;

    for (i = 0; i < cycle_num; i++) {
        int index = 0;
        for (j = 0; j < pixel_num * 2; j += 2) {
            *(y + index) = *(start + j);
            index++;
        }
        start += pixel_num * 2;
        y += pixel_num * 3 / 2;
    }

    start = image_in;
    for (i = 0; i < cycle_num; i++) {
        int uv_index = 0;
        for (j = 0; j < height; j += 2) {
            for (k = j * width * 2 + 1; k < width * 2 * (j + 1); k += 4) {
                *(uv + uv_index) = *(start + k);
                *(uv + uv_index + 1) = *(start + k + 2);
                uv_index += 2;
            }
        }
        start += pixel_num * 2;
        uv += pixel_num * 3 / 2;
    }
}

static td_void uvc_media_buf_attr_init(const ot_size *pic_size, ot_pic_buf_attr *buf_attr)
{
    buf_attr->width = pic_size->width;
    buf_attr->height = pic_size->height;
    buf_attr->pixel_format = OT_PIXEL_FORMAT_YUV_SEMIPLANAR_420;
    buf_attr->compress_mode = OT_COMPRESS_MODE_NONE;
    buf_attr->align = 0;
    buf_attr->bit_width = OT_DATA_BIT_WIDTH_8;
}

static td_void uvc_media_update_vb_cfg(ot_pixel_format pixel_format, td_u32 stride,
    const ot_size *pic_size, ot_vb_calc_cfg *calc_cfg)
{
    if (pixel_format != OT_PIXEL_FORMAT_VY1UY0_PACKAGE_422) {
        return;
    }

    calc_cfg->main_stride = stride >> 1;
    calc_cfg->main_y_size = calc_cfg->main_stride * pic_size->height;
}

static td_s32 uvc_media_prepare_frame_info(ot_vb_blk vb_blk, const ot_pic_buf_attr *buf_attr,
    const ot_vb_calc_cfg *calc_cfg, ot_video_frame_info *video_frame)
{
    video_frame->video_frame.header_phys_addr[0] = ss_mpi_vb_handle_to_phys_addr(vb_blk);
    if (video_frame->video_frame.header_phys_addr[0] == TD_NULL) {
        return TD_FAILURE;
    }

    video_frame->video_frame.header_virt_addr[0] =
        (td_u8 *)ss_mpi_sys_mmap(video_frame->video_frame.header_phys_addr[0], calc_cfg->vb_size);
    if (video_frame->video_frame.header_virt_addr[0] == TD_NULL) {
        return TD_FAILURE;
    }

    video_frame->mod_id = OT_ID_VGS;
    video_frame->pool_id = ss_mpi_vb_handle_to_pool_id(vb_blk);

    video_frame->video_frame.header_phys_addr[1] =
        video_frame->video_frame.header_phys_addr[0] + calc_cfg->head_y_size;
    video_frame->video_frame.header_virt_addr[1] =
        video_frame->video_frame.header_virt_addr[0] + calc_cfg->head_y_size;
    video_frame->video_frame.phys_addr[0] =
        video_frame->video_frame.header_phys_addr[0] + calc_cfg->head_size;
    video_frame->video_frame.phys_addr[1] =
        video_frame->video_frame.phys_addr[0] + calc_cfg->main_y_size;
    video_frame->video_frame.virt_addr[0] =
        video_frame->video_frame.header_virt_addr[0] + calc_cfg->head_size;
    video_frame->video_frame.virt_addr[1] =
        video_frame->video_frame.virt_addr[0] + calc_cfg->main_y_size;
    video_frame->video_frame.header_stride[0] = calc_cfg->head_stride;
    video_frame->video_frame.header_stride[1] = calc_cfg->head_stride;
    video_frame->video_frame.stride[0] = calc_cfg->main_stride;
    video_frame->video_frame.stride[1] = calc_cfg->main_stride;

    video_frame->video_frame.width = buf_attr->width;
    video_frame->video_frame.height = buf_attr->height;
    video_frame->video_frame.dynamic_range = OT_DYNAMIC_RANGE_SDR8;
    video_frame->video_frame.compress_mode = OT_COMPRESS_MODE_NONE;
    video_frame->video_frame.video_format = OT_VIDEO_FORMAT_LINEAR;
    video_frame->video_frame.field = OT_VIDEO_FIELD_FRAME;
    video_frame->video_frame.pixel_format = OT_PIXEL_FORMAT_YUV_SEMIPLANAR_420;

    return TD_SUCCESS;
}

static td_s32 uvc_media_send_frame_to_vpss(td_void *data, td_u32 size, td_u32 stride,
    const ot_size *pic_size, ot_pixel_format pixel_format)
{
    td_s32 ret;
    ot_vpss_grp grp = 0;
    ot_video_frame_info frame_info = {0};
    ot_vb_blk vb_blk;
    ot_pic_buf_attr buf_attr = {0};
    ot_vb_calc_cfg calc_cfg = {0};

    uvc_media_buf_attr_init(pic_size, &buf_attr);
    ot_common_get_pic_buf_cfg(&buf_attr, &calc_cfg);
    uvc_media_update_vb_cfg(pixel_format, stride, pic_size, &calc_cfg);

    vb_blk = ss_mpi_vb_get_blk(OT_VB_INVALID_POOL_ID, calc_cfg.vb_size, TD_NULL);
    if (vb_blk == OT_VB_INVALID_HANDLE) {
        sample_print("get vb blk(size:%u) failed!\n", calc_cfg.vb_size);
        return TD_FAILURE;
    }

    ret = uvc_media_prepare_frame_info(vb_blk, &buf_attr, &calc_cfg, &frame_info);
    if (ret != TD_SUCCESS) {
        (td_void)ss_mpi_vb_release_blk(vb_blk);
        return ret;
    }

    if (pixel_format == OT_PIXEL_FORMAT_VY1UY0_PACKAGE_422) {
        uvc_media_yuyv_to_nv12(data, pic_size->width, pic_size->height, size,
            frame_info.video_frame.virt_addr[0]);
    } else {
        frame_info.video_frame.pixel_format = pixel_format;
        ret = memcpy_s(frame_info.video_frame.virt_addr[0], size, data, size);
        if (ret != EOK) {
            sample_print("memcpy_s video frame data fail %x\n", ret);
        }
    }

    ret = ss_mpi_vpss_send_frame(grp, &frame_info, -1);
    if (ret != TD_SUCCESS) {
        sample_print("send frame to vpss failed!\n");
    }

    ss_mpi_sys_munmap(frame_info.video_frame.virt_addr[0], calc_cfg.vb_size);
    (td_void)ss_mpi_vb_release_blk(vb_blk);

    return ret;
}

static td_s32 uvc_media_get_pixel_format(const td_char *type_name, ot_pixel_format *pixel_format)
{
    if (strcmp(type_name, "YUYV") == 0) {
        *pixel_format = OT_PIXEL_FORMAT_VY1UY0_PACKAGE_422;
    } else if (strcmp(type_name, "NV12") == 0) {
        *pixel_format = OT_PIXEL_FORMAT_YUV_SEMIPLANAR_420;
    } else if (strcmp(type_name, "NV21") == 0) {
        *pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    } else {
        sample_print("pixel format error!\n");
        return TD_FAILURE;
    }

    return TD_SUCCESS;
}

static td_s32 uvc_media_yuyv_to_nv21_direct(const td_u8 *src, td_u32 size, td_u32 src_stride,
    td_u32 width, td_u32 height, td_u8 *dst_y, td_u8 *dst_vu, td_u32 dst_y_stride,
    td_u32 dst_vu_stride)
{
    td_u32 row;
    td_u32 col;
    td_u32 min_src_stride;

    if (src == TD_NULL || dst_y == TD_NULL || dst_vu == TD_NULL ||
        width == 0 || height == 0 || (width & 1) != 0 || (height & 1) != 0) {
        return TD_FAILURE;
    }

    min_src_stride = width * 2;
    if (src_stride < min_src_stride) {
        src_stride = min_src_stride;
    }
    if ((td_u64)src_stride * height > size ||
        dst_y_stride < width || dst_vu_stride < width) {
        return TD_FAILURE;
    }

    for (row = 0; row < height; row++) {
        const td_u8 *src_row = src + (td_u64)row * src_stride;
        td_u8 *y_row = dst_y + (td_u64)row * dst_y_stride;
        for (col = 0; col < width; col += 2) {
            td_u32 off = col * 2;
            y_row[col] = src_row[off];
            y_row[col + 1] = src_row[off + 2];
        }
        if (dst_y_stride > width) {
            (td_void)memset_s(y_row + width, dst_y_stride - width, 0, dst_y_stride - width);
        }
    }

    for (row = 0; row < height / 2; row++) {
        const td_u8 *src_row = src + (td_u64)(row * 2) * src_stride;
        td_u8 *vu_row = dst_vu + (td_u64)row * dst_vu_stride;
        for (col = 0; col < width; col += 2) {
            td_u32 off = col * 2;
            vu_row[col] = src_row[off + 3];
            vu_row[col + 1] = src_row[off + 1];
        }
        if (dst_vu_stride > width) {
            (td_void)memset_s(vu_row + width, dst_vu_stride - width, 128, dst_vu_stride - width);
        }
    }

    return TD_SUCCESS;
}

static td_s32 uvc_media_semiplanar_to_nv21_direct(const td_u8 *src, td_u32 size, td_u32 src_stride,
    td_u32 width, td_u32 height, td_bool src_is_nv21, td_u8 *dst_y, td_u8 *dst_vu,
    td_u32 dst_y_stride, td_u32 dst_vu_stride)
{
    td_u32 row;
    td_u32 col;
    td_u64 y_size;
    const td_u8 *src_uv;

    if (src == TD_NULL || dst_y == TD_NULL || dst_vu == TD_NULL ||
        width == 0 || height == 0 || (width & 1) != 0 || (height & 1) != 0) {
        return TD_FAILURE;
    }

    if (src_stride < width) {
        src_stride = width;
    }
    y_size = (td_u64)src_stride * height;
    if (y_size + (td_u64)src_stride * height / 2 > size ||
        dst_y_stride < width || dst_vu_stride < width) {
        return TD_FAILURE;
    }
    src_uv = src + y_size;

    for (row = 0; row < height; row++) {
        errno_t ret = memcpy_s(dst_y + (td_u64)row * dst_y_stride, dst_y_stride,
            src + (td_u64)row * src_stride, width);
        if (ret != EOK) {
            return TD_FAILURE;
        }
        if (dst_y_stride > width) {
            (td_void)memset_s(dst_y + (td_u64)row * dst_y_stride + width,
                dst_y_stride - width, 0, dst_y_stride - width);
        }
    }

    for (row = 0; row < height / 2; row++) {
        const td_u8 *src_row = src_uv + (td_u64)row * src_stride;
        td_u8 *vu_row = dst_vu + (td_u64)row * dst_vu_stride;
        if (src_is_nv21 == TD_TRUE) {
            errno_t ret = memcpy_s(vu_row, dst_vu_stride, src_row, width);
            if (ret != EOK) {
                return TD_FAILURE;
            }
        } else {
            for (col = 0; col < width; col += 2) {
                vu_row[col] = src_row[col + 1];
                vu_row[col + 1] = src_row[col];
            }
        }
        if (dst_vu_stride > width) {
            (td_void)memset_s(vu_row + width, dst_vu_stride - width, 128, dst_vu_stride - width);
        }
    }

    return TD_SUCCESS;
}

static td_s32 uvc_media_copy_raw_to_nv21_frame(td_void *data, td_u32 size, td_u32 stride,
    const ot_size *pic_size, const td_char *type_name, ot_video_frame_info *frame_info)
{
    td_u8 *src = (td_u8 *)data;
    td_u8 *dst_y = frame_info->video_frame.virt_addr[0];
    td_u8 *dst_vu = frame_info->video_frame.virt_addr[1];

    if (strcmp(type_name, "YUYV") == 0) {
        return uvc_media_yuyv_to_nv21_direct(src, size, stride, pic_size->width, pic_size->height,
            dst_y, dst_vu, frame_info->video_frame.stride[0], frame_info->video_frame.stride[1]);
    } else if (strcmp(type_name, "NV12") == 0) {
        return uvc_media_semiplanar_to_nv21_direct(src, size, stride, pic_size->width, pic_size->height,
            TD_FALSE, dst_y, dst_vu, frame_info->video_frame.stride[0], frame_info->video_frame.stride[1]);
    } else if (strcmp(type_name, "NV21") == 0) {
        return uvc_media_semiplanar_to_nv21_direct(src, size, stride, pic_size->width, pic_size->height,
            TD_TRUE, dst_y, dst_vu, frame_info->video_frame.stride[0], frame_info->video_frame.stride[1]);
    }

    return TD_FAILURE;
}

td_s32 sample_uvc_media_build_direct_frame(td_void *data, td_u32 size, td_u32 stride,
    const ot_size *pic_size, const td_char *type_name, ot_video_frame_info *frame)
{
    td_s32 ret;
    ot_vb_blk vb_blk;
    ot_pic_buf_attr buf_attr = {0};
    ot_vb_calc_cfg calc_cfg = {0};
    ot_video_frame_info frame_info = {0};

    if (sample_uvc_media_direct_frame_enabled() != TD_TRUE ||
        data == TD_NULL || pic_size == TD_NULL || type_name == TD_NULL || frame == TD_NULL) {
        return TD_FAILURE;
    }

    uvc_media_buf_attr_init(pic_size, &buf_attr);
    buf_attr.pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    ot_common_get_pic_buf_cfg(&buf_attr, &calc_cfg);

    vb_blk = ss_mpi_vb_get_blk(OT_VB_INVALID_POOL_ID, calc_cfg.vb_size, TD_NULL);
    if (vb_blk == OT_VB_INVALID_HANDLE) {
        sample_print("direct frame get vb blk(size:%u) failed!\n", calc_cfg.vb_size);
        return TD_FAILURE;
    }

    ret = uvc_media_prepare_frame_info(vb_blk, &buf_attr, &calc_cfg, &frame_info);
    if (ret != TD_SUCCESS) {
        (td_void)ss_mpi_vb_release_blk(vb_blk);
        return ret;
    }
    frame_info.video_frame.pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;

    ret = uvc_media_copy_raw_to_nv21_frame(data, size, stride, pic_size, type_name, &frame_info);
    (td_void)ss_mpi_sys_munmap(frame_info.video_frame.header_virt_addr[0], calc_cfg.vb_size);
    frame_info.video_frame.header_virt_addr[0] = TD_NULL;
    frame_info.video_frame.header_virt_addr[1] = TD_NULL;
    frame_info.video_frame.virt_addr[0] = TD_NULL;
    frame_info.video_frame.virt_addr[1] = TD_NULL;

    if (ret != TD_SUCCESS) {
        (td_void)ss_mpi_vb_release_blk(vb_blk);
        return ret;
    }

    *frame = frame_info;
    return TD_SUCCESS;
}

td_s32 sample_uvc_media_send_data(td_void *data, td_u32 size, td_u32 stride,
    const ot_size *pic_size, const td_char *type_name)
{
    td_bool end_of_stream = TD_FALSE;
    td_s32 read_len = size;
    td_u8 *buf = data;
    ot_vdec_stream vdec_stream;
    td_u32 start = 0;
    td_s32 ret;
    td_s32 chn_id = 0;
    ot_pixel_format pixel_format;
    td_bool send_again = TD_TRUE;

    if (data == NULL || pic_size == NULL || type_name == NULL) {
        sample_print("data, pic_size or type_name is NULL\n");
        return TD_FAILURE;
    }

    if (strcmp(type_name, "MJPEG") == 0) {
        uvc_media_cut_stream_for_mjpeg(data, size, chn_id, &read_len, &start);
    } else if (strcmp(type_name, "H264") == 0) {
        uvc_media_cut_stream_for_h264(data, size, chn_id, &read_len);
    } else if (strcmp(type_name, "H265") == 0) {
        uvc_media_cut_stream_for_h265(data, size, chn_id, &read_len);
    } else {
        ret = uvc_media_get_pixel_format(type_name, &pixel_format);
        if (ret != TD_SUCCESS) {
            return ret;
        }
        return uvc_media_send_frame_to_vpss(data, size, stride, pic_size, pixel_format);
    }

    vdec_stream.addr = buf + start;
    vdec_stream.len = read_len;
    vdec_stream.end_of_frame = TD_TRUE;
    vdec_stream.end_of_stream = end_of_stream;
    vdec_stream.need_display = 1;

    while (send_again == TD_TRUE) {
        ss_mpi_sys_get_cur_pts(&vdec_stream.pts);
        ret = ss_mpi_vdec_send_stream(chn_id, &vdec_stream, 0);
        if (ret != TD_SUCCESS) {
            usleep(1000);
            continue;
        }
        send_again = TD_FALSE;
    }

    usleep(1000);
    return TD_SUCCESS;
}

td_s32 sample_uvc_media_stop_receive_data(td_void)
{
    ot_vdec_stream vdec_stream = {0};
    td_s32 vdec_chn_num = 1;
    td_s32 chn_id = 0;

    if (g_is_need_vdec == TD_FALSE) {
        return TD_SUCCESS;
    }

    vdec_stream.end_of_stream = TD_TRUE;
    ss_mpi_vdec_send_stream(chn_id, &vdec_stream, -1);
    ss_mpi_vdec_stop_recv_stream(vdec_chn_num);

    return TD_SUCCESS;
}

td_s32 sample_uvc_media_get_frame(ot_video_frame_info *frame, td_s32 milli_sec)
{
    if (frame == TD_NULL) {
        return TD_FAILURE;
    }

    if (sample_uvc_media_direct_frame_enabled() == TD_TRUE) {
        return TD_FAILURE;
    }

    return ss_mpi_vpss_get_chn_frame(0, 1, frame, milli_sec);
}

static td_s32 uvc_media_release_direct_frame(const ot_video_frame_info *frame)
{
    td_phys_addr_t phys_addr;
    ot_vb_blk vb_blk;

    if (frame == TD_NULL) {
        return TD_FAILURE;
    }

    phys_addr = frame->video_frame.header_phys_addr[0];
    if (phys_addr == 0) {
        phys_addr = frame->video_frame.phys_addr[0];
    }
    if (phys_addr == 0) {
        return TD_FAILURE;
    }

    vb_blk = ss_mpi_vb_phys_addr_to_handle(phys_addr);
    if (vb_blk == OT_VB_INVALID_HANDLE) {
        sample_print("direct frame phys addr to vb handle failed\n");
        return TD_FAILURE;
    }

    return ss_mpi_vb_release_blk(vb_blk);
}

td_s32 sample_uvc_media_release_frame(const ot_video_frame_info *frame)
{
    if (frame == TD_NULL) {
        return TD_FAILURE;
    }

    if (sample_uvc_media_direct_frame_enabled() == TD_TRUE) {
        return uvc_media_release_direct_frame(frame);
    }

    return ss_mpi_vpss_release_chn_frame(0, 1, frame);
}

static inline uint8_t clip_u8(int v)
{
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return (uint8_t)v;
}

/* NV21(Y + VU) -> RGB888 */
void nv21_to_rgb888_safe(const uint8_t *y_plane, const uint8_t *vu_plane,
    int width, int height, int y_stride, int vu_stride, uint8_t *rgb)
{
    int x, y;

    for (y = 0; y < height; y++) {
        const uint8_t *py = y_plane + y * y_stride;
        const uint8_t *pvu = vu_plane + (y / 2) * vu_stride;
        uint8_t *prgb = rgb + y * width * 3;

        for (x = 0; x < width; x++) {
            int Y = py[x];
            int vu_index = (x / 2) * 2;
            int V = pvu[vu_index + 0] - 128;
            int U = pvu[vu_index + 1] - 128;

            int R = (int)(Y + 1.402 * V);
            int G = (int)(Y - 0.344136 * U - 0.714136 * V);
            int B = (int)(Y + 1.772 * U);

            prgb[x * 3 + 0] = clip_u8(R);
            prgb[x * 3 + 1] = clip_u8(G);
            prgb[x * 3 + 2] = clip_u8(B);
        }
    }
}
