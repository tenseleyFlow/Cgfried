/* Offline Sprint 60 F03 oracle generator.
 *
 * Build manually with:
 *   cc -std=c17 -O2 $(pkg-config --cflags mpfr) -o /tmp/f03-mpfr \
 *     tests/tools/f03_mpfr_constexpr_oracle.c $(pkg-config --libs mpfr)
 *
 * MPFR is deliberately a generator-only dependency.  The emitted fixture
 * contains fixed IEEE-754 byte images and has no non-project dependency.
 */
#include <mpfr.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum { C_FLOAT, C_DOUBLE } CType;
typedef enum { K_LITERAL, K_ADD, K_SUB, K_MUL, K_DIV, K_D2F } Kind;

typedef struct {
    const char *name;
    CType type;
    Kind kind;
    const char *a;
    const char *b;
    const char *expr;
} Case;

#define F_LIT(name, value) {name, C_FLOAT, K_LITERAL, value, NULL, value "f"}
#define D_LIT(name, value) {name, C_DOUBLE, K_LITERAL, value, NULL, value}
#define F_OP(name, kind, a, op, b)                                             \
    {name, C_FLOAT, kind, a, b, "(" a "f " op " " b "f)"}
#define D_OP(name, kind, a, op, b)                                             \
    {name, C_DOUBLE, kind, a, b, "(" a " " op " " b ")"}
#define D2F(name, value)                                                       \
    {name, C_FLOAT, K_D2F, value, NULL, "(float)(" value ")"}

static const Case cases[] = {
    F_LIT("f_point1", "0.1"),
    F_LIT("f_half_even_down", "1.000000059604644775390625"),
    F_LIT("f_half_even_up", "1.000000178813934326171875"),
    F_LIT("f_above_half", "1.0000000596046447753906250000000000001"),
    F_LIT("f_below_half", "1.0000000596046447753906249999999999999"),
    F_LIT("f_min_normal", "1.17549435082228750796873653722224567782e-38"),
    F_LIT("f_max_subnormal", "1.17549421069244107548702944484928734883e-38"),
    F_LIT("f_min_subnormal", "1.40129846432481707092372958328991613128e-45"),
    F_LIT("f_half_min_subnormal",
          "7.00649232162408535461864791644958065640e-46"),
    F_LIT("f_max_finite", "3.40282346638528859811704183484516925440e38"),

    D_LIT("d_point1", "0.1"),
    D_LIT("d_two53_plus_one", "9007199254740993.0"),
    D_LIT("d_half_even_down",
          "1.00000000000000011102230246251565404236316680908203125"),
    D_LIT("d_half_even_up",
          "1.00000000000000033306690738754696212708950042724609375"),
    D_LIT("d_above_half",
          "1.0000000000000001110223024625156540423631668090820312500000001"),
    D_LIT("d_below_half",
          "1.0000000000000001110223024625156540423631668090820312499999999"),
    D_LIT("d_min_normal",
          "2.225073858507201383090232717332404064219215980462331830553327417e-"
          "308"),
    D_LIT("d_max_subnormal",
          "2.225073858507200889024586876085859887650423112240959465493524802e-"
          "308"),
    D_LIT("d_min_subnormal",
          "4.940656458412465441765687928682213723650598026143247644255856825e-"
          "324"),
    D_LIT("d_half_min_subnormal",
          "2.470328229206232720882843964341106861825299013071623822127928412e-"
          "324"),
    D_LIT("d_max_finite", "1.797693134862315708145274237317043567981e308"),

    F_OP("f_add_point1_point2", K_ADD, "0.1", "+", "0.2"),
    F_OP("f_add_half_to_odd", K_ADD, "1.00000011920928955078125", "+",
         "5.9604644775390625e-8"),
    F_OP("f_sub_adjacent", K_SUB, "1.0", "-", "0.999999940395355224609375"),
    F_OP("f_mul_adjacent", K_MUL, "1.00000011920928955078125", "*",
         "1.00000011920928955078125"),
    F_OP("f_div_three", K_DIV, "1.0", "/", "3.0"),
    F_OP("f_div_max", K_DIV, "3.40282346638528859811704183484516925440e38", "/",
         "2.0"),
    F_OP("f_div_min_normal", K_DIV,
         "1.17549435082228750796873653722224567782e-38", "/", "2.0"),
    F_OP("f_add_min_subnormals", K_ADD,
         "1.40129846432481707092372958328991613128e-45", "+",
         "1.40129846432481707092372958328991613128e-45"),

    D_OP("d_add_point1_point2", K_ADD, "0.1", "+", "0.2"),
    D_OP("d_add_half_to_odd", K_ADD,
         "1.0000000000000002220446049250313080847263336181640625", "+",
         "1.1102230246251565404236316680908203125e-16"),
    D_OP("d_sub_adjacent", K_SUB, "1.0", "-",
         "0.99999999999999988897769753748434595763683319091796875"),
    D_OP("d_mul_adjacent", K_MUL,
         "1.0000000000000002220446049250313080847263336181640625", "*",
         "1.0000000000000002220446049250313080847263336181640625"),
    D_OP("d_div_three", K_DIV, "1.0", "/", "3.0"),
    D_OP("d_div_max", K_DIV, "1.797693134862315708145274237317043567981e308",
         "/", "2.0"),
    D_OP("d_div_min_normal", K_DIV,
         "2.225073858507201383090232717332404064219215980462331830553327417e-"
         "308",
         "/", "2.0"),
    D_OP("d_add_min_subnormals", K_ADD,
         "4.940656458412465441765687928682213723650598026143247644255856825e-"
         "324",
         "+",
         "4.940656458412465441765687928682213723650598026143247644255856825e-"
         "324"),

    D2F("cast_double_half_down",
        "1.0000000596046447753906250000000000000000000000000000000000000001"),
    D2F("cast_double_half_up",
        "1.0000001788139343261718749999999999999999999999999999999999999999"),
    D2F("cast_double_min_normal_boundary",
        "1.17549428075736429172788299103576651332e-38"),
    D2F("cast_double_half_min_subnormal",
        "7.00649232162408535461864791644958065640e-46"),
};

