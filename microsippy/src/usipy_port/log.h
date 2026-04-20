#pragma once

#include "public/usipy_platform.h"

#define USIPY_LOL_INFO 0
#define USIPY_LOL_ERR  1

#define USIPY_LOGI(tag, fmt, args...) \
  usipy_log_write(USIPY_LOL_INFO, tag, fmt, ## args)
#define USIPY_LOGE(tag, fmt, args...) \
  usipy_log_write(USIPY_LOL_ERR, tag, fmt, ## args)
