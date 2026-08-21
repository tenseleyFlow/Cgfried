# Closeout: Phase 10 — ARM64 Backend

Date:            2026-08-20
Baseline commit: `89b68ead`
Reviewer:        Independent Sprint 61 closeout review

## DoD items (from the phase's sprint files, every numbered item)

- [x] S47.1 `src/cg/arm64/{mir.h,isel.c,peep.c}` land; `cg/shared.c` split documented; zero `#ifdef __aarch64__` outside `target.c`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S47.2 Logical-imm unit test: 5334/5334 encodable patterns match afs-as; 0 false-encodes. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S47.3 MOV synthesis ≤ optimal inst count on the #3 table; 1M-value round-trip clean. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S47.4 40+ golden `ASM_CHECK(arm64)` fixtures pass; all fragments assemble under afs-as AND GNU as with byte-identical text bytes. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S47.5 Reg-31 fixture set (8 rows) green; `tbz` never emitted out of range under the generated-function stressor. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S47.6 UB-division corpus lint wired into the runner and failing on a planted fixture. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S48.1 `src/cg/arm64/regalloc.c` + AAPCS64 classifier land; shared.c untouched except documented extension points; x86 tests still green (shared-skeleton regression gate). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S48.2 Classifier unit suite: 60/60 types, all 6 HFA rows exact. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S48.3 20-signature mixed-link differential vs gcc passes both directions under qemu. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S48.4 Varargs fixture set (≥12) green at all opt levels; five-field va_list layout byte-compared against gcc's via a generated `_Static_assert` header. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S48.5 Allocator never assigns x18 (corpus-wide grep of emitted asm asserts 0 hits), never holds x16/x17 across calls, saves only d-halves of v8–v15. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S48.6 Hardware SP-alignment: full corpus run under qemu with no SIGBUS; verifier asserts frame%16==0 on every function. --- — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S49.1 Upstream afs-as PR merged + submodule bumped: arm64 ELF objects with the #1 reloc set; differential-clean vs GNU as on the full corpus (0 byte diffs, 0 reloc diffs). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S49.2 Hello world and the full e2e corpus run on arm64-linux natively in CI (`ubuntu-24.04-arm` lane green) and under `scripts/qemu-run.sh` locally. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S49.3 f128: all four arith ops + 11 compare/convert symbols land in libcgf_rt; Sprint-15 torture corpus passes at runtime; gcc differential 0 mismatches. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S49.4 char-sign lane: 15+ fixtures, divergent-by-design, green on BOTH arches. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S49.5 NEON lane green; fmla contract fixtures prove gnu17-on / c17-off policy. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S49.6 Atomics hammer green natively; every ll/sc site carries the UPGRADE marker (grep-enforced in the test suite). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S49.7 CI matrix shows: x86_64-linux native, arm64-linux native, arm64-linux qemu-cross — all green with exact expected-skip counts asserted. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S50.1 Hello world + full e2e corpus on nomad-1 and macos-15 CI, all opt levels, both link lanes (system ld, afs-ld `CGF_LD=1`). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S50.2 All 7 divergence-table fixtures green; rows 1–3 pass mixed-link vs clang in both directions. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S50.3 afs-as-vs-system-as reloc differential clean on the corpus (otool compare, 0 diffs modulo documented ADDEND-vs-embedded representation differences). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S50.4 Every produced binary passes `codesign --verify`; afs-ld-signed binaries execute on nomad-1 with signing done from a LINUX host (cross-sign proof, even if the binary is only run in Sprint 51's cross story). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S50.5 TLV fixtures green; `_Thread_local` on arm64-macos fully supported (IE/LE-equivalent semantics via descriptors). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S50.6 `-static` on arm64-macos hard-errors with the documented message; SDK discovery works with no Xcode env vars set and honors both overrides. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S51.1 All 5 targets compile from both host arches; determinism test: byte-identical objects per target, enforced in CI. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S51.2 musl lane: full corpus static-linked via afs-ld, Alpine CI green. freebsd lane: smoke in PR CI, full corpus nightly VM lane green. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S51.3 PIC: corpus subset green `-fPIC -pie` on the three linux/freebsd targets; no TEXTREL; `-shared`-via-system-ld smoke green; unsupported tiers hard-error with roadmap messages. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S51.4 TLS IE/LE green both arches all opt levels; GD hard-errors with fixture proof. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S51.5 ABI harness: 500-signature PR smoke 0 divergences both directions on x86_64-linux-gnu + arm64-linux; first 10k nightly completed on the fleet across all 5 targets with results archived; planted-bug self-test catches + minimizes. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S51.6 DWARF spot-check: 100 PCs per target agree with the reference toolchain. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S51.7 Index milestone "All targets" flipped: five-target closed set, cross-compile any→any, documented sysroot honesty table in the man page. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).

## Open audit findings against this phase

None. At remediation baseline `89b68ead`, [the Sprint 61 burndown](burndown.md) records 0 Critical / 0 High / 0 Medium / 0 Low findings, and the audit lifecycle reports 55 PASS / 0 XFAIL / 0 XPASS / 0 FAIL.

## Verdict

Every numbered phase DoD item is evidenced and no audit findings remain.

READY
