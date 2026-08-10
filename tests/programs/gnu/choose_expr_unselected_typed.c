// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: 'struct S' has no member named 'nosuch'
/* THE SPRINT FILE WAS WRONG ABOUT THIS ONE. Section 4 of
 * .docs/sprints/12-campaigns/s55-gnu-extensions.md specified the unselected
 * arm of __builtin_choose_expr as "unevaluated AND untype-checked beyond
 * parse". gcc type-checks it: a bad member access, an undeclared identifier
 * and a wrong-arity call are all errors there, measured on all three.
 *
 * Implementing the file as written would ACCEPT CODE GCC REJECTS, and no
 * fixture written in good faith would have caught it -- every natural test
 * puts a valid expression in the unselected arm. The file is corrected in
 * the same commit as this fixture.
 *
 * The arm is still UNEVALUATED; the corpus fixture proves that separately
 * with a call counter. */
struct S {
    int x;
};

int dead_arm(struct S s)
{
    return __builtin_choose_expr(1, 7, s.nosuch);
}
