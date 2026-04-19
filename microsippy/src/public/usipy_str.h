#pragma once

struct usipy_str {
    union {
        const char *ro;
        const unsigned char *_uro;
        char *rw;
    } s;
    size_t l;
};

struct usipy_str_ro {
    union {
        const char *ro;
        const unsigned char *_uro;
    } s;
    size_t l;
};

int usipy_str_split(const struct usipy_str *, unsigned char,
  struct usipy_str *, struct usipy_str *);
int usipy_str_split3(const struct usipy_str *, unsigned char,
  struct usipy_str *, struct usipy_str *, struct usipy_str *);
int usipy_str_atoui_range(const struct usipy_str *, unsigned int *,
  unsigned int, unsigned int);
int usipy_str_splitlws(const struct usipy_str *, struct usipy_str *,
  struct usipy_str *);
int usipy_str_split_elem(struct usipy_str *, unsigned char,
  struct usipy_str *);
int usipy_str_split_elem_nlws(struct usipy_str *, unsigned char,
  struct usipy_str *);
int usipy_str_eq(const struct usipy_str *, const struct usipy_str *);

#define USIPY_SP       ' '
#define USIPY_CRLF     "\r\n"
#define USIPY_CRLF_LEN 2
#define USIPY_ISWS(ch) ((ch) == USIPY_SP || (ch) == '\t')
#define USIPY_ISLWS(ch) (USIPY_ISWS(ch) || (ch) == '\r' || (ch) == '\n')

#define USIPY_STR_NULL (struct usipy_str){.l = 0, .s.ro = NULL}
#define USIPY_2STR(cstring) \
    {.l = (sizeof(cstring) - 1), .s.ro = (cstring)}
#define USIPY_B2STR(barray) \
    {.l = (sizeof(barray)), .s._uro = (barray)}

#define usipy_str_trm_e(sp) \
    while ((sp)->l > 0 && USIPY_ISWS((sp)->s.ro[(sp)->l - 1])) { \
        (sp)->l -= 1; \
    }

#define usipy_str_trm_b(sp) \
    while ((sp)->l > 0 && USIPY_ISWS((sp)->s.ro[0])) { \
        (sp)->s.ro += 1; \
        (sp)->l -= 1; \
    }

#define usipy_str_ltrm_e(sp) \
    while ((sp)->l > 0 && USIPY_ISLWS((sp)->s.ro[(sp)->l - 1])) { \
        (sp)->l -= 1; \
    }

#define usipy_str_ltrm_b(sp) \
    while ((sp)->l > 0 && USIPY_ISLWS((sp)->s.ro[0])) { \
        (sp)->s.ro += 1; \
        (sp)->l -= 1; \
    }

#define USIPY_SFMT(sp) (unsigned int)(sp)->l, (sp)->s.ro

#define DUMP_STR(vp, sname, canname) \
    USIPY_LOGI(log_tag, "%s%s." #sname " = \"%.*s\"", log_pref, canname, \
      USIPY_SFMT(vp->sname))
#define DUMP_PARAM(vp, sname, idx, canname) \
    USIPY_LOGI(log_tag, "%s%s." #sname "[%d] = \"%.*s\"=\"%.*s\"", log_pref, \
      canname, idx, USIPY_SFMT(&vp->sname[i].token), USIPY_SFMT(&vp->sname[i].value))
#define DUMP_UINT(vp, sname, canname) \
    USIPY_LOGI(log_tag, "%s%s." #sname " = %u", log_pref, canname, vp->sname)
