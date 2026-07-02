#ifndef VISION_DISPLAY_H
#define VISION_DISPLAY_H

#include "ot_type.h"

#ifdef __cplusplus
extern "C" {
#endif

td_s32 vision_display_start(const td_char *ready_file);
td_void vision_display_stop(td_void);

#ifdef __cplusplus
}
#endif

#endif
