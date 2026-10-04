/* GNU local labels are lexical identities.  Reusing the source spelling in
 * sibling statement expressions or a shadowing block must not collide in the
 * function-wide lowering maps. */

static int cleanup_total;

static void add_cleanup(int *value)
{
    cleanup_total += *value;
}

#define ADJUST(value)                                                          \
    ({                                                                         \
        __label__ done;                                                        \
        int result = (value);                                                  \
        if (result < 0)                                                        \
            goto done;                                                         \
        result += 3;                                                           \
    done:                                                                      \
        result;                                                                \
    })

static int nested(int select)
{
    __label__ target, finish;
    int result = 0;

    {
        __label__ target;
        int cleanup __attribute__((cleanup(add_cleanup))) = 1;

        if (select)
            goto target;
        result++;
    target:
        result += 10;
    }

    if (select > 1)
        goto target;
    goto finish;
target:
    result += 100;
finish:
    return result;
}

int main(void)
{
    if (ADJUST(-1) != -1 || ADJUST(4) != 7)
        return 1;

    cleanup_total = 0;
    if (nested(0) != 11 || cleanup_total != 1)
        return 2;
    cleanup_total = 0;
    if (nested(1) != 10 || cleanup_total != 1)
        return 3;
    cleanup_total = 0;
    if (nested(2) != 110 || cleanup_total != 1)
        return 4;
    return 0;
}