static void set_decimal(mpfr_t out, const char *s)
{
    mpfr_t wide;

    mpfr_init2(wide, 2048);
    if (mpfr_set_str(wide, s, 10, MPFR_RNDN) != 0) {
        fprintf(stderr, "invalid oracle input: %s\n", s);
        mpfr_clear(wide);
        return;
    }
    mpfr_set(out, wide, MPFR_RNDN);
    mpfr_clear(wide);
}

static uint64_t evaluate(const Case *c)
{
    mpfr_prec_t precision = c->type == C_FLOAT ? 24 : 53;
    mpfr_t a, b, out;
    uint64_t bits = 0;

    if (c->kind == K_D2F)
        precision = 53;
    mpfr_init2(a, precision);
    mpfr_init2(b, precision);
    mpfr_init2(out, precision);
    set_decimal(a, c->a);
    if (c->b)
        set_decimal(b, c->b);

    switch (c->kind) {
    case K_LITERAL:
        mpfr_set(out, a, MPFR_RNDN);
        break;
    case K_ADD:
        mpfr_add(out, a, b, MPFR_RNDN);
        break;
    case K_SUB:
        mpfr_sub(out, a, b, MPFR_RNDN);
        break;
    case K_MUL:
        mpfr_mul(out, a, b, MPFR_RNDN);
        break;
    case K_DIV:
        mpfr_div(out, a, b, MPFR_RNDN);
        break;
    case K_D2F: {
        double d = mpfr_get_d(a, MPFR_RNDN);
        mpfr_set_d(out, d, MPFR_RNDN);
        break;
    }
    }

    if (c->type == C_FLOAT) {
        float f = mpfr_get_flt(out, MPFR_RNDN);
        uint32_t u;

        memcpy(&u, &f, sizeof(u));
        bits = u;
    } else {
        double d = mpfr_get_d(out, MPFR_RNDN);

        memcpy(&bits, &d, sizeof(bits));
    }
    mpfr_clears(a, b, out, (mpfr_ptr)0);
    return bits;
}

static void print_le(uint64_t bits, unsigned bytes)
{
    unsigned i;

    for (i = 0; i < bytes; i++)
        printf("%02X", (unsigned)((bits >> (i * 8)) & 0xff));
}

int main(void)
{
    uint64_t results[sizeof(cases) / sizeof(cases[0])];
    size_t i;

    puts("// Generated offline by tests/tools/f03_mpfr_constexpr_oracle.c");
    puts("// MPFR_RNDN, binary24/binary53; MPFR is not a test dependency.");
    puts("// FLAGS: -fdump-init");
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        unsigned bytes = cases[i].type == C_FLOAT ? 4 : 8;

        results[i] = evaluate(&cases[i]);
        printf("// CHECK: %s: size=%u bytes=", cases[i].name, bytes);
        print_le(results[i], bytes);
        putchar('\n');
    }
    putchar('\n');
    puts("// clang-format off");
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        printf("%s %s = %s;\n", cases[i].type == C_FLOAT ? "float" : "double",
               cases[i].name, cases[i].expr);
    puts("// clang-format on");
    return 0;
}
