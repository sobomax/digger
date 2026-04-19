#ifndef _USIPY_DEBUG_H
#define _USIPY_DEBUG_H

#if 1
# include <assert.h>

# define USIPY_DABORT() assert(0 != 0)
# define USIPY_DASSERT(x) assert(x)
# define USIPY_DCODE(code) code
#else
# define USIPY_DABORT()
# define USIPY_DASSERT(x)
# define USIPY_DCODE(code)
#endif

#endif /* _USIPY_DEBUG_H */
