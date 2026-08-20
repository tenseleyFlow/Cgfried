// Atomic pointer updates scale first, then perform one indivisible RMW.
// Cover both prefix/postfix results, +=/-=, non-power-of-two and array sizes,
// while retaining the established integer-atomic contract.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: func ptr @post_inc_char
// IR_CHECK: imul i64 %0, 1
// IR_CHECK: atomicrmw add i64 @pc
// IR_CHECK: func ptr @pre_dec_short
// IR_CHECK: imul i64 %0, 2
// IR_CHECK: atomicrmw sub i64 @ps
// IR_CHECK: isub i64
// IR_CHECK: func ptr @post_dec_int
// IR_CHECK: imul i64 %0, 4
// IR_CHECK: atomicrmw sub i64 @pi
// IR_CHECK: func ptr @pre_inc_long
// IR_CHECK: imul i64 %0, 8
// IR_CHECK: atomicrmw add i64 @pl
// IR_CHECK: iadd i64
// IR_CHECK: func ptr @add_three
// IR_CHECK: imul i64 %2, 3
// IR_CHECK: atomicrmw add i64 @p3
// IR_CHECK: func ptr @sub_twelve
// IR_CHECK: imul i64 %2, 12
// IR_CHECK: atomicrmw sub i64 @p12
// IR_CHECK: func ptr @add_array
// IR_CHECK: imul i64 %2, 20
// IR_CHECK: atomicrmw add i64 @pa
// IR_CHECK: func void @add_vla
// IR_CHECK: imul i64
// IR_CHECK: imul i64
// IR_CHECK: atomicrmw add i64
// IR_CHECK: func i32 @integer_sibling
// IR_CHECK: atomicrmw add i32 @integer_cell
// IR_CHECK-NOT: load ptr
// IR_CHECK-NOT: store ptr

struct three {
    char bytes[3];
};
struct twelve {
    int words[3];
};

_Atomic(char *) pc;
_Atomic(short *) ps;
_Atomic(int *) pi;
_Atomic(long *) pl;
_Atomic(struct three *) p3;
_Atomic(struct twelve *) p12;
_Atomic(int (*)[5]) pa;
_Atomic int integer_cell;

char *post_inc_char(void)
{
    return pc++;
}
short *pre_dec_short(void)
{
    return --ps;
}
int *post_dec_int(void)
{
    return pi--;
}
long *pre_inc_long(void)
{
    return ++pl;
}
struct three *add_three(long n)
{
    return p3 += n;
}
struct twelve *sub_twelve(long n)
{
    return p12 -= n;
}
int (*add_array(long n))[5]
{
    return pa += n;
}
void add_vla(int n, long k)
{
    _Atomic(int (*)[n]) p;
    p += k;
}
int integer_sibling(int n)
{
    return integer_cell += n;
}
