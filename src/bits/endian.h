#if USIPY_BIGENDIAN

#  if !defined(__FreeBSD__)
#    define _BSD_SOURCE             /* See feature_test_macros(7) */
#    include <endian.h>
#  else
#    include <sys/endian.h>
#  endif

//#  warning Platform is big-endian.
#  define HTOLE(sp) (sizeof(sp) == 8 ? htole64(sp) : htole32(sp))
#else
//#  warning Platform is little-endian.
#  define HTOLE(sp) (sp)/* Nop */
#endif
