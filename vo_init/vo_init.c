#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

#include "sample_comm.h"
#include "sdk_module_init.h"
#include "securec.h"
#include "ot_common_vb.h"
#include "ot_common_video.h"
#include "ot_common_sys.h"
#include "ss_mpi_sys.h"
#include "gfbg.h"

#define HDMI_FB_DEV "/dev/fb0"
#define FB_WIDTH    1280
#define FB_HEIGHT   720
#define FB_BPP      16

typedef struct {
    int fb_fd;
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    ot_fb_layer_info layer_info;

    td_phys_addr_t canvas_phys_addr;
    void *canvas_virt_addr;
    td_u32 canvas_size;
    ot_fb_buf canvas_buf;
} app_fb_ctx;

static sample_vo_cfg g_vo_cfg = {
    .vo_dev            = SAMPLE_VO_DEV_UHD,
    .vo_intf_type      = OT_VO_INTF_HDMI,
    .intf_sync         = OT_VO_OUT_720P60,
    .bg_color          = COLOR_RGB_BLACK,
    .pix_format        = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420,
    .disp_rect         = {0, 0, 1280, 720},
    .image_size        = {1280, 720},
    .vo_part_mode      = OT_VO_PARTITION_MODE_SINGLE,
    .dis_buf_len       = 3,
    .dst_dynamic_range = OT_DYNAMIC_RANGE_SDR8,
    .vo_mode           = VO_MODE_1MUX,
    .compress_mode     = OT_COMPRESS_MODE_NONE,
};

static app_fb_ctx g_fb = {
    .fb_fd = -1,
    .canvas_phys_addr = 0,
    .canvas_virt_addr = NULL,
    .canvas_size = 0,
};

static td_u32 get_vo_vb_blk_size(td_u32 width, td_u32 height)
{
    ot_pic_buf_attr buf_attr;
    ot_vb_calc_cfg calc_cfg;

    (td_void)memset_s(&buf_attr, sizeof(buf_attr), 0, sizeof(buf_attr));
    (td_void)memset_s(&calc_cfg, sizeof(calc_cfg), 0, sizeof(calc_cfg));

    buf_attr.width         = width;
    buf_attr.height        = height;
    buf_attr.align         = OT_DEFAULT_ALIGN;
    buf_attr.bit_width     = OT_DATA_BIT_WIDTH_8;
    buf_attr.pixel_format  = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    buf_attr.compress_mode = OT_COMPRESS_MODE_NONE;

    ot_common_get_pic_buf_cfg(&buf_attr, &calc_cfg);
    return calc_cfg.vb_size;
}

static int init_sys_and_vb(void)
{
    td_s32 ret;
    ot_vb_cfg vb_cfg;

    (td_void)memset_s(&vb_cfg, sizeof(vb_cfg), 0, sizeof(vb_cfg));

    vb_cfg.max_pool_cnt = 128;
    vb_cfg.common_pool[0].blk_size = get_vo_vb_blk_size(FB_WIDTH, FB_HEIGHT);
    vb_cfg.common_pool[0].blk_cnt  = 6;

    ret = sample_comm_sys_init(&vb_cfg);
    if (ret != TD_SUCCESS) {
        printf("sample_comm_sys_init failed: %#x\n", ret);
        return -1;
    }

    printf("SYS+VB init ok: blk_size=%llu blk_cnt=%u\n",
           (unsigned long long)vb_cfg.common_pool[0].blk_size,
           vb_cfg.common_pool[0].blk_cnt);
    return 0;
}

static void deinit_sys_and_vb(void)
{
    sample_comm_sys_exit();
}

static int start_hdmi_vo(void)
{
    td_s32 ret;

    ret = sample_comm_vo_start_vo(&g_vo_cfg);
    if (ret != TD_SUCCESS) {
        printf("sample_comm_vo_start_vo failed: %#x\n", ret);
        return -1;
    }

    printf("VO started: HDMI %dx%d @ 60Hz\n",
           g_vo_cfg.image_size.width, g_vo_cfg.image_size.height);
    return 0;
}

static void stop_hdmi_vo(void)
{
    sample_comm_vo_stop_vo(&g_vo_cfg);
}

static void fill_argb1555_color(unsigned short *buf, int width, int height,
                                int stride_pixels, unsigned short color)
{
    int x, y;
    for (y = 0; y < height; ++y) {
        unsigned short *row = buf + y * stride_pixels;
        for (x = 0; x < width; ++x) {
            row[x] = color;
        }
    }
}

