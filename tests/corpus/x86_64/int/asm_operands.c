// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 4 102 77 9 10 6 11 7
/* Extended asm with OPERANDS, executed. Every expectation was taken from
 * gcc's output, not predicted.
 *
 * The rows, and what each one is the only cover for:
 *
 *   r_only   an ordinary `=r` output and `r` input
 *   imm      `i`, an assemble-time constant. IrAsmOp.imm was DECLARED and
 *            never populated in the first draft, so this emitted `$0` and
 *            read 2 where 102 was right -- a field that exists and is never
 *            written looks exactly like a field that works.
 *   memop    `m`, whose operand is an ADDRESS and prints as `(%reg)`
 *   rw       `+r`, read-write. Its load after the asm was forwarded from the
 *            store before it at -O2 (correct at -O0) until IR_ASM joined
 *            GVN's barrier list: the asm writes THROUGH its output address
 *            and no analysis here can see inside the template.
 *   named    `%[sym]` symbolic operands
 *   pct      `%%` and a named register clobber
 *   amp      `=&r`, early clobber -- see the note below
 *   two_in   two inputs, which is where an off-by-one in the operand walk
 *            shows up
 *
 * EARLY CLOBBER IS IMPLIED HERE, and the reason is narrower than an earlier
 * draft of this comment claimed. cg_intervals_build extends a def and a use
 * at one instruction point to that same point, and intervals are hole-free
 * and inclusive, so an asm output cannot share a register with an input --
 * BUT ONLY WHILE BOTH ARE IN REGISTERS. That claim was written as a property
 * of the allocator and it was FALSE in the spill path, where every operand
 * was reloaded through the same small scratch set: three `r` operands all
 * became %r11 and the template read one value three times. It is true now
 * because CgInterval.no_spill makes an asm operand's interval unspillable,
 * so the spill path has no asm operands to collide. Never sharing is always
 * safe -- gcc merely shares when it may -- so `amp` below agrees with gcc
 * for the right reason, and dropping the `&` would not change our output
 * the way it changes gcc's (gcc returns 2 instead of 11).
 *
 * THIS RUNS UNDER CGF_SPILL_ALL=1 IN BOTH arm64 LANES, which is the point of
 * its living in tests/corpus: the collision above appeared at no other
 * pressure, on either target, and an ordinary run cannot see it.
 *
 * The templates stay inside what the BUNDLED assembler encodes -- `addl $1`
 * rather than `incl` -- because a template it cannot assemble is reported
 * against the programmer's text rather than ours. */
extern int printf(const char *, ...);

#if defined(__x86_64__)
static int r_only(int a)
{
    int o;

    __asm__("movl %1, %0\n\taddl $3, %0" : "=r"(o) : "r"(a));
    return o;
}
static int imm(int a)
{
    int o;

    __asm__("movl %1, %0\n\taddl %2, %0" : "=r"(o) : "r"(a), "i"(100));
    return o;
}
static int memop(int *p)
{
    int o;

    __asm__("movl %1, %0" : "=r"(o) : "m"(*p));
    return o;
}
static int rw(int n)
{
    __asm__("addl $5, %0" : "+r"(n));
    return n;
}
static int named(int a)
{
    int o;

    __asm__("movl %[in], %[out]\n\taddl $1, %[out]"
            : [out] "=r"(o)
            : [in] "r"(a));
    return o;
}
static int pct(int a)
{
    int o;

    __asm__("movl %1, %0\n\tandl $255, %%eax\n\torl $0, %0"
            : "=r"(o)
            : "r"(a)
            : "rax");
    return o;
}
static int amp(int a)
{
    int o;

    __asm__("movl $1, %0\n\taddl %1, %0" : "=&r"(o) : "r"(a));
    return o;
}
static int two_in(int a, int b)
{
    int o;

    __asm__("movl %1, %0\n\taddl %2, %0" : "=&r"(o) : "r"(a), "r"(b));
    return o;
}
#elif defined(__aarch64__)
static int r_only(int a)
{
    int o;

    __asm__("add %w0, %w1, #3" : "=r"(o) : "r"(a));
    return o;
}
static int imm(int a)
{
    int o;

    __asm__("mov %w0, %w1\n\tadd %w0, %w0, %2" : "=r"(o) : "r"(a), "i"(100));
    return o;
}
static int memop(int *p)
{
    int o;

    __asm__("ldr %w0, %1" : "=r"(o) : "m"(*p));
    return o;
}
static int rw(int n)
{
    __asm__("add %w0, %w0, #5" : "+r"(n));
    return n;
}
static int named(int a)
{
    int o;

    __asm__("add %w[out], %w[in], #1" : [out] "=r"(o) : [in] "r"(a));
    return o;
}
static int pct(int a)
{
    int o;

    __asm__("mov %w0, %w1\n\tmov x9, #0\n\torr %w0, %w0, w9"
            : "=r"(o)
            : "r"(a)
            : "x9");
    return o;
}
static int amp(int a)
{
    int o;

    __asm__("mov %w0, #1\n\tadd %w0, %w0, %w1" : "=&r"(o) : "r"(a));
    return o;
}
static int two_in(int a, int b)
{
    int o;

    __asm__("add %w0, %w1, %w2" : "=&r"(o) : "r"(a), "r"(b));
    return o;
}
#else
#error "no inline-asm templates for this target"
#endif

int main(void)
{
    int x = 77;

    printf("%d %d %d %d %d %d %d %d\n", r_only(1), imm(2), memop(&x), rw(4),
           named(9), pct(6), amp(10), two_in(3, 4));
    return 0;
}
