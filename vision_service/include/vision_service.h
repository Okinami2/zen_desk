#ifndef VISION_SERVICE_H
#define VISION_SERVICE_H

#include "ot_type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const td_char *device_path;
    const td_char *pixel_format;
    td_u32 width;
    td_u32 height;
    td_s32 capture_timeout_ms;
} vision_service_config;

td_s32 vision_service_run(const vision_service_config *config);
td_void vision_service_request_stop(td_void);

#ifdef __cplusplus
}
#endif

#endif
