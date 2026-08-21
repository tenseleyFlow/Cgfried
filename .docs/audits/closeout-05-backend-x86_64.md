# Closeout: Phase 05 — x86_64 Backend

Date:            2026-08-20
Baseline commit: `89b68ead`
Reviewer:        Independent Sprint 61 closeout review

## DoD items (from the phase's sprint files, every numbered item)

- [x] S21.1 All integer-only IR fixtures from Sprints 18/20 select without ICE; FP/call fixtures ICE with the exact sprint-naming message (grep-asserted). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S21.2 `x64_mir_verify` catches: flags-clobber between def and use, two-address flag missing on marked opcodes, invalid fold (rsp index, unfit disp) — one negative test each. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S21.3 Imm-fit unit table 100% (≥ 12 cases); fold table 100% (≥ 10 cases). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S21.4 ≥ 25 MIR golden fixtures green; printer output byte-stable across runs (determinism invariant 3). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S21.5 Zero FP or call opcodes representable in MIR without tripping the verifier. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S22.1 All Sprint 21 fixtures allocate and pass MIR verification post-RA (verifier extended: no vreg survives, no overlapping phys assignment at any point). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S22.2 Two-address table: 3 cases × ≥ 4 opcodes unit-tested = 12+ green. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S22.3 `CGF_SPILL_ALL=1` lane green over the full fixture set. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S22.4 Random-MIR interpreter differential: 200/200. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S22.5 Alignment table verified by unit test for every pushes-parity × outgoing combination (≥ 8 cases); frame finalize asserts fire on injected misalign. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S22.6 Zero allocator code conditioned on opt level (grep-gate in CI: `opt_level` must not appear in regalloc.c). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S23.1 FP compare table: 12×2 recipe fixtures green under the MIR interpreter, including NaN operands for every predicate. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S23.2 u64→f64 and f64→u64 differential vs softfloat reference: 8+ edge values exact, sticky-bit case included. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S23.3 Call goldens: ≥ 10 fixtures covering both queues, overflow, sret, AL. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S23.4 Variadic prologue golden matches the asm above modulo offsets; AL=0 skip path covered. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S23.5 f80: load-op-store only (MIR verifier rejects any x87 value crossing a block boundary or call); whitelist grep-gate green. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S23.6 afs-as upstream PR opened with encode tests; tracking ID in the Sprint 24 findings table. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S24.1 Every integer+FP+call fixture emits, assembles under BOTH assemblers, and `cgf-objdiff` reports clean (strict `.text`, normalized symbols/relocs). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S24.2 `cgf -S` output assembles under system gas with zero diagnostics: 100% of fixtures. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S24.3 Findings table exists; every open row has an upstream issue link or an explicit "note only" rationale; zero silent workarounds in emit.c (grep-gate: no "afs-as" workaround comments without a findings row ID). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S24.4 Determinism: two full corpus emissions byte-identical (`.s` and `.o`). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S24.5 Assembler-failure path returns exit 4 with the offending `.s` line quoted (one injected-fault test). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S24.6 CI has both assembler lanes green; lane skew (fixture passing one, failing other) is a hard failure, not a skip. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S25.1 `cgf hello.c -o hello && ./hello` prints `hello, world` — the milestone, demonstrated in CI logs from a clean checkout. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S25.2 50/50 corpus at `-O0`, both assembler lanes, plus the `CGF_SPILL_ALL=1` lane: three green lanes total. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S25.3 Integer sublane green standalone; FP sublane separately reported. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S25.4 10/10 gcc-behavioral differential fixtures match stdout + exit code. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S25.5 `// ASM_CHECK(x86_64):` implemented per the matching rules; ≥ 8 fixtures use it (idiv, lea-fold, RIP-rel, setcc+movzx, AL-before-call among them). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S25.6 crt probe: Arch + Debian layouts pass in a chroot/fixture test; miss path exits 2 with both probed paths named. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S25.7 Index milestone row updated: "Hello world — Sprint 25 — compiled + assembled by afs-as + linked + runs" checked with the CI job link. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).

## Open audit findings against this phase

None. At remediation baseline `89b68ead`, [the Sprint 61 burndown](burndown.md) records 0 Critical / 0 High / 0 Medium / 0 Low findings, and the audit lifecycle reports 55 PASS / 0 XFAIL / 0 XPASS / 0 FAIL.

## Verdict

Every numbered phase DoD item is evidenced and no audit findings remain.

READY