static void draw_test_pattern_argb1555(unsigned short *buf, int width, int height, int stride_pixels)
{
    int x, y;
    int bar_w = width / 4;

    const unsigned short black = 0x8000;
    const unsigned short red   = 0xFC00;
    const unsigned short green = 0x83E0;
    const unsigned short blue  = 0x801F;
    const unsigned short white = 0xFFFF;

    fill_argb1555_color(buf, width, height, stride_pixels, black);

    for (y = 0; y < height; ++y) {
        unsigned short *row = buf + y * stride_pixels;
        for (x = 0; x < width; ++x) {
            if (x < bar_w) {
                row[x] = red;
            } else if (x < bar_w * 2) {
                row[x] = green;
            } else if (x < bar_w * 3) {
                row[x] = blue;
            } else {
                row[x] = white;
            }
        }
    }
}

static int open_and_config_fb0(app_fb_ctx *ctx)
{
    td_bool is_show = TD_FALSE;
    ot_fb_point point = {0, 0};

    ctx->fb_fd = open(HDMI_FB_DEV, O_RDWR);
    if (ctx->fb_fd < 0) {
        perror("open /dev/fb0 failed");
        return -1;
    }

    if (ioctl(ctx->fb_fd, FBIOGET_FSCREENINFO, &ctx->fix) < 0) {
        perror("FBIOGET_FSCREENINFO failed");
        close(ctx->fb_fd);
        ctx->fb_fd = -1;
        return -1;
    }

    if (ioctl(ctx->fb_fd, FBIOGET_VSCREENINFO, &ctx->var) < 0) {
        perror("FBIOGET_VSCREENINFO failed");
        close(ctx->fb_fd);
        ctx->fb_fd = -1;
        return -1;
    }

    if (ioctl(ctx->fb_fd, FBIOPUT_SHOW_GFBG, &is_show) < 0) {
        perror("FBIOPUT_SHOW_GFBG hide failed");
        close(ctx->fb_fd);
        ctx->fb_fd = -1;
        return -1;
    }

    if (ioctl(ctx->fb_fd, FBIOPUT_SCREEN_ORIGIN_GFBG, &point) < 0) {
        perror("FBIOPUT_SCREEN_ORIGIN_GFBG failed");
        close(ctx->fb_fd);
        ctx->fb_fd = -1;
        return -1;
    }

    ctx->var.xres = FB_WIDTH;
    ctx->var.yres = FB_HEIGHT;
    ctx->var.xres_virtual = FB_WIDTH;
    ctx->var.yres_virtual = FB_HEIGHT;
    ctx->var.xoffset = 0;
    ctx->var.yoffset = 0;
    ctx->var.bits_per_pixel = FB_BPP;

    /* ARGB1555 */
    ctx->var.transp.offset = 15;
    ctx->var.transp.length = 1;
    ctx->var.red.offset    = 10;
    ctx->var.red.length    = 5;
    ctx->var.green.offset  = 5;
    ctx->var.green.length  = 5;
    ctx->var.blue.offset   = 0;
    ctx->var.blue.length   = 5;
    ctx->var.activate      = FB_ACTIVATE_NOW;

    if (ioctl(ctx->fb_fd, FBIOPUT_VSCREENINFO, &ctx->var) < 0) {
        perror("FBIOPUT_VSCREENINFO failed");
        close(ctx->fb_fd);
        ctx->fb_fd = -1;
        return -1;
    }

    if (ioctl(ctx->fb_fd, FBIOGET_FSCREENINFO, &ctx->fix) < 0) {
        perror("FBIOGET_FSCREENINFO(2) failed");
        close(ctx->fb_fd);
        ctx->fb_fd = -1;
        return -1;
    }

    if (ioctl(ctx->fb_fd, FBIOGET_VSCREENINFO, &ctx->var) < 0) {
        perror("FBIOGET_VSCREENINFO(2) failed");
        close(ctx->fb_fd);
        ctx->fb_fd = -1;
        return -1;
    }

    memset(&ctx->layer_info, 0, sizeof(ctx->layer_info));
    if (ioctl(ctx->fb_fd, FBIOGET_LAYER_INFO, &ctx->layer_info) < 0) {
        perror("FBIOGET_LAYER_INFO failed");
        close(ctx->fb_fd);
        ctx->fb_fd = -1;
        return -1;
    }

    ctx->layer_info.buf_mode = OT_FB_LAYER_BUF_DOUBLE;
    ctx->layer_info.mask = OT_FB_LAYER_MASK_BUF_MODE;

    if (ioctl(ctx->fb_fd, FBIOPUT_LAYER_INFO, &ctx->layer_info) < 0) {
        perror("FBIOPUT_LAYER_INFO failed");
        close(ctx->fb_fd);
        ctx->fb_fd = -1;
        return -1;
    }

    {
        ot_fb_rotate_mode rotate_mode = OT_FB_ROTATE_180;
        if (ioctl(ctx->fb_fd, FBIOPUT_ROTATE_MODE, &rotate_mode) < 0) {
            perror("FBIOPUT_ROTATE_MODE failed");
            close(ctx->fb_fd);
            ctx->fb_fd = -1;
            return -1;
        }
    }

    ctx->canvas_size = ctx->fix.line_length * ctx->var.yres;
    if (ss_mpi_sys_mmz_alloc(&ctx->canvas_phys_addr,
                             &ctx->canvas_virt_addr,
                             TD_NULL, TD_NULL,
                             ctx->canvas_size) != TD_SUCCESS) {
        printf("ss_mpi_sys_mmz_alloc failed\n");
        close(ctx->fb_fd);
        ctx->fb_fd = -1;
        return -1;
    }

    if (memset_s(ctx->canvas_virt_addr, ctx->canvas_size, 0x00, ctx->canvas_size) != EOK) {
        printf("memset_s failed\n");
        ss_mpi_sys_mmz_free(ctx->canvas_phys_addr, ctx->canvas_virt_addr);
        ctx->canvas_phys_addr = 0;
        ctx->canvas_virt_addr = NULL;
        close(ctx->fb_fd);
        ctx->fb_fd = -1;
        return -1;
    }

    memset(&ctx->canvas_buf, 0, sizeof(ctx->canvas_buf));
    ctx->canvas_buf.canvas.phys_addr = ctx->canvas_phys_addr;
    ctx->canvas_buf.canvas.width = ctx->var.xres;
    ctx->canvas_buf.canvas.height = ctx->var.yres;
    ctx->canvas_buf.canvas.pitch = ctx->fix.line_length;
    ctx->canvas_buf.canvas.format = OT_FB_FORMAT_ARGB1555;
    ctx->canvas_buf.update_rect.x = 0;
    ctx->canvas_buf.update_rect.y = 0;
    ctx->canvas_buf.update_rect.width = ctx->var.xres;
    ctx->canvas_buf.update_rect.height = ctx->var.yres;

    is_show = TD_TRUE;
    if (ioctl(ctx->fb_fd, FBIOPUT_SHOW_GFBG, &is_show) < 0) {
        perror("FBIOPUT_SHOW_GFBG show failed");
        ss_mpi_sys_mmz_free(ctx->canvas_phys_addr, ctx->canvas_virt_addr);
        ctx->canvas_phys_addr = 0;
        ctx->canvas_virt_addr = NULL;
        close(ctx->fb_fd);
        ctx->fb_fd = -1;
        return -1;
    }
    

    printf("fb0 configured:\n");
    printf("  xres=%u yres=%u\n", ctx->var.xres, ctx->var.yres);
    printf("  xres_virtual=%u yres_virtual=%u\n",
           ctx->var.xres_virtual, ctx->var.yres_virtual);
    printf("  bpp=%u line_length=%u\n",
           ctx->var.bits_per_pixel, ctx->fix.line_length);
    printf("  canvas_size=%u\n", ctx->canvas_size);

    return 0;
}

