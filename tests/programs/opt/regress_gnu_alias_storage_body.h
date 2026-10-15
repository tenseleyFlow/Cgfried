/* Included by the strict and -fno-strict-aliasing OPT_EQ matrices. These are
 * storage-identity cases, so their answers cannot depend on TBAA policy. */
typedef struct AliasRecord {
    int head;
    int values[3];
    int tail;
} AliasRecord;

AliasRecord gvn_forward_target = {1, {7, 8, 9}, 10};
extern AliasRecord gvn_forward_alias
    __attribute__((alias("gvn_forward_target")));

AliasRecord gvn_reverse_target = {2, {12, 13, 14}, 15};
extern AliasRecord gvn_reverse_alias
    __attribute__((alias("gvn_reverse_target")));

AliasRecord dse_forward_target = {3, {17, 18, 19}, 20};
extern AliasRecord dse_forward_alias
    __attribute__((alias("dse_forward_target")));

AliasRecord dse_reverse_target = {4, {22, 23, 24}, 25};
extern AliasRecord dse_reverse_alias
    __attribute__((alias("dse_reverse_target")));

static AliasRecord static_target = {5, {27, 28, 29}, 30};
static AliasRecord static_alias __attribute__((alias("static_target")));

AliasRecord weak_target = {6, {32, 33, 34}, 35};
extern AliasRecord weak_alias __attribute__((weak, alias("weak_target")));

static int check_gvn_alias_to_target(void)
{
    int before = gvn_forward_alias.values[1];

    gvn_forward_target.values[1] = 41;
    return gvn_forward_alias.values[1] - before != 33;
}

static int check_gvn_target_to_alias(void)
{
    int before = gvn_reverse_target.tail;

    gvn_reverse_alias.tail = 47;
    return gvn_reverse_target.tail - before != 32;
}

static int check_dse_alias_read(void)
{
    int observed;

    dse_forward_target.head = 51;
    observed = dse_forward_alias.head;
    dse_forward_target.head = 52;
    return observed != 51;
}

static int check_dse_target_read(void)
{
    int observed;

    dse_reverse_alias.values[2] = 57;
    observed = dse_reverse_target.values[2];
    dse_reverse_alias.values[2] = 58;
    return observed != 57;
}

static int check_static_alias(void)
{
    int before = static_alias.values[0];

    static_target.values[0] = 61;
    return static_alias.values[0] - before != 34;
}

static int check_weak_alias(void)
{
    int observed;

    weak_alias.tail = 67;
    observed = weak_target.tail;
    weak_alias.tail = 68;
    return observed != 67;
}

int main(void)
{
    if (check_gvn_alias_to_target())
        return 1;
    if (check_gvn_target_to_alias())
        return 2;
    if (check_dse_alias_read())
        return 3;
    if (check_dse_target_read())
        return 4;
    if (check_static_alias())
        return 5;
    if (check_weak_alias())
        return 6;
    return 0;
}
