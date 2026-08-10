// ERROR_EXPECTED: mode 'TI' names a 128-bit integer type
// ERROR_EXPECTED: mode 'DF' names a floating type
// ERROR_EXPECTED: mode 'TF' names a floating type
// ERROR_EXPECTED: mode 'V4SI' names a vector type
// ERROR_EXPECTED: not supported on an enumerated type
// ERROR_EXPECTED: not supported on a pointer
// ERROR_EXPECTED: not supported on a function parameter
// ERROR_EXPECTED: mode applied to inappropriate type '_Bool'
// ERROR_EXPECTED: mode applied to inappropriate type 'int (void)'
// ERROR_EXPECTED: unknown machine mode '__ZZ__'
/* ONE DIRECTIVE PER REFUSAL, and that is the point rather than thoroughness
 * for its own sake: ERROR_EXPECTED matches ONE message, so a single line
 * covering ten constructs would keep passing while nine of them silently
 * started being accepted. Every distinct message below is pinned, including
 * the tails that tell the four "only integer machine modes" cases apart.
 *
 * The `mode` attribute is HALF implemented, and this fixture is the other
 * half: every construct below is one gcc ACCEPTS and this compiler refuses
 * by name. Without it, "refused" and "quietly accepted and ignored" look
 * identical from outside -- and ignoring a mode gives the declaration a
 * type of the wrong size with nothing to show for it.
 *
 * The boundary is deliberate, not incidental. An integer mode is a width
 * and a signedness, both of which this type system has. The others each
 * name a type it does not have:
 *
 *   TI       a 128-bit integer
 *   SF/DF/XF/TF  a floating type selected by width, which would silently
 *            disagree with the target's own float/double/long double --
 *            note x86-64's long double is x87 80-bit, so TF is NOT it
 *   V*       a vector type, with no SysV or AAPCS64 parameter contract
 *            (the same reason `vector_size` is refused)
 *
 * The enum and pointer rows are a different kind of no, and they get a
 * different message for it: gcc accepts both, so borrowing gcc's
 * "inappropriate type" wording there would tell the author their program
 * is invalid C when it is only unsupported here.
 *
 * The parameter row is the one that would otherwise be a WARNING. A
 * parameter has no symbol, so every other attribute warns and drops there;
 * dropping a mode would hand the callee an `int` where the caller passed a
 * `long`, which is a calling-convention mismatch, so it is an error. */
typedef int ti_mode __attribute__((__mode__(__TI__)));
typedef float df_mode __attribute__((__mode__(__DF__)));
typedef float tf_mode __attribute__((__mode__(__TF__)));
typedef int v4si_mode __attribute__((__mode__(__V4SI__)));

/* gcc accepts these two; we refuse them with a message that says so. */
enum E { E0 };
typedef enum E enum_mode __attribute__((__mode__(__DI__)));
typedef int *ptr_mode __attribute__((__mode__(__DI__)));

/* gcc rejects these two as well, with the wording this one borrows. */
typedef _Bool bool_mode __attribute__((__mode__(__DI__)));
int func_mode(void) __attribute__((__mode__(__DI__)));

/* A typo must not read as an unimplemented feature. gcc's wording. */
typedef int typo_mode __attribute__((__mode__(__ZZ__)));

/* The calling-convention row. */
void param_mode(int p __attribute__((__mode__(__DI__))));