static void close_fb0(app_fb_ctx *ctx)
{
    td_bool is_show = TD_FALSE;

    if (ctx->fb_fd >= 0) {
        (void)ioctl(ctx->fb_fd, FBIOPUT_SHOW_GFBG, &is_show);
    }

    if (ctx->canvas_phys_addr && ctx->canvas_virt_addr) {
        ss_mpi_sys_mmz_free(ctx->canvas_phys_addr, ctx->canvas_virt_addr);
        ctx->canvas_phys_addr = 0;
        ctx->canvas_virt_addr = NULL;
    }

    if (ctx->fb_fd >= 0) {
        close(ctx->fb_fd);
        ctx->fb_fd = -1;
    }
}

static int show_test_pattern(app_fb_ctx *ctx)
{
    int stride_pixels;

    if (ctx->canvas_virt_addr == NULL) {
        return -1;
    }

    stride_pixels = ctx->fix.line_length / 2;

    draw_test_pattern_argb1555((unsigned short *)ctx->canvas_virt_addr,
                               ctx->var.xres,
                               ctx->var.yres,
                               stride_pixels);

    if (ioctl(ctx->fb_fd, FBIO_REFRESH, &ctx->canvas_buf) < 0) {
        perror("FBIO_REFRESH failed");
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int ret = 0;

    (void)argc;
    (void)argv;

#ifdef CONFIG_USER_SPACE
    SDK_init();
#endif

    ret = init_sys_and_vb();
    if (ret != 0) {
        goto exit_sdk;
    }

    ret = start_hdmi_vo();
    if (ret != 0) {
        goto exit_sys;
    }

    ret = open_and_config_fb0(&g_fb);
    if (ret != 0) {
        goto stop_vo;
    }

    ret = show_test_pattern(&g_fb);
    if (ret != 0) {
        goto close_fb;
    }

    printf("\nVO + GFBG init done.\n");
    printf("Qt can now run with:\n");
    printf("  export QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0\n");
    printf("\nPress Enter to exit...\n");
    getchar();

close_fb:
    close_fb0(&g_fb);

stop_vo:
    stop_hdmi_vo();

exit_sys:
    deinit_sys_and_vb();

exit_sdk:
#ifdef CONFIG_USER_SPACE
    SDK_exit();
#endif
    return ret;
}