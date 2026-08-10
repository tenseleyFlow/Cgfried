typedef int alias_int __attribute__((may_alias));

struct __attribute__((__may_alias__)) AliasRecord {
    int value;
};

int through_alias_typedef(float *fp, alias_int *ip)
{
    *fp = 1.0f;
    *ip = 0;
    return *fp == 0.0f;
}

int through_alias_record(float *fp, struct AliasRecord *rp)
{
    *fp = 1.0f;
    rp->value = 0;
    return *fp == 0.0f;
}
