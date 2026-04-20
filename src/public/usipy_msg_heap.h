#pragma once

#include <stddef.h>

struct usipy_str;
typedef int (*usipy_msg_heap_build_cb)(void *, char *, size_t);

#define USIPY_MSG_HEAP_CHECKPOINT_NONE ((size_t)-1)

struct usipy_msg_heap {
    size_t tsize;
    size_t alen;
    size_t *checkpoints;
    size_t ncheckpoints;
    size_t checkpoint_top;
    void *first;
};

void *usipy_msg_heap_alloc(struct usipy_msg_heap *, size_t);
void usipy_msg_heap_init(struct usipy_msg_heap *, void *, size_t, size_t *, size_t);
size_t usipy_msg_heap_checkpoint(struct usipy_msg_heap *);
void usipy_msg_heap_rollback(struct usipy_msg_heap *, size_t);
int usipy_msg_heap_append(struct usipy_msg_heap *, struct usipy_str *,
  const struct usipy_str *);
int usipy_msg_heap_build(struct usipy_msg_heap *, struct usipy_str *, void *,
  usipy_msg_heap_build_cb);
int usipy_msg_heap_sprintf(struct usipy_msg_heap *, struct usipy_str *,
  const char *, ...) __attribute__ ((format (printf, 3, 4)));

#define USIPY_MEM_ALIGNOF  (3) /* alignof(max_align_t) ? */
#define USIPY_REALIGN(val) ((val) & ~((1 << USIPY_MEM_ALIGNOF) - 1))
#define USIPY_ALIGNED_SIZE(val) ( \
  (val & ~((1 << USIPY_MEM_ALIGNOF) - 1)) + \
  ((val & ((1 << USIPY_MEM_ALIGNOF) - 1)) != 0 ? (1 << USIPY_MEM_ALIGNOF) : 0) \
)

#define usipy_msg_heap_remaining(hp) \
  ((hp)->tsize - (hp)->alen)
