# Fast-math policy

Cgfried defaults to strict floating-point transformations. `-Ofast` selects
the `-O3` pipeline and enables the same complete bundle as `-ffast-math`.
`-fno-fast-math` disables the bundle without changing the selected
optimization pipeline. Options are processed from left to right, so a later
`-O` or `-f[no-]fast-math` controls the final state.

## Bundle semantics

| License | Meaning under `-ffast-math` or `-Ofast` |
| --- | --- |
| Reassociation | Floating `+` and `*` may be reassociated or commuted. This permits split accumulators and vector floating reductions. |
| No signed zeros | `-0.0` and `+0.0` may be treated as interchangeable. |
| Finite math | Inputs and results are assumed not to be NaN or infinity. Supplying either has undefined behavior. |
| Reciprocal math | Accepted bundle license; no reciprocal rewrite is implemented yet. |
| Non-trapping evaluation | FP exception side effects need not be preserved. This licenses constant folding and eager/control-flow motion that strict modes reject when they could raise an exception. |
| No math errno | Accepted but inert. Calls remain opaque and effectful, and no library-math folding is implemented. |

The individual compatibility spellings
`-f[no-]associative-math`, `-f[no-]signed-zeros`,
`-f[no-]finite-math-only`, `-f[no-]reciprocal-math`, and
`-f[no-]math-errno` are recognized but do not create partially configured
modes in v0.1.0. Each use warns and points to this document. Use
`-ffast-math` or `-fno-fast-math` to select the complete behavior.

## Contraction and the ISA ceiling

`-ffp-contract=off`, `on`, `fast`, and `fast-honor-pragmas` are accepted for
command-line compatibility and warn that the option is bundled-only. They do
not alter x86-64 code generation: Cgfried's x86 baseline is SSE2, which has no
fused multiply-add instruction. FP contraction remains off. A future backend
with an appropriate instruction must define and test its contraction policy
before honoring these settings.

## IEC 60559 macro claim

`__STDC_IEC_559__` is **undefined in every mode**, including default strict
math. The preprocessor records `#pragma STDC FENV_ACCESS`, but that state is
not yet carried through IR optimization. Defining the macro before rounding
modes and floating exceptions are preserved end to end would overstate Annex
F conformance.

The target-specific `__FLT_IS_IEC_60559__`, `__DBL_IS_IEC_60559__`, and
`__LDBL_IS_IEC_60559__` macros describe the storage formats; they are not a
claim that the full translation and execution environment implements Annex F.

## Deliberate limits

- Fast-math is permission, not a promise that every licensed rewrite occurs.
- NaN and infinity behavior is unsupported only while the bundle is enabled;
  strict modes retain their ordinary value semantics.
- Strict modes conservatively retain exception-raising FP operations across
  constant folding and control-flow motion. The complete bundle may erase or
  eagerly execute those operations; a partial test configuration does not
  grant that license.
- The no-math-errno policy does not change the host C library's
  `math_errhandling` value, remove call side effects, or license a library
  function fold in v0.1.0.
- x86-64 emits no FMA under this policy.
