// RESOLVED(audit): IR-H-12 SysV spilled aggregate parameters lose their stack ABI marker
// Exhausting the fixed-aggregate register budget moves the later values to the
// stack. Caller operands and callee parameters must describe that same ABI
// placement or valid C reaches verifier check 9 and becomes an ICE.
struct s8 {
    char x[8];
};
struct s9 {
    char x[9];
};
struct s10 {
    char x[10];
};
struct s11 {
    char x[11];
};
struct s12 {
    char x[12];
};
struct s13 {
    char x[13];
};

static struct s8 a;
static struct s9 b;
static struct s10 c;
static struct s11 d;
static struct s12 e;
static struct s13 f;

static void sink(struct s8 aa, struct s9 bb, struct s10 cc, struct s11 dd,
                 struct s12 ee, struct s13 ff)
{
    (void)aa;
    (void)bb;
    (void)cc;
    (void)dd;
    (void)ee;
    (void)ff;
}

void exercise(void)
{
    sink(a, b, c, d, e, f);
}
