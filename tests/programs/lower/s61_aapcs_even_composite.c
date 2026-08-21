// IR-C-09: Linux AAPCS64 rule C.10 skips odd NGRN before a naturally
// 16-byte-aligned composite. The `even` boundary survives IR so caller and
// callee backends place the two flattened leaves in x2:x3, not x1:x2.
// FLAGS: --target=arm64-linux -emit-ir
// IR_CHECK: func i64 @sink(i64 %0, i64 even %1, i64 %2)
// IR_CHECK: call i64 @sink(i64 %
// IR_CHECK: iadd i32 %
// IR_CHECK: and i32 %
typedef __builtin_va_list va_list;

struct Pair16 {
    _Alignas(16) long first;
    long second;
};

long sink(long tag, struct Pair16 value)
{
    return tag + value.first + value.second;
}

long call(struct Pair16 value) { return sink(7, value); }

long variadic(long tag, ...)
{
    va_list ap;
    struct Pair16 value;

    __builtin_va_start(ap, tag);
    value = __builtin_va_arg(ap, struct Pair16);
    __builtin_va_end(ap);
    return value.first + value.second;
}
