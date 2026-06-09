#include "vision_uvc.h"
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/uvcvideo.h>
#include <linux/usb/video.h>
#include "vision_media.h"
#include "sdk_module_init.h"

static volatile td_bool g_uvc_exit = TD_FALSE;

static td_void uvc_lite_exit_signal_handler(td_s32 signal __attribute__((__unused__)))
{
    g_uvc_exit = TD_TRUE;
}

#ifndef V4L2_PIX_FMT_HEVC
#define V4L2_PIX_FMT_HEVC v4l2_fourcc('H', 'E', 'V', 'C')
#endif

static td_s32 uvc_lite_xioctl(td_s32 fd, td_s32 request, void *arg)
{
    td_s32 ret;
    do {
        ret = ioctl(fd, request, arg);
    } while ((ret == -1) && (errno == EINTR));
    return ret;
}

static td_s32 uvc_lite_open(uvc_lite_ctx *ctx, const td_char *dev_name)
{
    ctx->fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);
    if (ctx->fd < 0) {
        sample_print("open %s failed\n", dev_name);
        return TD_FAILURE;
    }
    return TD_SUCCESS;
}

static td_u32 uvc_lite_get_v4l2_format(const td_char *type_name)
{
    if (strcmp(type_name, "MJPEG") == 0) {
        return V4L2_PIX_FMT_MJPEG;
    } else if (strcmp(type_name, "H264") == 0) {
        return V4L2_PIX_FMT_H264;
    } else if (strcmp(type_name, "H265") == 0) {
        return V4L2_PIX_FMT_HEVC;
    } else if (strcmp(type_name, "YUYV") == 0) {
        return V4L2_PIX_FMT_YUYV;
    } else if (strcmp(type_name, "NV12") == 0) {
        return V4L2_PIX_FMT_NV12;
    } else if (strcmp(type_name, "NV21") == 0) {
        return V4L2_PIX_FMT_NV21;
    }
    return 0;
}

static td_s32 uvc_lite_set_format(uvc_lite_ctx *ctx, const td_char *type_name, td_u32 width, td_u32 height)
{
    struct v4l2_format fmt;
    td_u32 pixfmt;
    errno_t ret;

    pixfmt = uvc_lite_get_v4l2_format(type_name);
    if (pixfmt == 0) {
        sample_print("unsupported type %s\n", type_name);
        return TD_FAILURE;
    }

    ret = memset_s(&fmt, sizeof(fmt), 0, sizeof(fmt));
    if (ret != EOK) {
        return TD_FAILURE;
    }

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = pixfmt;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (uvc_lite_xioctl(ctx->fd, VIDIOC_S_FMT, &fmt) < 0) {
        sample_print("VIDIOC_S_FMT failed\n");
        return TD_FAILURE;
    }

    ctx->width = fmt.fmt.pix.width;
    ctx->height = fmt.fmt.pix.height;
    ctx->stride = fmt.fmt.pix.bytesperline;
    ctx->pixelformat = fmt.fmt.pix.pixelformat;

    return TD_SUCCESS;
}

static td_s32 uvc_lite_reqbufs(uvc_lite_ctx *ctx, td_u32 req_count)
{
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;
    td_u32 i;
    errno_t ret;

    ret = memset_s(&req, sizeof(req), 0, sizeof(req));
    if (ret != EOK) {
        return TD_FAILURE;
    }

    req.count = req_count;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (uvc_lite_xioctl(ctx->fd, VIDIOC_REQBUFS, &req) < 0) {
        sample_print("VIDIOC_REQBUFS failed\n");
        return TD_FAILURE;
    }

    if (req.count < 2 || req.count > UVC_LITE_MAX_BUFFERS) {
        sample_print("invalid req.count=%u\n", req.count);
        return TD_FAILURE;
    }

    ctx->buffer_count = req.count;

    for (i = 0; i < ctx->buffer_count; i++) {
        ret = memset_s(&buf, sizeof(buf), 0, sizeof(buf));
        if (ret != EOK) {
            return TD_FAILURE;
        }

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (uvc_lite_xioctl(ctx->fd, VIDIOC_QUERYBUF, &buf) < 0) {
            sample_print("VIDIOC_QUERYBUF failed\n");
            return TD_FAILURE;
        }

        ctx->buffers[i].length = buf.length;
        ctx->buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->fd, buf.m.offset);
        if (ctx->buffers[i].start == MAP_FAILED) {
            ctx->buffers[i].start = TD_NULL;
            sample_print("mmap failed\n");
            return TD_FAILURE;
        }
    }

    return TD_SUCCESS;
}

