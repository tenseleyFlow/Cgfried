# F03 sema/layout — CLOSED

- Review date: 2026-08-15
- Baseline: `1c639e060ab38bf3daf9a4e2f2a431c9ca3041cb`
- Scope: `src/sema/`, semantic and layout fixtures
- Confirmation oracles: GCC 16.1.1, Clang 22.1.8, AArch64 GCC 16.1.0,
  GCC 8.3.0 (Debian 8.3.0-6), Clang 7.0.1 (Debian
  1:7.0.1-8+deb10u2), and MPFR 4.2.2 as an offline floating-point fixture
  generator.

## Regression status (2026-08-20)

Fresh regression runs from the current worktree are green:

- `CGF_TEST_CC=build/cgfried build/cgf-test --profile linux-x86_64 tests/programs/conv`
  — 20/20 passed.
- `build/unit_tests --filter test_conv` — 12 tests, 319 assertions, zero
  failures.
- `CGF_TEST_CC=build/cgfried build/cgf-test --profile linux-x86_64 tests/programs/layout`
  — 11/11 passed.
- `build/unit_tests --filter test_layout` — 9 tests, 166 assertions, zero
  failures.
- `CGF_LAYOUT_GEN=build/gen_layout CGF_LAYOUT_WORK=build/audit-f03-layout sh scripts/layout_diff.sh build/cgfried 500 1`
  — 500/500 generated records agreed with GCC on size, alignment, and member
  offsets.
- `CGF_TEST_CC=build/cgfried build/cgf-test --profile linux-x86_64 tests/programs/sema`
  — 30/30 passed, including the linkage fixtures.
- `build/unit_tests --filter test_sema` — 16 tests, 199 assertions, zero
  failures, including the linkage matrix and block-scope linkage cases.
- `CGF_TEST_CC=build/cgfried build/cgf-test --profile linux-x86_64 tests/programs/constexpr`
  — 13/13 passed.
- `build/unit_tests --filter test_softfp` — 16 tests, 329 assertions, zero
  failures.
- `CGF_FP_WORK=build/audit-f03-fp sh scripts/fp_diff.sh build/fpdiff` — 50/50
  literals matched GCC bit-for-bit in binary32 and binary64.
- `CGF_INIT_WORK=build/audit-f03-init sh scripts/init_diff.sh build/cgfried` —
  53/53 initializer images were byte-identical to GCC.
- Historical compiler replay reproduced the oracle results: GCC 8.3.0 passed
  500/500 generated layout records, and GCC 8.3.0 plus Clang 7.0.1 each
  passed 50/50 soft-float literal rows and 53/53 initializer-image rows.
  Clang 7 rejects the canonical layout harness's hand-written null-pointer
  offset expression as non-constant; an ephemeral evidence-only substitution
  of `__builtin_offsetof` made the same 500/500 generated records pass. No
  shared script or fixture was changed.
- Historical finding probes confirmed that GCC 8.3.0 and Clang 7.0.1 both
  reject `SEMA-H-03` and `SEMA-H-04`, both accept `SEMA-H-07`, and both give
  the GNU zero-length records in `SEMA-H-06` sizes 0/0/4 (struct/union/nested).
  GCC 8.3.0 rejects `SEMA-H-05` under `-std=c17 -pedantic-errors`, while
  Clang 7.0.1 accepts it; current Clang 22 rejects it. This is a historical
  extension-policy difference, not a new Cgfried finding.
- `sh scripts/check-audit-fixtures.sh build/cgfried` — 29 checks, 29 expected
  failures, zero unexpected passes, and zero harness failures. In particular,
  all six F03 fixtures (`SEMA-C-01`, `SEMA-C-02`, `SEMA-H-03`, `SEMA-H-04`,
  `SEMA-H-05`, and `SEMA-H-06`) still reproduce their recorded compiler
  correctness failures.
- `tests/audit-regressions/sema-h-07.c` — GCC and Clang accept under
  `-std=c17 -pedantic-errors`; Cgfried rejects with "this is not a constant
  expression". Shared manifest/harness wiring is integrated separately.
