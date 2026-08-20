# F10 runtime and standard headers — CLOSED

- Review date: 2026-08-20
- Baseline: `1c639e060ab38bf3daf9a4e2f2a431c9ca3041cb`
- Validation head: `b83e07470e8f4b5234627c86f5b890632ea99171`
- Scope: `src/rt/`, the runtime copies of `src/util/{softfp,bigint}.c`, and
  `include/`
- Confirmation tools: GCC 16.1.1, clang 22.1.8, AArch64 GCC 16.1.0,
  qemu-aarch64 11.0.3, GNU `nm`, and Cgfried's deterministic target dump.

## Findings

ID: `RT-H-01`
Title: arm64-macos publishes the wrong long-double size macro
Severity: High — target code sees `sizeof(long double) == 8` but
`__SIZEOF_LONG_DOUBLE__ == 16`. Valid compile-time ABI selection can therefore
choose a 16-byte representation for an 8-byte target type, producing source
incompatibility or an ABI mismatch at a library boundary.
Reproducer: `tests/audit-regressions/rt-h-01.c`
Root cause: the supposedly all-target common predefined block at
`src/target.c:132-142` hardcodes 16. The target-specific float table at
`src/target.c:376-395` and `TargetLayout` correctly model Apple's long double
as binary64/8 bytes, so the predefined size macro is the inconsistent copy.
Affected sprints: 14, 28.
Needed manifest row (not added during this independently owned front):
`RT-H-01\trt-h-01.c\tarm64-macos publishes the wrong long-double size macro`.

## Runtime attack-surface dispatch

- 128-bit integer directed differential: complete. `scripts/rt_diff.sh`
  emitted 2,317 result lines byte-identical to libgcc from 16 boundary values,
  all nonzero divisor pairings, signed and unsigned quotient/remainder,
  multiplication, shifts at 0/1/63/64/65/127, and 64-bit popcount/clz/ctz.
  The reverse mixing check (GCC object linked against `libcgf_rt.a`) passed,
  the small fp128 link smoke passed, and a rebuild reproduced the archive
  byte-for-byte.
- 128-bit integer seeded differential: complete. `tests/rt/int128_abi.sh`
  ran seed `0x58c0ffee12345678` for 4,096 cases. Each case checks unsigned
  division/remainder, signed division/remainder, multiplication, left shift,
  arithmetic and logical right shift, plus popcount/clz/ctz. GCC and clang
  runtime builds matched their scalar `__int128` references; Cgfried-built
  runtime objects matched the GCC reference at O0 and O2.
- Binary128 directed differential: complete. `scripts/fp128_diff.sh` checked
  all 24 required entry points and produced 1,432 result lines byte-identical
  to AArch64 libgcc. Its 16 raw operand images include both zero signs, finite
  boundaries, minimum and maximum subnormal, minimum normal, maximum finite,
  infinities, and quiet NaN. The matrix covers 256 ordered arithmetic and
  six-relation comparison pairs, both negations, narrowing, signed/unsigned
  integer conversions, and float/double extension.
- Binary128 seeded differential: complete audit-only probe, seed
  `0x60f10a5eed1234`. A 512-pair splitmix64 sweep injected random-payload quiet
  NaNs in both operand positions, random positive/negative subnormals, and
  random finite normals. Four arithmetic results plus all six comparisons per
  pair (2,560 checked result components) were byte-identical between
  `libcgf_rt.a` and AArch64 libgcc under qemu. The temporary source was removed;
  retained evidence is `/tmp/cgf-f10-random-{gcc,cgf}.out` for this review
  session.
- Soft-float core: all 16 focused unit tests passed, 329 assertions. These pin
  binary32/64/80/128 parsing and encoding, special values and unordered
  comparison, integer and format conversions, wide binary128 multiplication,
  and division by a minimum subnormal. `check_no_host_fpu.sh` and
  `check_target_seam.sh` passed.

## Header attack-surface dispatch

- Standard inventory: all nine compiler-owned C17 headers are present:
  `float.h`, `iso646.h`, `limits.h`, `stdalign.h`, `stdarg.h`, `stdbool.h`,
  `stddef.h`, `stdint.h`, and `stdnoreturn.h`. A clause-by-clause source pass
  covered the 22 `limits.h` integer/character limits; the 40 `float.h`
  radix/rounding, decimal-digit, format, true-minimum, and subnormal macros;
  all 28 `stdint.h` typedefs supplied by this implementation, their limit
  families and ten constant macros; all required `stddef.h`, `stdarg.h`,
  `stdbool.h`, `stdalign.h`, `iso646.h`, and `stdnoreturn.h` names. No
  non-reserved extension name is added by an ISO header. The reserved
  `__gnuc_va_list` extension in `stdarg.h` is the documented glibc interop
  protocol.
