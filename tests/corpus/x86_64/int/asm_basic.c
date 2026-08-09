// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: marker=4242 counter=2 ok
/* Basic (operand-free) inline asm, EXECUTED rather than inspected.
 *
 * Three claims, and only the first is visible in the assembly:
 *
 *   - a file-scope `asm` really DEFINES a symbol the rest of the file can
 *     reference, so the text reached the assembler intact and in a position
 *     where its `.globl` counted;
 *   - a statement `asm` survives every optimization level. It produces no IR
 *     value, so the only thing keeping one alive is its opcode appearing in
 *     each pass's side-effect list. That is why this runs at all levels AND
 *     why the templates below have an OBSERVABLE effect;
 *   - `volatile` and the implicit volatility of an output-free asm both hold.
 *
 * The template's own layout is preserved: gcc indents only the FIRST line of
 * a template and copies the rest verbatim, so a template that indents itself
 * is not indented twice. Measured with `cat -A` against gcc, because the
 * difference is invisible in ordinary output.
 *
 * OPERANDS ARE A SEPARATE SLICE and are refused by name until the register
 * allocator can honour their constraints -- see tests/programs/gnu/
 * asm_operands_refused.c.
 *
 * THE TEMPLATES ARE PER-TARGET, because an asm template IS target-specific
 * assembly -- that is the whole point of the construct. This file lives in
 * tests/corpus, which the arm64 lane re-runs under qemu, so an x86-only
 * template here fails there with `unknown mnemonic addl`. Selecting on
 * __x86_64__ / __aarch64__ is exactly what portable code with inline asm
 * does, and it makes this fixture cover BOTH backends rather than being
 * excluded from one. */
extern int printf(const char *, ...);

/* The template names its own SECTION, which is what a real file-scope asm
 * does and what this one must do: the text is emitted where the template
 * says, and a template that says nothing leaves data sitting in `.text`.
 * That is gcc's behaviour too, and it is not academic -- ci/check_isa.sh
 * disassembles `.text` against a closed instruction allow-list, and the
 * bytes of `.quad 4242` decode to `adc %al,(%rax)`. The gate caught the
 * first draft of this fixture doing exactly that. */
#if defined(__x86_64__)
#define CGF_BUMP "addl $1, cgf_asm_counter(%rip)"
#elif defined(__aarch64__)
/* No RIP-relative addressing: the address is built with adrp + add, and x9
 * is call-clobbered so an operand-free template may use it freely. */
#define CGF_BUMP                                                               \
    "adrp x9, cgf_asm_counter\n"                                               \
    "\tadd x9, x9, :lo12:cgf_asm_counter\n"                                    \
    "\tldr w10, [x9]\n"                                                        \
    "\tadd w10, w10, #1\n"                                                     \
    "\tstr w10, [x9]"
#else
#error "no inline-asm template for this target"
#endif

__asm__(".section .rodata\n"
        "\t.globl cgf_asm_marker\n"
        "\t.p2align 3\n"
        "cgf_asm_marker:\n"
        "\t.quad 4242\n"
        "\t.text");

extern long cgf_asm_marker;
int cgf_asm_counter = 0;

int main(void)
{
    /* THE TEMPLATE NAMES A GLOBAL DIRECTLY, which is what makes an
     * operand-free asm observable -- and observable is the whole point. A
     * bare `nop` satisfies every check in this file whether it survives or
     * not, so a fixture built on one passes on a compiler that deletes every
     * asm it sees. The first draft of this file was exactly that fixture.
     *
     * `addl $1` rather than `incl` because the BUNDLED assembler has no
     * `incl` yet, and a corpus fixture runs through it. That is the template
     * passing through unexamined working as designed -- the assembler
     * reports on the programmer's text, and the driver no longer calls that
     * a cgf emission bug. */
    __asm__ __volatile__(CGF_BUMP);
    __asm__ __volatile__(CGF_BUMP);
    printf("marker=%ld counter=%d ok\n", cgf_asm_marker, cgf_asm_counter);
    return 0;
}
