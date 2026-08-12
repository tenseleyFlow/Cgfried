# Sprint 57 campaign findings

This ledger covers defects and explicit exclusions discovered while compiling
the pinned musl, Chibicc, TinyCC, and QBE trees.  There are **no unresolved
critical findings**.  `CAMP-MUSL-001` and `CAMP-MUSL-002` are deliberate,
published scope exclusions rather than unexpected failures.

## Reproducer and ratchet ritual

1. Preserve the failing command, upstream revision, source file, and complete
   diagnostic or runtime output under `build/campaigns/<project>/logs/`.
2. Reproduce with the pinned source archive and classify the first failing
   compiler phase; compare with the campaign's host-compiler lane.
3. Remove unrelated declarations and headers until one behavior remains.  A
   preprocessed input may be used while reducing, but the final case must be
   understandable C and retain the original failure.
4. Commit the minimized case to the canonical `tests/programs/` regression
   corpus (or a focused unit test when no source program can expose the bug)
   before changing the compiler.
5. Record a stable `CAMP-<project>-NNN` ID, phase, status, and regression
   evidence here.  Never turn a new failure into an expected result merely to
   make the campaign green.
6. After the fix, run the focused regression, the affected upstream campaign,
   and the exact bidirectional `.expected` gate.  A new pass is drift too and
   requires the baseline and this ledger to change together.

## Scope exclusions

| ID | Severity | Status | Phase | Finding / disposition |
|---|---|---|---|---|
| `CAMP-MUSL-001` | scope | DEFERRED | language | `src/complex/*` is compiled by the host compiler because C `_Complex` is explicitly post-v0.1.0.  The results file reports both routed TU counts; all other C TUs must use Cgfried. |
| `CAMP-MUSL-002` | scope | DEFERRED | TLS / dynamic link | The shared-musl lane remains disabled pending general-dynamic TLS and full dynamic-loader validation.  The static archive, crt, hello, benchmarks, and libc-test parity lanes remain mandatory. |

## Fixed compiler findings

All rows below are fixed by the Sprint 57 integration change and have a
permanent minimized regression.  The phase attribution names the repaired
compiler boundary, not the upstream project that happened to expose it.

