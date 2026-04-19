#if !defined(_turbocompare_h)
#define _turbocompare_h

/*
 * markbetween() macro takes int-like type in x treats it as a sequence of bytes
 * and produces a value of the same type and lengh with bytes of original
 * value in the range m > bX < n replaced with 0x80 and the rest with 0x00, i.e.
 *
 * markbetween((uint64_t)0x0102030405060708, 0x03, 0x08) == (uint64_t)0x0000008080808000
 *
 * Obtained from Bit Twiddling Hacks By Sean Eron Anderson <seander@cs.stanford.edu>
 */
#define markbetween(x,m,n) \
   ({const typeof(x) cFF = ~(typeof(x))0, c01 = cFF / 255; (((c01*(127+(n))-((x)&c01*127))&~(x)&(((x)&c01*127)+c01*(127-(m))))&c01*128);})

/*
 * TURBO_LCMASK() generates mask that can be ORed with original int-like
 * value x to produce lower-case version of the sequence of bytes contained
 * in x.
 */
#define TURBO_LCMASK(x) (markbetween(x, 'A' - 1, 'Z' + 1) >> 2)
#define TOLOWER_FUNC(itype) \
    static inline unsigned itype \
    turbo_tolower_##itype(const void *wp) \
    { \
        unsigned itype msk, wrd; \
        memcpy(&wrd, wp, sizeof(wrd)); \
        msk = TURBO_LCMASK(wrd); \
        return (wrd | msk); \
    }

TOLOWER_FUNC(long);
TOLOWER_FUNC(int);
TOLOWER_FUNC(short);
TOLOWER_FUNC(char);

#define FASTCASEBCMP_LOOP(itype) \
    while (len >= sizeof(unsigned itype)) { \
        if (turbo_tolower_##itype(us1.itype##_p) != turbo_tolower_##itype(us2.itype##_p)) \
            return 1; \
        len -= sizeof(unsigned itype); \
        if (len == 0) \
            return 0; \
        if (len < sizeof(unsigned itype)) { \
            us1.char_p -= sizeof(unsigned itype) - len; \
            us2.char_p -= sizeof(unsigned itype) - len; \
            len = sizeof(unsigned itype); \
        } \
        us1.itype##_p++; \
        us2.itype##_p++; \
    }

/*
 * The turbo_casebcmp() function compares ASCII byte strings s1 against s2,
 * ignoring case and returning zero if they are identical, non-zero otherwise.
 * Both strings are assumed to be len bytes long. Zero-length strings are always
 * identical. No special treatment for \0 is performed, the comparison will
 * continue if both strings are matching until len bytes are compared, i.e.
 * turbo_casebcmp("1234\05678", "1234\05679", 9) will return 1 (i.e. mismatch).
 */
static inline int
turbo_casebcmp(const char *s1, const char *s2, unsigned int len)
{
    union {
        const char *char_p;
        const unsigned long *long_p;
        const unsigned int *int_p;
        const unsigned short *short_p;
    } us1, us2;
    us1.char_p = s1;
    us2.char_p = s2;
    FASTCASEBCMP_LOOP(long);
    FASTCASEBCMP_LOOP(int);
    FASTCASEBCMP_LOOP(short);
    FASTCASEBCMP_LOOP(char);
    return 0;
}

/*
 * Convinience macro: return true if both sargs->l is the same as Slen and
 * string S matches sarg->s (ignoring the case in both).
 */
#define turbo_strcasematch(sarg, op, S, Slen) ((sarg)->l op (Slen) && \
  !turbo_casebcmp((sarg)->s.ro, (S), (Slen)))
#define TURBO_STREQ(sarg, S) (turbo_strcasematch((sarg), >=, (S), sizeof(S) - 1))

#endif