static td_s32 uvc_lite_queue_all(uvc_lite_ctx *ctx)
{
    struct v4l2_buffer buf;
    td_u32 i;
    errno_t ret;

    for (i = 0; i < ctx->buffer_count; i++) {
        ret = memset_s(&buf, sizeof(buf), 0, sizeof(buf));
        if (ret != EOK) {
            return TD_FAILURE;
        }

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (uvc_lite_xioctl(ctx->fd, VIDIOC_QBUF, &buf) < 0) {
            sample_print("VIDIOC_QBUF failed\n");
            return TD_FAILURE;
        }
    }

    return TD_SUCCESS;
}

static td_s32 uvc_lite_stream_on(uvc_lite_ctx *ctx)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (uvc_lite_xioctl(ctx->fd, VIDIOC_STREAMON, &type) < 0) {
        sample_print("VIDIOC_STREAMON failed\n");
        return TD_FAILURE;
    }
    return TD_SUCCESS;
}

static td_void uvc_lite_stream_off(uvc_lite_ctx *ctx)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ctx->fd >= 0) {
        (td_void)uvc_lite_xioctl(ctx->fd, VIDIOC_STREAMOFF, &type);
    }
}

static td_void uvc_lite_close(uvc_lite_ctx *ctx)
{
    td_u32 i;

    for (i = 0; i < ctx->buffer_count; i++) {
        if (ctx->buffers[i].start != TD_NULL && ctx->buffers[i].start != MAP_FAILED) {
            (td_void)munmap(ctx->buffers[i].start, ctx->buffers[i].length);
            ctx->buffers[i].start = TD_NULL;
        }
    }

    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }
}

static td_s32 uvc_lite_loop(uvc_lite_ctx *ctx, const td_char *type_name)
{
    struct v4l2_buffer buf;
    ot_size pic_size;
    fd_set fds;
    struct timeval tv;
    errno_t ret;

    pic_size.width = ctx->width;
    pic_size.height = ctx->height;

    while (!g_uvc_exit) {
        FD_ZERO(&fds);
        FD_SET(ctx->fd, &fds);
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        if (select(ctx->fd + 1, &fds, TD_NULL, TD_NULL, &tv) <= 0) {
            usleep(10000);
            continue;
        }

        ret = memset_s(&buf, sizeof(buf), 0, sizeof(buf));
        if (ret != EOK) {
            return TD_FAILURE;
        }

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (uvc_lite_xioctl(ctx->fd, VIDIOC_DQBUF, &buf) < 0) {
            usleep(10000);
            continue;
        }

        if (sample_uvc_media_send_data(ctx->buffers[buf.index].start, buf.bytesused,
            ctx->stride, &pic_size, type_name) != TD_SUCCESS) {
            sample_print("sample_uvc_media_send_data failed\n");
        }

        (td_void)uvc_lite_xioctl(ctx->fd, VIDIOC_QBUF, &buf);
    }

    return TD_SUCCESS;
}

/* 最小入口：只做 HDMI 预览 */
td_s32 sample_uvc_preview_run(const td_char *dev_name, const td_char *type_name,
    td_u32 width, td_u32 height)
{
    uvc_lite_ctx ctx;
    struct sigaction sig_exit = {0};
    td_s32 ret;
    errno_t sret;

    sret = memset_s(&ctx, sizeof(ctx), 0, sizeof(ctx));
    if (sret != EOK) {
        return TD_FAILURE;
    }
    ctx.fd = -1;

    sig_exit.sa_handler = uvc_lite_exit_signal_handler;
    sigaction(SIGINT, &sig_exit, TD_NULL);
    sigaction(SIGTERM, &sig_exit, TD_NULL);

    ret = uvc_lite_open(&ctx, dev_name);
    if (ret != TD_SUCCESS) {
        return ret;
    }

    ret = uvc_lite_set_format(&ctx, type_name, width, height);
    if (ret != TD_SUCCESS) {
        uvc_lite_close(&ctx);
        return ret;
    }

    ret = uvc_lite_reqbufs(&ctx, 4);
    if (ret != TD_SUCCESS) {
        uvc_lite_close(&ctx);
        return ret;
    }

    ret = uvc_lite_queue_all(&ctx);
    if (ret != TD_SUCCESS) {
        uvc_lite_close(&ctx);
        return ret;
    }

    ret = uvc_lite_stream_on(&ctx);
    if (ret != TD_SUCCESS) {
        uvc_lite_close(&ctx);
        return ret;
    }

    ret = sample_uvc_media_init(type_name, ctx.width, ctx.height);
    if (ret != TD_SUCCESS) {
        uvc_lite_stream_off(&ctx);
        uvc_lite_close(&ctx);
        return ret;
    }

    ret = uvc_lite_loop(&ctx, type_name);

    sample_uvc_media_stop_receive_data();
    sample_uvc_media_exit();
    uvc_lite_stream_off(&ctx);
    uvc_lite_close(&ctx);

    return ret;
}

