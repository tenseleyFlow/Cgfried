// FLAGS: -O2 -emit-ir
// An anonymous call argument's provenance belongs to the SLOT, not to the
// value in it. Every pass that replaces a call operand must re-attach it:
// mem2reg forwards the parameter over the promoted alloca here, and sccp
// folds the literal. Apple's arm64 ABI is the consumer -- an argument that
// loses `anon` there is passed in a register the callee never reads.
//
// gvn, cse and simplify rewrite the same operands; all five now share
// ir_arg_carry_provenance, which is why one fixture covers the class.
int printf(const char *, ...);

// IR_CHECK: call i32 @printf(ptr @.Lstr.0, i32 %0 anon, i32 7 anon)
int report(int n)
{
    int local = n;

    return printf("%d %d\n", local, 3 + 4);
}
