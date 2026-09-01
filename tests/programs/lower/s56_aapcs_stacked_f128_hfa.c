// AAPCS64 gives an HFA at most four FP-register leaves, but once the bank is
// exhausted the same value travels as ordinary bytes on the stack. A Linux
// long double is binary128, so the widest four-leaf HFA occupies 64 bytes and
// needs eight stack eightbytes. The planner used to share the four-register
// cap with this flattened stack form and ICE before producing IR.
// FLAGS: --target=arm64-linux -emit-ir
// IR_CHECK: func f128 @stacked_hfa(f128 %0
// IR_CHECK: f64 onstack stackalign16 %8
// IR_CHECK: call f128 @stacked_hfa(f128 %
struct H4 {
    long double value[4];
};

long double stacked_hfa(struct H4 first, struct H4 second, struct H4 third)
{
    return third.value[3];
}

long double call_stacked_hfa(struct H4 first, struct H4 second, struct H4 third)
{
    return stacked_hfa(first, second, third);
}
