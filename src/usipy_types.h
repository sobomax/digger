#define DEFINE_RAW_METHOD(func, rval, args...) typedef rval (*func##_f)(args)
#define GET_MEMBER_OR_NULL(ptr, member) ((ptr) != NULL ? (ptr)->member : NULL)
#define GET_HOST_OR_NULL(addrp) (((addrp) != NULL && (addrp)->host.l != 0) ? \
  &(addrp)->host : NULL)
