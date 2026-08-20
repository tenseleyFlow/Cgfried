// RESOLVED(audit): SEMA-C-08 AAPCS64 unnamed nonzero bitfields fail to align aggregates
struct anon_long_only {
    long : 16;
    char value;
};

struct mixed_anon {
    unsigned int : 28;
    long : 16;
    char tail : 2;
    int value;
};

union anon_long_union {
    unsigned long : 7;
    char value;
};
