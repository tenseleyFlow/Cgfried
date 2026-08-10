// FLAGS: -std=gnu17 tests/fixtures/gnu/may_alias_impl.c
// OPT_EQ: all
// EXIT_CODE: 0
// The implementation lives in a separate translation unit so IPO cannot
// inline the calls and recover the shared address from provenance alone. At
// optimized levels, correctness therefore depends on may_alias suppressing
// the ordinary int-vs-float TBAA proof inside each callee.
struct AliasRecord {
    int value;
};

int through_alias_typedef(float *fp, int *ip);
int through_alias_record(float *fp, struct AliasRecord *rp);

int main(void)
{
    union {
        float f;
        int i;
    } scalar;
    union {
        float f;
        struct AliasRecord record;
    } aggregate;

    if (!through_alias_typedef(&scalar.f, &scalar.i))
        return 1;
    if (!through_alias_record(&aggregate.f, &aggregate.record))
        return 2;
    return 0;
}
