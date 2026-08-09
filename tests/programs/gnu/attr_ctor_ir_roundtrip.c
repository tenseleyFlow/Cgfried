// FLAGS: -emit-ir
// IR_CHECK: global @g size 4 align 4 internal section(".mydata")
// IR_CHECK: func void @c_def() internal constructor {
// IR_CHECK: func void @c_101() internal constructor(101) {
// IR_CHECK: func void @both() internal constructor(103) destructor(103) {
// IR_CHECK: func void @in_section() internal section(".mytext") {
/* The IR round trip, which every `-emit-ir` fixture also proves: the driver
 * re-parses what it printed and compares structurally, so a marker the printer
 * emits and the parser cannot read is an ICE.
 *
 * THIS FIXTURE EXISTS BECAUSE `section` WAS EXACTLY THAT. It was printed on
 * both functions and globals and parsed on neither, so `cgf -emit-ir` on any
 * program using the attribute died with
 *
 *   <reprint>:3:35: error: expected 'sym', 'alias', 'global', or 'func'
 *   internal compiler error: -emit-ir round-trip broke
 *
 * No fixture caught it because every `section` test is an ASM_CHECK, and
 * ordinary -S and -c never go near the IR text. The two attributes are pinned
 * together here so the next symbol property added has one place to join.
 *
 * The section names are QUOTED in the IR text. A section name is an arbitrary
 * byte string -- `.note.GNU-stack` is entirely ordinary and does not lex as an
 * identifier in that grammar -- so printing it bare could not have round-tripped
 * even once the parser existed.
 *
 * The priority is printed only when it is not the default, which is not
 * cosmetic: gcc emits the same unnumbered section for a bare `constructor` and
 * for `constructor(65535)`, so the two ARE the same request and must not print
 * differently. `c_max` below pins that. */
__attribute__((constructor)) static void c_def(void)
{
}

__attribute__((constructor(65535))) static void c_max(void)
{
}

__attribute__((constructor(101))) static void c_101(void)
{
}

__attribute__((constructor(103), destructor(103))) static void both(void)
{
}

__attribute__((section(".mytext"))) static void in_section(void)
{
}

__attribute__((section(".mydata"))) static int g = 1;

int main(void)
{
    in_section();
    return g;
}