- `cc -std=c17 -O2 $(pkg-config --cflags mpfr) -o /tmp/f03-mpfr-oracle
  tests/tools/f03_mpfr_constexpr_oracle.c $(pkg-config --libs mpfr)` rebuilt
  the oracle against MPFR 4.2.2; `/tmp/f03-mpfr-oracle >
  /tmp/f03-constexpr-float-mpfr.regen.c` plus `cmp` proved its output
  byte-identical to the checked-in fixture. Running
  `CGF_TEST_CC=build/cgfried build/cgf-test --profile linux-x86_64
  tests/audit-f03` then passed 1/1 fixture and all 41 baked MPFR expectations.

Checklist complete: **Yes.** The remaining deterministic matrices were run on
2026-08-20. They found one additional ordinary-C conformance failure,
`SEMA-H-07`. All F03 attack surfaces are now explicitly dispatched, every
confirmed finding has a self-describing reproducer, and no audit evidence
remains missing. F03 is therefore **CLOSED for Sprint 60 collection**; the
seven findings remain open repair work for Sprint 61.

### Remaining-matrix closeout (2026-08-20)

- Usual arithmetic conversions: a generated 15-by-15 matrix covered all 225
  ordered pairs of `_Bool`, the six sub-`int` integer types, the six
  `int`-through-`unsigned long long` types, and the three standard floating
  types. GCC and Clang accepted 225/225 independently computed `_Generic`
  type assertions. Cgfried's `-fdump-sema` then showed 225/225 generated
  functions returning the expected expression type with zero outer return
  casts (operand promotions remained visible). The pre-existing five-target
  type-level table remains 210/210 assertions green. Attempting to use the
  same `_Generic` assertions directly exposed `SEMA-H-07`.
- Bitfields: 172 deterministic x86_64 records covered all 4-by-4 ordered
  pairs of unsigned `char`/`short`/`int`/`long` allocation units, three widths
  per operand, 16 zero-width mixed-unit barriers, and 12 `_Alignas(8/16/32)`
  tails. GCC accepted 516/516 assertions over size, alignment, and tail
  offset. Separately, `scripts/init_diff.sh` proved 172/172 initialized byte
  images identical to GCC, covering the bit positions that `offsetof` cannot
  observe.
- Linkage and tentatives: six file-scope entity patterns were checked under
  both `-fcommon` and `-fno-common` (12 resolution decisions), with block
  `extern` inheritance and a block-local static as controls. Cgfried's common,
  zero-init, initialized, internal, and external states agreed with GCC's
  `C`/`B`/`D`/`b` object classes in both modes. The focused tentative unit test
  remained 11/11 assertions green and the full `sema16` corpus was 40/40.
- Compile-time floating rounding: 31 boundary literals targeted binary32 and
  binary64 half-way neighbors, largest finite values, normal/subnormal
  boundaries, and least subnormals in both decimal and hexadecimal forms.
  Source-level `float`-suffix and `double` initializer images were 62/62
  byte-identical to GCC. Together with the established 50/50 literal
  differential (two formats each), this adds 62 independently observed
  boundary images without a new compiler failure. The required non-compiler
  oracle is now baked in `tests/audit-f03/constexpr_float_mpfr.c`: MPFR 4.2.2
  generated 41 expected IEEE images under round-to-nearest-even, comprising
  21 binary32/binary64 literal conversions, 16 target-precision add/subtract/
  multiply/divide expressions, and four double-to-float conversions. The
  checked-in generator is `tests/tools/f03_mpfr_constexpr_oracle.c`; MPFR is
  used only offline and is neither linked into Cgfried nor required to run the
  fixture. Regeneration was byte-identical and Cgfried passed 41/41.

## Findings

~~ID: `SEMA-C-01`~~
~~Title: signed-minimum division by minus one crashes constant evaluation~~
~~Severity: Critical — valid input reaches undefined behavior in the compiler~~
~~process and terminates it with `SIGFPE` instead of a source diagnostic.~~
~~Reproducer: `tests/audit-regressions/sema-c-01.c`~~
~~Root cause: `src/sema/constexpr.c:638-649` checks only a zero divisor before~~
~~performing host signed `i64` division/remainder; `INT64_MIN / -1` is itself~~
~~undefined in the host implementation.~~
~~Affected sprint: 15.~~
Resolution: RESOLVED 2026-08-20 by `0eca9832`.
Cluster hunt: audited signed division and remainder at every target integer
width and in every constant-evaluation mode; the same pass found and repaired
the sibling signed-multiplication host-UB and target-width overflow gap.

