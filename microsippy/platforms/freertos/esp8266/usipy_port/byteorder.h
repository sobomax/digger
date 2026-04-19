#include <lwip/def.h>

#ifndef BYTE_ORDER
#error BYTE_ORER is unknown
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
#define USIPY_BIGENDIAN 0
#else
#define USIPY_BIGENDIAN 1
#endif
