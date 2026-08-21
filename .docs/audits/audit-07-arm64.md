# F07 ARM64 backend — CLOSED

- Review date: 2026-08-20
- Baseline: `1c639e060ab38bf3daf9a4e2f2a431c9ca3041cb`
- Validation head: `b83e07470e8f4b5234627c86f5b890632ea99171`
- Scope: `src/cg/arm64/`, ARM64 assembly and unwind fixtures
- Confirmation tools: AArch64 GCC 16.1.0, GNU assembler/readelf 2.47,
  qemu-aarch64 11.0.3, and clang 22.1.8.

## Findings

~~ID: `A64-H-01`~~
~~Title: TLS tentative definitions are emitted as COMMON symbols~~
~~Severity: High — valid `-fcommon` TLS output is rejected by the assembler and~~
~~cannot produce an object file.~~
~~Reproducer: `tests/audit-regressions/a64-h-01.c`~~
~~Root cause: `src/cg/arm64/emit.c:1371-1389` handles tentative definitions~~
~~with `.comm` before the TLS section selection at lines 1427-1433. References~~
~~still use TLS relocations, so GNU assembler rejects the inconsistent symbol.~~
~~Affected sprints: 49, 51.~~
Resolution: RESOLVED 2026-08-20 by `2b4ab767`. Tentative TLS objects now bypass
ELF COMMON and use the established `.tbss`/`@tls_object` path. Cross-assembled
section, symbol, and TLSLE relocation evidence matches AArch64 GCC; ordinary
non-TLS COMMON and the effective Mach-O zero-fill form remain unchanged.

ID: `A64-H-02`
Title: large TLS addends are emitted as unencodable immediates
Severity: High — valid optimized C produces assembly rejected by the target
assembler.
Reproducer: `tests/audit-regressions/a64-h-02.c`
Root cause: `src/cg/arm64/emit.c:941-943` prints a folded TLS addend as one
unvalidated `add #imm`; 5000 is not encodable by ARM64 add-immediate.
Affected sprints: 49, 51.

ID: `A64-H-03`
Title: large GOT addends trigger an internal compiler error
Severity: High — valid C with a large external-object offset aborts the
compiler instead of materializing the address.
Reproducer: `tests/audit-regressions/a64-h-03.c`
Root cause: `src/cg/arm64/emit.c:426-443` assumes no C object offset reaches
16 MiB and hard-ICEs when the magnitude exceeds its two-add scheme.
Affected sprints: 50, 51.

ID: `A64-M-04`
Title: unwind information omits x19 and epilogue state transitions
Severity: Medium — generated code executes, but stack unwinding across a
callee-saved function can recover stale registers and CFA state.
Reproducer: `tests/audit-regressions/a64-m-04.c`
Root cause: register allocation records only frame and x29/x30 pair metadata
at `src/cg/arm64/regalloc.c:2049-2057`; `src/cg/arm64/debug.c:137-193` emits
only those prologue rows and no callee-save or epilogue restore rules.
`readelf --debug-dump=frames` shows no x19 offset/restore and no final CFA
offset zero, while the function saves and restores x19 in its code.
Affected sprints: 29, 49, 51.

## Attack-surface dispatch

- Immediate/address encodability: complete deterministic pass. The production
  logical-immediate encoder accepted all 5,334 unique 64-bit encodable masks;
  its raw `N:immr:imms` words were byte-identical to GNU `as` output. Unit
  boundaries cover add/sub imm12, shifted imm12, rejected gaps and
  `INT64_MIN`; load/store selection covers scaled `uimm12`, signed unscaled
  imm9, pre/post imm9, forced materialization, and the signed scaled imm7
  `ldp`/`stp` limits. `A64-H-02` and `A64-H-03` remain confirmed because the
  TLS/GOT special paths bypass those ordinary selectors.