| ID | Severity | Phase | Finding | Regression evidence |
|---|---|---|---|---|
| `CAMP-MUSL-003` | high | parse / sema | Materialize ISO `__func__` and GNU `__FUNCTION__` as function-local constant arrays. | `tests/programs/lower-exec/exec_func_name.c`, `tests/programs/sema/err_func_name_const.c` |
| `CAMP-MUSL-004` | medium | parse | A typedef-name token remains legal in the label namespace. | `tests/programs/parse/typedef_named_label.c` |
| `CAMP-MUSL-005` | high | x86-64 emit | Canonicalize 32-bit immediate bit patterns instead of rejecting valid unsigned spellings. | `tests/programs/lower-exec/exec_imm32_bitpattern.c` |
| `CAMP-MUSL-006` | high | lowering | Lower every file-scope sibling in a comma declaration. | `tests/programs/lower-exec/exec_global_siblings.c` |
| `CAMP-MUSL-007` | high | constexpr / initializer | Preserve designator metadata and sort address relocations so forward references and nested pointer designators remain deterministic. | `tests/programs/lower-exec/exec_static_pointer_designators.c` |
| `CAMP-MUSL-008` | high | sema | Apply integer promotion to a `switch` controlling expression. | `tests/programs/lower-exec/exec_switch_char_promotion.c` |
| `CAMP-TINYCC-001` | medium | lex | Accept GNU binary integer constants and diagnose malformed/pedantic cases. | `tests/programs/gnu/binary_integer_constants.c`, `tests/programs/gnu/binary_integer_constants_pedantic.c`, `tests/programs/lex/err_bad_binary.c` |
| `CAMP-MUSL-009` | critical | runtime / link | Supply hidden ELF `__dso_handle` and resolve the runtime/libc archive group without link-order dependence. | `tests/programs/lower-exec/exec_atexit_dso_handle.c`, `tests/unit/test_link.c` |
| `CAMP-MUSL-010` | high | optimizer | Preserve provenance and token uses through CFG simplification, jump threading, LCSSA, mem2reg, and inlining; conservatively retain f80 and stack-state constructs. | `tests/programs/lower-exec/exec_f80_loop_optimized.c`, focused `test_opt_*` unit suites |
| `CAMP-MUSL-011` | high | x86-64 isel / regalloc | Localize inline-asm register operands at the asm site so a memory operand cannot pin a long-lived no-spill interval. | `tests/programs/gnu/asm_operand_locality.c` |
| `CAMP-MUSL-012` | high | soft-float | Propagate decimal big-integer limb carry during correctly-rounded conversion. | `tests/unit/test_softfp.c` |
| `CAMP-TINYCC-002` | medium | driver | Stage assembly in a real temporary file when `-c -o /dev/null` is requested. | `scripts/driver_matrix.sh` |
| `CAMP-MUSL-013` | high | sema / ELF | Permit an alias definition after a compatible prototype without losing alias-definition state. | `tests/corpus/x86_64/int/attr_alias.c` |
| `CAMP-MUSL-014` | high | constexpr | Fold traditional `offsetof` expressions through direct and indexed member access. | `tests/programs/builtins/offsetof_matrix.c` |
| `CAMP-MUSL-015` | high | inline asm | Implement alternative `Nd`/`dN` constraints and x87 floating constraints used by system headers. | `tests/programs/gnu/asm_constraint_nd.c`, `tests/programs/gnu/asm_constraint_fp.c` |
| `CAMP-MUSL-016` | high | lowering | Lower f80 conditional expressions through a scalar temporary without illegal SSA merging. | `tests/programs/lower-exec/exec_f80_conditional.c` |
| `CAMP-MUSL-017` | high | initializer | Accept a braced string literal when initializing a character array. | `tests/programs/lower-exec/exec_braced_string_array.c` |
| `CAMP-QBE-001` | high | x86-64 isel | Reach a fixed point when selecting forward-dependent address plans. | `tests/programs/lower-exec/exec_forward_address_plan.c` |
| `CAMP-MUSL-018` | high | constexpr / initializer | Clear the complete replaced subobject, including zero bits and overlapping relocations, when a later designated initializer overrides an earlier aggregate or union member. | `tests/programs/lower-exec/exec_initializer_override.c`, `tests/programs/constexpr/init_images.c` |
| `CAMP-MUSL-019` | high | lowering | Keep symbol interning separate from definition emission so a forward initializer relocation cannot suppress the referenced global's later storage definition. | `tests/programs/lower-exec/exec_forward_global_definition.c` |
| `CAMP-MUSL-020` | high | IR / ELF emit | Preserve `weak` and non-default visibility on undefined symbols through textual IR and both ELF emitters; hidden weak externals such as `_DYNAMIC` must link and resolve to null when absent. | `tests/programs/gnu/weak_undefined.c`, `tests/unit/test_ir_core.c` |
| `CAMP-MUSL-021` | critical | local initializer lowering | Preserve relocations when the zero-tail optimizer splits a large automatic aggregate into a template copy plus zero fill.  Dropping a function-pointer relocation produced a null indirect call in musl `vsnprintf`. | `tests/programs/lower-exec/exec_local_large_reloc_init.c` |
| `CAMP-MUSL-023` | high | local initializer lowering | Sort automatic aggregate relocations by object offset and clear stale bytes/relocations when later designators or union members replace earlier values. | `tests/programs/lower-exec/exec_local_backward_pointer_designators.c`, `tests/programs/lower-exec/exec_local_union_pointer_string_override.c` |
| `CAMP-MUSL-024` | high | local initializer lowering | Deactivate deferred runtime stores when a later initializer replaces the same scalar, relocation, bitfield, aggregate, or union subobject; bitfield invalidation is bit-precise so neighboring fields remain live. | `tests/programs/lower-exec/exec_local_runtime_scalar_override.c`, `tests/programs/lower-exec/exec_local_runtime_bitfield_override.c`, `tests/programs/lower-exec/exec_local_runtime_union_string_override.c` |

## Fixed campaign-integrity findings

These rows repair the measurement harness rather than compiler semantics.  They
are part of the gate's trust boundary and therefore receive the same permanent
finding treatment.

| ID | Severity | Campaign | Finding / disposition |
|---|---|---|---|
| `CAMP-ALL-001` | high | Chibicc / TinyCC / QBE | Every campaign now builds and tests a separate pristine host-GCC lane; Cgfried-only failures are computed explicitly instead of being confused with upstream or host failures. |
| `CAMP-QBE-002` | high | QBE ARM64 | The pinned makefile's `$define` recipe is malformed after make expansion.  The runner deterministically generates the intended `config.h` in each archived work tree without modifying the pinned reference checkout. |
| `CAMP-QBE-003` | high | QBE ARM64 | Native ARM64 uses the ARM backend while excluding exactly upstream `dark.ssa`, whose own metadata marks it unsupported on ARM64; the architecture-specific exact gate requires 31 native cases and records the one exclusion. |
| `CAMP-ALL-002` | high | all campaigns | Destructive cleanup is confined to a canonical direct child of `build/campaigns`; nested, `..`, symlinked work paths, and a symlink-traversing campaign root are rejected before removal. |
| `CAMP-ALL-003` | high | CI integration | The standalone Sprint 56 provenance fixture copies the real top-level Makefile and now stages every required Sprint 57 campaign fragment, preventing new includes from making the synthetic source-set test incomplete. |
| `CAMP-MUSL-022` | high | musl / libc-test | A clean parallel libc-test build could race `common/REPORT`, compute build directories before reading `config.mak`, and silently return success after a missing `libtest.a` link.  The runner passes `B` on the command line, builds and verifies the common runner first, and retains both failure sets plus the Cgfried-only difference. |

The committed `.expected` files are the public closure record: every fixed
finding must continue to produce `PASS`, and only the two scope IDs above may
produce `SKIP`.
