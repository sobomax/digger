#include <stdarg.h>
#include <stdint.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "usipy_debug.h"
#include "public/usipy_msg_heap.h"
#include "public/usipy_str.h"
#include "usipy_msg_heap_rb.h"

static void *
usipy_msg_heap_reserve(struct usipy_msg_heap *hp, size_t len, int align_start)
{
    size_t off, total;

    USIPY_DASSERT(hp != NULL);

    off = hp->alen;
    if (align_start) {
        off = USIPY_ALIGNED_SIZE(off);
    }
    total = (off - hp->alen) + len;
    if (usipy_msg_heap_remaining(hp) < total)
        return (NULL);
    hp->alen = off + len;
    return ((char *)hp->first + off);
}

void *
usipy_msg_heap_alloc(struct usipy_msg_heap *hp, size_t len)
{
    return (usipy_msg_heap_reserve(hp, USIPY_ALIGNED_SIZE(len), 1));
}

void
usipy_msg_heap_init(struct usipy_msg_heap *hp, void *bp, size_t len, size_t *checkpoints,
  size_t ncheckpoints)
{
    uintptr_t ralgn;

    hp->alen = 0;
    hp->first = bp;
    hp->checkpoints = checkpoints;
    hp->ncheckpoints = ncheckpoints;
    hp->checkpoint_top = 0;
    ralgn = USIPY_REALIGN((uintptr_t)hp->first);
    if ((void *)ralgn != hp->first) {
        hp->first = (void *)(ralgn + (1 << USIPY_MEM_ALIGNOF));
    }
    hp->tsize = USIPY_REALIGN(len - (hp->first - bp));
}

size_t
usipy_msg_heap_checkpoint(struct usipy_msg_heap *hp)
{
    size_t cpi;

    USIPY_DASSERT(hp != NULL);
    USIPY_DASSERT(hp->checkpoint_top < hp->ncheckpoints);
    USIPY_DASSERT(hp->checkpoints != NULL);
    cpi = hp->checkpoint_top;
    hp->checkpoints[cpi] = hp->alen;
    hp->checkpoint_top += 1;
    return (cpi);
}

void
usipy_msg_heap_rollback(struct usipy_msg_heap *hp, size_t checkpoint_index)
{
    size_t alen;

    USIPY_DASSERT(hp != NULL);
    USIPY_DASSERT(hp->checkpoint_top > 0);
    USIPY_DASSERT(hp->checkpoints != NULL);
    USIPY_DASSERT(checkpoint_index != USIPY_MSG_HEAP_CHECKPOINT_NONE);
    USIPY_DASSERT(checkpoint_index == hp->checkpoint_top - 1);
    alen = hp->checkpoints[checkpoint_index];
    USIPY_DASSERT(alen <= hp->alen);
    memset((char *)hp->first + alen, '\0', hp->alen - alen);
    hp->alen = alen;
}

int
usipy_msg_heap_append(struct usipy_msg_heap *hp, struct usipy_str *dstp,
  const struct usipy_str *srcp)
{
    char *bp;

    USIPY_DASSERT(hp != NULL);
    USIPY_DASSERT(dstp != NULL);
    USIPY_DASSERT(srcp != NULL);

    if (srcp->l == 0) {
        *dstp = USIPY_STR_NULL;
        return (0);
    }
    bp = usipy_msg_heap_reserve(hp, srcp->l, 0);
    if (bp == NULL) {
        return (-1);
    }
    memcpy(bp, srcp->s.ro, srcp->l);
    dstp->s.ro = bp;
    dstp->l = srcp->l;
    return (0);
}

int
usipy_msg_heap_build(struct usipy_msg_heap *hp, struct usipy_str *sp, void *arg,
  usipy_msg_heap_build_cb cb)
{
    const size_t off = USIPY_ALIGNED_SIZE(hp->alen);
    const size_t currfree = hp->tsize - off;
    char *bp;
    size_t alen;
    int blen;

    USIPY_DASSERT(hp != NULL);
    USIPY_DASSERT(sp != NULL);
    USIPY_DASSERT(cb != NULL);

    if (currfree == 0) {
        return (-1);
    }
    bp = (char *)hp->first + off;
    blen = cb(arg, bp, currfree);
    if (blen < 0) {
        return (-1);
    }
    alen = USIPY_ALIGNED_SIZE((size_t)blen);
    bp = usipy_msg_heap_reserve(hp, alen, 1);
    if (bp == NULL) {
        return (-1);
    }
    sp->s.ro = bp;
    sp->l = (size_t)blen;
    return (0);
}

static int
usipy_msg_heap_vsprintf(struct usipy_msg_heap *hp, struct usipy_str *sp,
  const char *fmt, va_list ap) __attribute__ ((format (printf, 3, 0)));

static int
usipy_msg_heap_vsprintf(struct usipy_msg_heap *hp, struct usipy_str *sp,
  const char *fmt, va_list ap)
{
    const size_t currfree = usipy_msg_heap_remaining(hp);
    const char *bp;
    int plen;

    USIPY_DASSERT(hp != NULL);
    USIPY_DASSERT(sp != NULL);
    USIPY_DASSERT(fmt != NULL);

    if (currfree == 0) {
        return (-1);
    }
    bp = (char *)hp->first + hp->alen;
    plen = vsnprintf((char *)bp, currfree, fmt, ap);
    if (plen < 0 || (size_t)plen >= currfree) {
        return (-1);
    }
    bp = usipy_msg_heap_reserve(hp, (size_t)plen + 1, 0);
    if (bp == NULL) {
        return (-1);
    }
    sp->s.ro = bp;
    sp->l = (size_t)plen;
    return (0);
}

int
usipy_msg_heap_sprintf(struct usipy_msg_heap *hp, struct usipy_str *sp,
  const char *fmt, ...)
{
    va_list ap;
    int rval;

    va_start(ap, fmt);
    rval = usipy_msg_heap_vsprintf(hp, sp, fmt, ap);
    va_end(ap);
    return (rval);
}