td_s32 sample_uvc_capture_open(sample_uvc_capture_ctx *cap,
    const td_char *dev_name, const td_char *type_name, td_u32 width, td_u32 height)
{
    sample_uvc_media_set_preview_enable(TD_FALSE);
    td_s32 ret;
    errno_t sret;

    if (cap == TD_NULL || dev_name == TD_NULL || type_name == TD_NULL) {
        return TD_FAILURE;
    }

    sret = memset_s(cap, sizeof(*cap), 0, sizeof(*cap));
    if (sret != EOK) {
        return TD_FAILURE;
    }

    cap->uvc.fd = -1;
    sret = strncpy_s(cap->type_name, sizeof(cap->type_name), type_name, sizeof(cap->type_name) - 1);
    if (sret != EOK) {
        return TD_FAILURE;
    }


    ret = uvc_lite_open(&cap->uvc, dev_name);
    if (ret != TD_SUCCESS) {
        return ret;
    }

    ret = uvc_lite_set_format(&cap->uvc, type_name, width, height);
    if (ret != TD_SUCCESS) {
        uvc_lite_close(&cap->uvc);
        return ret;
    }

    ret = uvc_lite_reqbufs(&cap->uvc, 4);
    if (ret != TD_SUCCESS) {
        uvc_lite_close(&cap->uvc);
        return ret;
    }

    ret = uvc_lite_queue_all(&cap->uvc);
    if (ret != TD_SUCCESS) {
        uvc_lite_close(&cap->uvc);
        return ret;
    }

    ret = uvc_lite_stream_on(&cap->uvc);
    if (ret != TD_SUCCESS) {
        uvc_lite_close(&cap->uvc);
        return ret;
    }

    ret = sample_uvc_media_init(type_name, cap->uvc.width, cap->uvc.height);
    if (ret != TD_SUCCESS) {
        uvc_lite_stream_off(&cap->uvc);
        uvc_lite_close(&cap->uvc);
        return ret;
    }

    cap->opened = TD_TRUE;
    return TD_SUCCESS;
}

td_s32 sample_uvc_capture_read_frame(sample_uvc_capture_ctx *cap,
    ot_video_frame_info *frame, td_s32 timeout_ms)
{
    struct v4l2_buffer buf;
    ot_size pic_size;
    fd_set fds;
    struct timeval tv;
    errno_t ret;
    td_s32 sret;

    if (cap == TD_NULL || frame == TD_NULL || cap->opened != TD_TRUE) {
        return TD_FAILURE;
    }

    pic_size.width = cap->uvc.width;
    pic_size.height = cap->uvc.height;

    FD_ZERO(&fds);
    FD_SET(cap->uvc.fd, &fds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (select(cap->uvc.fd + 1, &fds, TD_NULL, TD_NULL, &tv) <= 0) {
        return TD_FAILURE;
    }

    ret = memset_s(&buf, sizeof(buf), 0, sizeof(buf));
    if (ret != EOK) {
        return TD_FAILURE;
    }

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (uvc_lite_xioctl(cap->uvc.fd, VIDIOC_DQBUF, &buf) < 0) {
        return TD_FAILURE;
    }

    sret = sample_uvc_media_send_data(cap->uvc.buffers[buf.index].start, buf.bytesused,
        cap->uvc.stride, &pic_size, cap->type_name);

    (td_void)uvc_lite_xioctl(cap->uvc.fd, VIDIOC_QBUF, &buf);

    if (sret != TD_SUCCESS) {
        return TD_FAILURE;
    }

    return sample_uvc_media_get_frame(frame, timeout_ms);
}

td_s32 sample_uvc_capture_release_frame(const ot_video_frame_info *frame)
{
    return sample_uvc_media_release_frame(frame);
}

td_s32 sample_uvc_capture_close(sample_uvc_capture_ctx *cap)
{
    if (cap == TD_NULL) {
        return TD_FAILURE;
    }

    if (cap->opened == TD_TRUE) {
        sample_uvc_media_stop_receive_data();
        sample_uvc_media_exit();
        uvc_lite_stream_off(&cap->uvc);
        uvc_lite_close(&cap->uvc);
        cap->opened = TD_FALSE;
    }

    return TD_SUCCESS;
}