struct usipy_fast_parser {
    uint32_t magic;
    char toid[256];
};

int usipy_fp_classify(const struct usipy_fast_parser *, const struct usipy_str *);