~~ID: `SEMA-C-02`~~
~~Title: AAPCS64 zero-width bitfields fail to raise record alignment~~
~~Severity: Critical — cross-target layout is wrong, so emitted object layout~~
~~can disagree with AAPCS64 callers and callees.~~
~~Reproducer: `tests/audit-regressions/sema-c-02.c`~~
~~Root cause: `src/sema/layout.c:161-174` aligns the next offset for `T : 0`~~
~~but unconditionally skips the record-alignment update. Cgfried reports~~
~~size/alignment 4/4 for `struct { long :0; int x; }`; AArch64 GCC proves 8/8.~~
~~Affected sprint: 14.~~
Resolution: RESOLVED 2026-08-20 by `d4d674ec`.
Cluster hunt: checked zero-width bitfields across all integer base types,
record positions, structs and unions, and the closed five-target matrix. It
found the distinct unnamed-nonzero AAPCS64 defect now tracked as `SEMA-C-08`.

ID: `SEMA-H-03`
Title: old-style function compatibility ignores default promotions
Severity: High — incompatible function declarations are merged, leaving
miscompile-adjacent call ABI disagreement latent in the translation unit.
Reproducer: `tests/audit-regressions/sema-h-03.c`
Root cause: `src/sema/types.c:154-163` returns true whenever either function
type lacks a prototype, bypassing the default-promotion and variadic
compatibility constraints. `char`, `float`, and variadic conflicts all pass;
GCC rejects them.
Affected sprints: 12, 16.

ID: `SEMA-H-04`
Title: no-linkage and external-linkage declarations are merged
Severity: High — distinct C entities are treated as redeclarations, a scope
and linkage standards violation adjacent to wrong symbol binding.
Reproducer: `tests/audit-regressions/sema-h-04.c`
Root cause: `src/sema/decl.c:1121-1144` rejects only NONE/NONE and a later
INTERNAL mismatch; either ordering of automatic and block-scope `extern`
therefore falls through to type merging. GCC and Clang reject both orderings.
Affected sprint: 12.

ID: `SEMA-H-05`
Title: unary negation of a signed minimum is accepted as constant
Severity: High — an integer constant expression with signed overflow is
accepted and assigned a value instead of receiving a required diagnostic.
Reproducer: `tests/audit-regressions/sema-h-05.c`
Root cause: `src/sema/constexpr.c:1044-1050` implements unary minus as
unsigned `0 - value` followed by `fit`, without checking the signed-minimum
case. GCC 8.3.0, GCC 16.1.1, and Clang 22.1.8 reject under
`-pedantic-errors`; Clang 7.0.1 accepts it as a historical extension-policy
difference.
Affected sprint: 15.