- TLS symbol emission: complete focused pass; `A64-H-01` remains confirmed.
- Unwind state: complete focused pass; `A64-M-04` remains confirmed.
- HFA classification: complete deterministic pass for reachable C types. The
  table covers scalar leaves, one through four leaves, rejection above four, nested
  structs, arrays, overlaying unions, mixed leaf types, zero-length arrays,
  over-alignment, Linux binary128 leaves, and Apple's double-width `long
  double`. The argument/return classifier, layout predicate, and 35 generated
  mixed-link signatures all passed; each generated signature ran in both
  compiler directions against AArch64 GCC. The checklist's empty-struct-member
  row cannot reach this backend: Cgfried rejects `struct empty {};` in GNU17
  before lowering, while clang classifies `struct { struct empty e; float x,
  y; }` as a two-leaf HFA on both Linux and macOS. This is an explicit
  front-end capability boundary, not an ARM64 backend mismatch.
- Linux varargs: complete. The 32-byte five-field `va_list` shape and all
  offsets passed 13 GCC `_Static_assert` oracles; register-save-area and HFA
  exhaustion accounting passed the unit matrix.
- Apple varargs/stack rules: deterministic target-independent checks passed
  for anonymous HFA/non-HFA shape and natural-size stack packing. Both
  Cgfried and clang accepted the row-2/row-3 static caller fixture for an
  arm64-macos target. Native mixed-link execution is honestly skipped on this
  Linux host (`HARNESS_SKIP ... count=6 reason="not arm64 Darwin"`).
- Atomic mappings: complete for the v0.1.0 policy. All C memory orders are
  deliberately strengthened to the IR's sole `seq_cst` order. Generated
  assembly used `ldar` for a sequentially consistent load, `stlr` for a
  sequentially consistent store, and `ldaxr`/`stlxr` retry loops for five
  RMW operations plus compare-exchange; compare-exchange's miss path emitted
  `clrex`. The atomic execution fixture passed under qemu, including the
  four-thread add/CAS hammer. This is semantic smoke evidence only: qemu-user
  is not evidence for weak-memory ordering on real hardware.

## Validation evidence

- `build/unit_tests --filter a64`: 44 tests, 4,170,907 assertions, zero
  failures.
- `build/unit_tests --filter abi_aapcs64`: 8 tests, 133 assertions, zero
  failures; `test_layout_hfa`: 25 assertions; `test_abi_apple_anonymous_shape`:
  11 assertions.
- Direct logical-immediate oracle: 5,334 production-accepted encodings,
  21,336 `.text` bytes, byte-identical between GNU-assembled mnemonic input
  and production-packed instruction words.
- `scripts/abi_differential_lane.sh`, `arm64-linux`, requested count 24:
  35 signatures (fixed plus generated) agreed with AArch64 GCC in both
  caller/callee directions.
- `scripts/a64_exec_lane.sh`: 12 MIR modules emitted and assembled; 10 linked
  programs executed successfully under qemu-aarch64 11.0.3.
- Existing finding revalidation:
  - `A64-H-01`: compiler exit 0, one `.comm tls_counter`, GNU assembler exit
    1 with `Accessing 'tls_counter' as thread-local object`.
  - `A64-H-02`: compiler exit 0, one `add ..., #5000`, GNU assembler exit 1
    with `immediate out of range`.
  - `A64-H-03`: compiler exit 4 at the documented 16 MiB GOT addend ICE.
  - `A64-M-04`: compiler/assembler/readelf all exit 0 and code saves x19,
    but frame data contains zero x19 offset rules, zero x19 restore rules,
    and zero final CFA-offset-zero transitions.
- Bundled-assembler comparison lanes were skipped because
  `afs-as/target/release/afs-as` was not built. GNU assembler acceptance and
  the direct logical encoding oracle still ran; no afs-as parity claim is
  made by this review.

## Closure

**CLOSED for Sprint 60 finding collection.** Every F07 attack item is
explicitly dispatched above with unit, cross-tool, differential, static-oracle,
or execution evidence. `A64-H-01`, `A64-H-02`, `A64-H-03`, and `A64-M-04`
remain reproducible and intentionally open for Sprint 61 remediation; each has
a self-describing `tests/audit-regressions/` fixture and manifest row. Native
arm64-Darwin mixed-link execution and native ARM64 weak-memory litmus evidence
are recorded platform limitations, not silent passes. The empty-struct HFA row
is explicitly blocked before backend entry by the documented front-end
refusal. No new F07 regression input or manifest row is required.

## Unverified observations

None recorded.
