#include "vision_display.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "gfbg.h"
#include "sample_comm.h"
#include "securec.h"
#include "ss_mpi_sys.h"

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
    td_bool vo_started;
    td_char ready_file[256];
} vision_display_context;

static sample_vo_cfg g_vo_cfg = {
    .vo_dev            = SAMPLE_VO_DEV_UHD,
    .vo_intf_type      = OT_VO_INTF_HDMI,
    .intf_sync         = OT_VO_OUT_720P60,
    .bg_color          = COLOR_RGB_BLACK,
    .pix_format        = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420,
    .disp_rect         = {0, 0, FB_WIDTH, FB_HEIGHT},
    .image_size        = {FB_WIDTH, FB_HEIGHT},
    .vo_part_mode      = OT_VO_PARTITION_MODE_SINGLE,
    .dis_buf_len       = 3,
    .dst_dynamic_range = OT_DYNAMIC_RANGE_SDR8,
    .vo_mode           = VO_MODE_1MUX,
    .compress_mode     = OT_COMPRESS_MODE_NONE,
};

static vision_display_context g_display = {
    .fb_fd = -1,
    .canvas_phys_addr = 0,
    .canvas_virt_addr = NULL,
    .canvas_size = 0,
    .vo_started = TD_FALSE,
    .ready_file = {0},
};

