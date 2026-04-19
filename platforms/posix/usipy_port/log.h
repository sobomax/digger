#define USIPY_LOL_INFO 0
#define USIPY_LOL_ERR  1

#define USIPY_LOGI(tag, fmt, args...) \
  usipy_log_write(USIPY_LOL_INFO, tag, fmt, ## args)
#define USIPY_LOGE(tag, fmt, args...) \
  usipy_log_write(USIPY_LOL_ERR, tag, fmt, ## args)

void usipy_log_write(int, const char *, const char *, ...) \
  __attribute__ ((format (printf, 3, 4)));
