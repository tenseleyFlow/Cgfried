// FLAGS: -fsyntax-only
// _Noreturn composes with everything a function specifier may touch, and
// _Noreturn main is legal-but-weird (gcc: silent; matched).
_Noreturn void die(void);
static _Noreturn void die2(void);
_Noreturn int main(void) { for (;;) ; }