ID: `SEMA-H-06`
Title: a record of only zero-length arrays is sized to its alignment
Severity: High — a silent ABI divergence from GCC on a supported GNU shape:
`sizeof` and every enclosing member offset differ, so a mixed link disagrees
about layout while each compiler stays self-consistent. Torn toward Critical,
because across a GCC boundary this IS wrong code and the project treats
mixed-link layout agreement as a shipped requirement (see `packed`); filed
High because no single-compiler program miscompiles and the shape is rare.
Reproducer: `tests/audit-regressions/sema-h-06.c`
Root cause: `src/sema/layout.c:220-221` and `:280-281` both end with
`if (tag->size == 0) tag->size = align;`. The struct-path comment claims it is
"only reachable for a FAM-only shape", and that claim is wrong in both
directions: a FAM-only struct is REFUSED by sema ("a flexible array member in
a struct with no other named members is a GNU extension that is not
supported"), so the named shape cannot reach it, while a record whose members
are all zero-length arrays does reach it and is silently mis-sized. The union
path carries the identical fixup with no comment at all.
Measured (GCC 16.1.1 vs Cgfried, x86_64): `struct { int x[0]; }` 0 vs 4;
`union { int x[0]; }` 0 vs 4; `struct { int x[0]; int y[0]; }` 0 vs 4;
`struct { struct A a; int n; }` where `A` is the above, 4 vs 8; array stride
`sizeof(struct A[2])/2` 0 vs 4. The trailing idioms agree exactly:
`struct { int n; char p[0]; }` and `struct { char p[0]; int n; }` are both 4
in each compiler, with `offsetof` agreeing — which is why ordinary code never
surfaced this.
Affected sprint: 14.
Cross-front note for F11: `tests/tools/gen_layout.c` never emits a
zero-length array, so the 2,000-record-per-run layout differential cannot
reach this shape. The generator's coverage bounds what the differential can
prove.

ID: `SEMA-H-07`
Title: selected `_Generic` integer result is not an integer constant expression
Severity: High — valid C17 is rejected in every context requiring an integer
constant expression, including `_Static_assert`, enum values, bitfield widths,
and array bounds. The selected association is an integer constant even though
the controlling expression is unevaluated; GCC 16.1.1 and Clang 22.1.8 accept
the minimized reproducer under `-std=c17 -pedantic-errors`.
Reproducer: `tests/audit-regressions/sema-h-07.c`
Root cause: semantic typing selects and types `_Generic` in
`src/sema/expr.c:1501`, but the constant evaluator's expression switch in
`src/sema/constexpr.c:820-1219` has no `AST_EXPR_GENERIC` case. It reaches the
default "this is not a constant expression" diagnostic instead of evaluating
the already-selected association.
Affected sprints: 10, 15.

ID: `SEMA-C-08`
Title: AAPCS64 unnamed nonzero bitfields fail to align aggregates
Severity: Critical — valid arm64-linux records have the wrong size and
alignment, so Cgfried and AAPCS64 callers/callees disagree at an ABI boundary.
Reproducer: `tests/audit-regressions/sema-c-08.c`
Root cause: `src/sema/layout.c` raises struct/union alignment for a nonzero
bitfield only when `m->name` is present. AAPCS64 requires unnamed bitfields to
contribute their declared base type's alignment too. Cgfried reports 3/1,
12/4, and 1/1 for the reproducer's two structs and union; AArch64 GCC 16.1.0
and Clang 22.1.8 both prove 8/8, 16/8, and 8/8 respectively.
Affected sprint: 14.

## Attack-surface dispatch

- Conversion/compatibility rules: `SEMA-H-03` remains confirmed; the complete
  225-pair 6.3.1.8 source matrix is clean. Its assertion oracle exposed the
  separate constant-expression failure `SEMA-H-07`.
- Bitfield layout: zero-width AAPCS64 case remains `SEMA-C-02`; its Sprint 61
  sibling hunt found the distinct unnamed-nonzero alignment failure
  `SEMA-C-08`. The 172-record x86_64 over-aligned and mixed-unit matrix is
  otherwise clean.
- Linkage matrix: focused block-scope pass confirmed `SEMA-H-04`; the expanded
  tentative/common resolution matrix is otherwise clean.
- Constant evaluation: integer edge pass confirmed `SEMA-C-01` and
  `SEMA-H-05`; 41/41 MPFR-baked floating expectations and 62/62 additional
  GCC boundary images are clean, while generic selections in ICE contexts are
  recorded as `SEMA-H-07`.

## Unverified observations

- Signed `1 << 31` is accepted by Cgfried and Clang but rejected by GCC under
  pedantic-errors. Oracle disagreement and extension policy are unresolved,
  so this is excluded from all counts.
- The generic `fp_diff.sh` oracle computes its binary32 side by first parsing
  an unsuffixed literal as binary64 and then casting to `float`. Four of the 31
  new half-way probes therefore differed from direct `SF_BINARY32` conversion
  through known double-rounding-sensitive shapes. The actual compiler-source
  oracle, which uses an `f` suffix, was 62/62 clean, and the independent MPFR
  fixture confirms both direct binary32 rounding and the distinct double-to-
  float cast results. This is excluded from compiler-finding counts and
  retained only as a possible harness-hardening follow-up.