static int start_hdmi_vo(void)
{
    td_s32 ret = sample_comm_vo_start_vo(&g_vo_cfg);

    if (ret != TD_SUCCESS) {
        printf("vision display: sample_comm_vo_start_vo failed: %#x\n", ret);
        return -1;
    }

    printf("vision display: VO started HDMI %dx%d @ 60Hz\n",
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
    int x;
    int y;

    for (y = 0; y < height; ++y) {
        unsigned short *row = buf + y * stride_pixels;
        for (x = 0; x < width; ++x) {
            row[x] = color;
        }
    }
}

static void draw_test_pattern_argb1555(unsigned short *buf, int width, int height,
    int stride_pixels)
{
    int x;
    int y;
    int bar_w = width / 4;
    const unsigned short black = 0x8000;
    const unsigned short red = 0xFC00;
    const unsigned short green = 0x83E0;
    const unsigned short blue = 0x801F;
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

static int display_close_on_error(vision_display_context *ctx)
{
    if (ctx->fb_fd >= 0) {
        close(ctx->fb_fd);
        ctx->fb_fd = -1;
    }
    return -1;
}

static int open_and_config_fb0(vision_display_context *ctx)
{
    td_bool is_show = TD_FALSE;
    ot_fb_point point = {0, 0};

    ctx->fb_fd = open(HDMI_FB_DEV, O_RDWR);
    if (ctx->fb_fd < 0) {
        perror("vision display: open /dev/fb0 failed");
        return -1;
    }

    if (ioctl(ctx->fb_fd, FBIOGET_FSCREENINFO, &ctx->fix) < 0 ||
        ioctl(ctx->fb_fd, FBIOGET_VSCREENINFO, &ctx->var) < 0) {
        perror("vision display: get fb info failed");
        return display_close_on_error(ctx);
    }

    if (ioctl(ctx->fb_fd, FBIOPUT_SHOW_GFBG, &is_show) < 0 ||
        ioctl(ctx->fb_fd, FBIOPUT_SCREEN_ORIGIN_GFBG, &point) < 0) {
        perror("vision display: configure GFBG origin/show failed");
        return display_close_on_error(ctx);
    }

    ctx->var.xres = FB_WIDTH;
    ctx->var.yres = FB_HEIGHT;
    ctx->var.xres_virtual = FB_WIDTH;
    ctx->var.yres_virtual = FB_HEIGHT;
    ctx->var.xoffset = 0;
    ctx->var.yoffset = 0;
    ctx->var.bits_per_pixel = FB_BPP;
    ctx->var.transp.offset = 15;
    ctx->var.transp.length = 1;
    ctx->var.red.offset = 10;
    ctx->var.red.length = 5;
    ctx->var.green.offset = 5;
    ctx->var.green.length = 5;
    ctx->var.blue.offset = 0;
    ctx->var.blue.length = 5;
    ctx->var.activate = FB_ACTIVATE_NOW;

    if (ioctl(ctx->fb_fd, FBIOPUT_VSCREENINFO, &ctx->var) < 0 ||
        ioctl(ctx->fb_fd, FBIOGET_FSCREENINFO, &ctx->fix) < 0 ||
        ioctl(ctx->fb_fd, FBIOGET_VSCREENINFO, &ctx->var) < 0) {
        perror("vision display: set fb info failed");
        return display_close_on_error(ctx);
    }

    (void)memset_s(&ctx->layer_info, sizeof(ctx->layer_info), 0, sizeof(ctx->layer_info));
    if (ioctl(ctx->fb_fd, FBIOGET_LAYER_INFO, &ctx->layer_info) < 0) {
        perror("vision display: FBIOGET_LAYER_INFO failed");
        return display_close_on_error(ctx);
    }

    ctx->layer_info.buf_mode = OT_FB_LAYER_BUF_DOUBLE;
    ctx->layer_info.mask = OT_FB_LAYER_MASK_BUF_MODE;
    if (ioctl(ctx->fb_fd, FBIOPUT_LAYER_INFO, &ctx->layer_info) < 0) {
        perror("vision display: FBIOPUT_LAYER_INFO failed");
        return display_close_on_error(ctx);
    }

    {
        ot_fb_rotate_mode rotate_mode = OT_FB_ROTATE_180;
        if (ioctl(ctx->fb_fd, FBIOPUT_ROTATE_MODE, &rotate_mode) < 0) {
            perror("vision display: FBIOPUT_ROTATE_MODE failed");
            return display_close_on_error(ctx);
        }
    }

    ctx->canvas_size = ctx->fix.line_length * ctx->var.yres;
    if (ss_mpi_sys_mmz_alloc(&ctx->canvas_phys_addr, &ctx->canvas_virt_addr,
        TD_NULL, TD_NULL, ctx->canvas_size) != TD_SUCCESS) {
        printf("vision display: ss_mpi_sys_mmz_alloc failed\n");
        return display_close_on_error(ctx);
    }

    if (memset_s(ctx->canvas_virt_addr, ctx->canvas_size, 0x00, ctx->canvas_size) != EOK) {
        printf("vision display: memset_s canvas failed\n");
        ss_mpi_sys_mmz_free(ctx->canvas_phys_addr, ctx->canvas_virt_addr);
        ctx->canvas_phys_addr = 0;
        ctx->canvas_virt_addr = NULL;
        return display_close_on_error(ctx);
    }

    (void)memset_s(&ctx->canvas_buf, sizeof(ctx->canvas_buf), 0, sizeof(ctx->canvas_buf));
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
        perror("vision display: FBIOPUT_SHOW_GFBG show failed");
        ss_mpi_sys_mmz_free(ctx->canvas_phys_addr, ctx->canvas_virt_addr);
        ctx->canvas_phys_addr = 0;
        ctx->canvas_virt_addr = NULL;
        return display_close_on_error(ctx);
    }

    printf("vision display: fb0 configured %ux%u bpp=%u line_length=%u\n",
        ctx->var.xres, ctx->var.yres, ctx->var.bits_per_pixel, ctx->fix.line_length);
    return 0;
}

static void close_fb0(vision_display_context *ctx)
{
    td_bool is_show = TD_FALSE;

    if (ctx->fb_fd >= 0) {
        (void)ioctl(ctx->fb_fd, FBIOPUT_SHOW_GFBG, &is_show);
    }
    if (ctx->canvas_phys_addr != 0 && ctx->canvas_virt_addr != NULL) {
        ss_mpi_sys_mmz_free(ctx->canvas_phys_addr, ctx->canvas_virt_addr);
        ctx->canvas_phys_addr = 0;
        ctx->canvas_virt_addr = NULL;
    }
    if (ctx->fb_fd >= 0) {
        close(ctx->fb_fd);
        ctx->fb_fd = -1;
    }
}

static int show_test_pattern(vision_display_context *ctx)
{
    int stride_pixels;

    if (ctx->canvas_virt_addr == NULL) {
        return -1;
    }

    stride_pixels = ctx->fix.line_length / 2;
    draw_test_pattern_argb1555((unsigned short *)ctx->canvas_virt_addr,
        ctx->var.xres, ctx->var.yres, stride_pixels);

    if (ioctl(ctx->fb_fd, FBIO_REFRESH, &ctx->canvas_buf) < 0) {
        perror("vision display: FBIO_REFRESH failed");
        return -1;
    }

    return 0;
}

static td_s32 write_ready_file(vision_display_context *ctx, const td_char *ready_file)
{
    FILE *file;
    errno_t sret;

    if (ready_file == TD_NULL || ready_file[0] == '\0') {
        ctx->ready_file[0] = '\0';
        return TD_SUCCESS;
    }

    file = fopen(ready_file, "w");
    if (file == TD_NULL) {
        perror("vision display: create ready file");
        return TD_FAILURE;
    }
    (void)fprintf(file, "%ld\n", (long)getpid());
    (void)fclose(file);

    sret = strncpy_s(ctx->ready_file, sizeof(ctx->ready_file),
        ready_file, sizeof(ctx->ready_file) - 1);
    if (sret != EOK) {
        ctx->ready_file[0] = '\0';
    }
    return TD_SUCCESS;
}

td_s32 vision_display_start(const td_char *ready_file)
{
    if (g_display.vo_started == TD_TRUE || g_display.fb_fd >= 0) {
        return TD_SUCCESS;
    }

    if (start_hdmi_vo() != 0) {
        return TD_FAILURE;
    }
    g_display.vo_started = TD_TRUE;

    if (open_and_config_fb0(&g_display) != 0) {
        vision_display_stop();
        return TD_FAILURE;
    }

    // if (show_test_pattern(&g_display) != 0) {
    //     vision_display_stop();
    //     return TD_FAILURE;
    // }
    if (ioctl(g_display.fb_fd, FBIO_REFRESH, &g_display.canvas_buf) < 0) {
        perror("vision display: FBIO_REFRESH failed");
    }

    if (write_ready_file(&g_display, ready_file) != TD_SUCCESS) {
        vision_display_stop();
        return TD_FAILURE;
    }

    printf("vision display: VO + GFBG ready\n");
    fflush(stdout);
    return TD_SUCCESS;
}

td_void vision_display_stop(td_void)
{
    if (g_display.ready_file[0] != '\0') {
        (void)unlink(g_display.ready_file);
        g_display.ready_file[0] = '\0';
    }

    close_fb0(&g_display);

    if (g_display.vo_started == TD_TRUE) {
        stop_hdmi_vo();
        g_display.vo_started = TD_FALSE;
    }
}
