#define DEFINE_RAW_METHOD(func, rval, args...) typedef rval (*func##_f)(args)
