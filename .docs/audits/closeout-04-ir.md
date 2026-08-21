# Closeout: Phase 04 — IR and Lowering

Date:            2026-08-20
Baseline commit: `89b68ead`
Reviewer:        Independent Sprint 61 closeout review

## DoD items (from the phase's sprint files, every numbered item)

- [x] S17.1 All ten verifier checks implemented; each has a unit test proving it fires. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S17.2 `parse(print(M)) == M` structurally for ≥ 25 `.cgfir` fixtures; print determinism byte-pinned. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S17.3 Dominator tests: 3 CFG shapes vs hand-computed idom tables, all green. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S17.4 `ir.h` states the aggregates-in-memory law and the undef contract in comments carrying the why. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S17.5 `IrInst` ≤ 48 bytes (`_Static_assert`); module construction is arena-only (no per-inst malloc). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S17.6 Reserved opcodes and `cgf -emit-ir` on `.c` hard-error naming their sprint. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S17.7 ≥ 120 unit assertions across the four test files; ASan/UBSan clean. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S18.1 All C17 statements except VLA-bearing ones lower and verify; the exceptions hard-error naming Sprint 20/55. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S18.2 Duff's device fixture lowers, verifies, and its `// IR_CHECK:` golden pins the two-pass block structure. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S18.3 Bitfield RMW sequences pinned by goldens for signed/unsigned; assignment result-value semantics unit-tested. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S18.4 Short-circuit lowering emits zero allocas (grep the golden: `// IR_CHECK-NOT: alloca`). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S18.5 Evaluation-order fixture staged with pinned expected output; skip-counted. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S18.6 ≥ 30 lowering fixtures verifier-clean under `CGF_VERIFY_AFTER_EACH=1`; ≥ 100 unit assertions; ASan/UBSan clean. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S19.1 `lower_abi_module` rewrites every abstract call/prologue/ret; a verifier pass over the corpus finds zero abstract calls remaining post-rewrite. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S19.2 Classification table above pinned by ≥ 12 unit cases including f80, mixed eightbytes, and packed-MEMORY. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S19.3 `va_arg` goldens for int, double, `long double`, and a MEMORY struct; both va_list-passing forms lower identically (golden-compared). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S19.4 String pool emission order byte-stable across two runs (determinism test). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S19.5 Local-init threshold behavior pinned: 32-byte and 33-byte fixtures take different strategies per goldens; `{0}` big-array is exactly one memset. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S19.6 All new paths verifier-clean under `CGF_VERIFY_AFTER_EACH=1`; staged execution fixtures skip-counted exactly; ASan/UBSan clean. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S20.1 Every verifier check (Sprint 17 §9 + this sprint's additions) has a `bad/` fixture asserting that exact check fires — coverage script enumerates check numbers vs fixtures and fails on gaps. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S20.2 Volatile law pinned: count-based goldens in place; pass-manager snapshot hook implemented and ICEs on a deliberately-broken test pass. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S20.3 `calls_setjmp` set correctly (unit-tested for setjmp/sigsetjmp/_setjmp; `__builtin_setjmp` hard-errors with the documented message). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S20.4 VLA fixtures: declaration, `sizeof` side-effect-once, and all five scope-exit kinds lower and verify; goto-out-of-two-scopes golden shows one outermost-token restore. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S20.5 `_Atomic` load/store/RMW/cmpxchg-loop/flag fixtures lower and verify; every RMW op golden-pinned. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S20.6 Round-trip fuzzer runs 10^6 iterations locally with zero crashes and zero silent-accepts; CI runs a 60-second slice. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S20.7 `CGF_DUMP_BAD_IR` output re-parses; ASan/UBSan clean; staged execution fixtures skip-counted exactly. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).

## Open audit findings against this phase

None. At remediation baseline `89b68ead`, [the Sprint 61 burndown](burndown.md) records 0 Critical / 0 High / 0 Medium / 0 Low findings, and the audit lifecycle reports 55 PASS / 0 XFAIL / 0 XPASS / 0 FAIL.

## Verdict

Every numbered phase DoD item is evidenced and no audit findings remain.

READY
