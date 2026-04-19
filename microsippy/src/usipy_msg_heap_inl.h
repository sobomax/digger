#ifndef _USIPY_MSG_HEAP_INL_H
#define _USIPY_MSG_HEAP_INL_H

static inline void
usipy_msg_heap_cnt_rollback(struct usipy_msg_heap *hp, const struct usipy_msg_heap_cnt *cntp)
{

    USIPY_DASSERT(cntp->lastalen == hp->alen);
    USIPY_DASSERT(cntp->alen <= hp->alen);
    hp->alen -= cntp->alen;
    memset(hp->first + hp->alen, '\0', cntp->alen);
}

static inline void *
usipy_msg_heap_alloc_cnt(struct usipy_msg_heap *hp, size_t len,
  struct usipy_msg_heap_cnt *cntp)
{
    void *rp;
    size_t currfree, alen;

    alen = USIPY_ALIGNED_SIZE(len);
    currfree = usipy_msg_heap_remaining(hp);
    if (currfree < len)
       return (NULL);
    rp = hp->first + hp->alen;
    hp->alen += alen;
    cntp->alen = alen;
    USIPY_DCODE(cntp->lastalen = hp->alen);
    return (rp);
}

static inline int
usipy_msg_heap_aextend(struct usipy_msg_heap *hp, size_t nlen,
  struct usipy_msg_heap_cnt *cntp)
{
    size_t currfree, elen, alen;

    USIPY_DASSERT(cntp->lastalen == hp->alen);
    alen = USIPY_ALIGNED_SIZE(nlen);
    USIPY_DASSERT(alen >= cntp->alen);
    elen = alen - cntp->alen;
    if (elen == 0) {
        return (0);
    }
    currfree = usipy_msg_heap_remaining(hp);
    if (currfree < elen) {
        return (-1);
    }
    hp->alen += elen;
    cntp->alen = alen;
    USIPY_DCODE(cntp->lastalen = hp->alen);
    return (0);
}

static inline void
usipy_msg_heap_cnt_reclaim(struct usipy_msg_heap *hp, struct usipy_msg_heap_cnt *cntp,
  size_t nlen)
{
    size_t alen, rlen;

    USIPY_DASSERT(cntp->lastalen == hp->alen);
    alen = USIPY_ALIGNED_SIZE(nlen);
    USIPY_DASSERT(alen <= cntp->alen);
    if (alen == cntp->alen) {
        return;
    }
    rlen = cntp->alen - alen;
    hp->alen -= rlen;
    memset(hp->first + hp->alen, '\0', rlen);
    cntp->alen = alen;
    USIPY_DCODE(cntp->lastalen = hp->alen);
}

#endif /* _USIPY_MSG_HEAP_INL_H */