- Native x86_64 evaluated differential: 148 typedef/property and macro-value
  lines were byte-identical to GCC. The sole removed comparison row is the
  documented `MB_LEN_MAX` policy (`4` in Cgfried, `16` in glibc); both expected
  values were checked. The lane also proved `stdio.h` resolves to the system
  header rather than being shadowed.
- AArch64 Linux evaluated differential: the same 148-line probe, compiled to
  an object by Cgfried and by AArch64 GCC and executed under qemu, was
  byte-identical after the same documented `MB_LEN_MAX` row was removed. This
  independently checks unsigned plain `char`, unsigned `wchar_t`, LP64 exact/
  least/fast integer types and suffixes, binary128 `long double`, floating
  extrema, and `max_align_t` against the target compiler rather than this x86
  host.
- Five-target static matrix: `tests/cross/determinism.sh` compiled all five
  targets. Direct predefined-macro oracles confirmed signed char plus x87-80
  (`LDBL_MANT_DIG=64`) for all three x86 targets, unsigned char plus binary128
  (`113`) for arm64-linux, and signed char plus binary64 (`53`) for
  arm64-macos. Darwin's natural-width `int_fast16_t`/`int_fast32_t`, `long
  long`-spelled `int64_t`, and the Linux/FreeBSD widened fast types were also
  inspected in the emitted target definitions. This matrix exposed
  `RT-H-01`.
- Extension-header portability: `scripts/header_portability.sh` passed. All
  five `cgfried/memsafe.h` annotations disappear cleanly under GCC, survive
  install staging byte-for-byte, and activate only under Cgfried.

## Validation evidence

- `CGF_RT_WORK=/tmp/cgf-f10-rt BUILD=build sh scripts/rt_diff.sh
  build/cgfried`: 2,317 lines matched libgcc; both mixing directions, fp128
  smoke, and archive reproducibility passed.
- `CGF_TEST_CC=build/cgfried sh tests/rt/int128_abi.sh`: GCC and clang seeded
  references passed; Cgfried O0/O2 runtime objects passed.
- `CGF_FP128_WORK=/tmp/cgf-f10-fp128 sh scripts/fp128_diff.sh`: 24 symbols,
  1,432 lines matched AArch64 libgcc.
- Seeded audit fp128 probe: 512/512 operand pairs matched, including injected
  random NaN payloads and subnormals.
- `build/unit_tests --filter softfp`: 16 tests, 329 assertions, zero failures.
- `CGF_AS=0 CGF_HEADER_WORK=/tmp/cgf-f10-header sh
  scripts/header_diff.sh build/cgfried`: 148 lines matched GCC; nine standard
  headers plus the namespaced extension header; no libc shadowing.
- AArch64 Linux header oracle: 148 lines matched AArch64 GCC after the one
  documented `MB_LEN_MAX` row was removed.
- `CGF_CROSS_WORK=/tmp/cgf-f10-cross sh tests/cross/determinism.sh
  build/cgfried`: five targets compiled and their sysroots were verified.
- `RT-H-01`: `build/cgfried --target=arm64-macos -fsyntax-only
  tests/audit-regressions/rt-h-01.c` exits 1 at the macro/type equality static
  assertion while the preceding `sizeof(long double) == 8` assertion passes.

## Closure

**CLOSED for Sprint 60 finding collection.** Every F10 checklist item has
directed and deterministic-random runtime evidence or a standard-header/
target-matrix dispatch. `RT-H-01` is confirmed, reproducible, and intentionally
unfixed for Sprint 61; its manifest row is listed above for the shared manifest
owner. The ordinary integer runtime, fp128 runtime, and required standard-header
surface otherwise matched their independent GCC/libgcc oracles in the tested
matrices.

## Unverified observations

- No native arm64-macos compiler/execution host was available. `RT-H-01` does
  not depend on one: the same compiler invocation proves its target type is 8
  bytes and its published macro says 16. Other Darwin header claims are backed
  by target-definition/static compilation, not native execution.
