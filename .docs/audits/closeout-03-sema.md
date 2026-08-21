# Closeout: Phase 03 — Semantic Analysis

Date:            2026-08-20
Baseline commit: `89b68ead`
Reviewer:        Independent Sprint 61 closeout review

## DoD items (from the phase's sprint files, every numbered item)

- [x] S12.1 All linkage-matrix rows covered by passing unit tests, including both 6.2.2p4 orderings and the p7 error. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S12.2 `type_compatible`/`type_composite` truth-table suite ≥ 40 cases, 100% pass. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S12.3 Tag-scoping fixtures (≥ 12) match gcc-8 accept/reject/warn behavior exactly, including the prototype-scope warning text. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S12.4 Enum fixtures verify underlying-type selection for all four gcc buckets and constant type = `int`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S12.5 "did you mean" fires on a fixture and never suggests across namespaces. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S12.6 Every deferred path hard-errors naming its sprint; grep for `sema: unimplemented` returns only messages containing a sprint number. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S12.7 Valgrind-clean on the sema unit suite; no non-arena allocations in `src/sema/`. --- — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S13.1 UAC unit table ≥ 60 cases green on all five `TargetSpec`s, including both §3 trap rows per model. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S13.2 Every §9 row has a fixture; severities match gcc-8 defaults exactly (verified by the differential runner's exit codes: theirs and ours agree on 0-vs-1). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S13.3 `char` signedness fixture pair passes for x86_64-linux and arm64-linux specs; no `#ifdef` in `src/sema/` (CI grep gate added). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S13.4 Shift, `_Bool`, decay, and `?:` corner suites (≥ 25 fixtures) pass. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S13.5 All implicit conversions appear as explicit cast nodes in `-fdump-sema` output (spot-checked by `// CHECK:` fixtures). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S13.6 NPC forms beyond literal `0`/`(void*)0` hard-error naming Sprint 15; no silent acceptance. --- — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S14.1 Random-struct differential: 500/500 files accepted by gcc-8 on x86_64; ≥ 200 on aarch64 cross-gcc. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S14.2 All §5 examples and ≥ 80 unit layout cases byte-exact; all §9 rows pass plus ≥ 30 classification cases. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S14.3 `layout_classify_sysv` covers every merge/cleanup rule with at least one dedicated test each (name tests after psABI rule letters). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S14.4 `long double` table encoded in `TargetSpec` and asserted per target in fixtures. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S14.5 `_Alignas` constraint suite (≥ 10 fixtures) matches gcc-8 accept/reject. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S14.6 No host `#ifdef` in `src/sema/layout.c` (CI grep gate); functions pure — a determinism test classifies the same corpus twice into identical dumps. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S14.7 `packed`/`pragma pack`/size-0 paths hard-error naming Sprint 55; `_Alignas` non-literal operands hard-error naming Sprint 15. --- — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S15.1 All torture-table literals bit-exact in all four formats (28 pinned patterns). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S15.2 Softfloat unit vectors ≥ 300 pass; add/sub/mul/div/convert each have subnormal, overflow-to-inf, and tie-to-even cases. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S15.3 gcc-8 `.data` byte-differential green over ≥ 200 static-initializer fixtures (x86_64 + aarch64 cross), including struct/bitfield/union/string images with zeroed padding verified. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S15.4 ICE constraint suite ≥ 30 fixtures matching gcc-8 accept/reject, including `sizeof(VLA)` rejection and immediate-cast float allowance. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S15.5 `{symbol, addend}` produced for all §4 accepted forms; automatic-address and truncating-cast forms error with gcc-parity wording. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S15.6 Zero host-FPU tokens in `softfp.c`/`bigint.c`/`constexpr.c` (CI grep); library-clean check: `softfp.c` compiles standalone with only `stdint.h`/`stdbool.h`/`string.h`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S15.7 All s12–s14 constant stubs retired; `_Static_assert` fully live. --- — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S16.1 Inline matrix: all 4 rows + 6 ordering variants pass; emission decisions match `gcc-8 -std=c17` for each (verified by differential symbol check). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S16.2 Tentative resolution: 10+ fixtures incl. both `-fcommon` modes; COMMON vs zero-init recorded correctly for s19 (dump-asserted). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S16.3 VLA constraint suite ≥ 20 fixtures; jump-into-VM-scope checker rejects all 6 illegal patterns (goto fwd/back, switch, nested) and accepts jumps out/within. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S16.4 FAM suite ≥ 12 fixtures matches gcc-8 accept/reject incl. sizeof-excludes-FAM asserts. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S16.5 `_Atomic`/`_Thread_local` combo matrices fully fixtured (≥ 15 each); atomic aggregates and dynamic-TLS paths hard-error with sprint-naming messages. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S16.6 K&R promoted-parameter fixtures (float/short/char traps) warn with gcc-parity substance. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S16.7 Phase-3 closeout: zero remaining `sema: unimplemented` messages without a sprint number; sema runs clean over the full parser corpus from s9–s11 (no crashes, valgrind-clean). --- — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).

## Open audit findings against this phase

None. At remediation baseline `89b68ead`, [the Sprint 61 burndown](burndown.md) records 0 Critical / 0 High / 0 Medium / 0 Low findings, and the audit lifecycle reports 55 PASS / 0 XFAIL / 0 XPASS / 0 FAIL.

## Verdict

Every numbered phase DoD item is evidenced and no audit findings remain.

READY
