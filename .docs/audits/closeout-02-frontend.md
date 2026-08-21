# Closeout: Phase 02 — Frontend

Date:            2026-08-20
Baseline commit: `89b68ead`
Reviewer:        Independent Sprint 61 closeout review

## DoD items (from the phase's sprint files, every numbered item)

- [x] S8.1 All 6 `-std` modes select the documented keyword sets (table-driven test, 0 failures). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S8.2 Integer ladder: 24-row matrix (6 suffix classes × dec/hex × 2 boundary values) green; `2147483648`→long and `0x80000000`→uint both asserted. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S8.3 Phase-6 concat: 25-combination prefix matrix behaves per Deliverable 5. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S8.4 Zero calls to `strtod`/`strtol`-family outside `numlit.c`; `lex_fp_interim` is the only FP conversion, XD-S08-FPHOST filed. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S8.5 `--dump-tokens` golden files for ≥15 fixtures; all diagnostics carry valid spans (checked by harness). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S8.6 `make test` green; no token path reads host `sizeof`/endianness. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S9.1 Declarator torture suite (≥20 cases) round-trips through `--dump-ast` exactly. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S9.2 All five typedef-ambiguity pitfalls behave as specified, with fixtures. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S9.3 6.7.2 multiset matrix test: every valid multiset accepted, every invalid one rejected with a targeted message (not "syntax error"). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S9.4 K&R definitions parse and dump correctly; identifier-list-in-declaration rejected. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S9.5 Differential accept/reject agreement ≥ 98% on the fixture corpus vs gcc 8 (disagreements triaged or filed). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S9.6 Every AST node verified to carry an in-bounds Span (harness assertion); zero silent stubs — all deferrals hard-error naming their sprint. --- — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S10.1 Precedence matrix suite green (≥40 asserted parenthesizations, all 17 levels covered, associativity proven for `?:` and `=`). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S10.2 All Deliverable-2 disambiguation fixtures behave exactly as specified, both typedef states. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S10.3 Duff's device + a 3-deep nested-label switch parse and dump correctly. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S10.4 `_Generic` parses with unevaluated flag set; assoc-list constraints that are parse-time (≥2 `default`) diagnosed. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S10.5 Differential accept/reject agreement ≥ 98% on corpus; every disagreement has a ledger entry. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S10.6 Zero silent stubs: every deferred construct hard-errors naming its sprint (grep-audited list in the PR description). --- — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S11.1 u32-cascade fixture: 5 uses of an unknown type ⇒ exactly 1 diagnostic, at file scope and block scope variants. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S11.2 `parse_sync` progress proven by unit test; bracket-nesting limit enforced at 256 with a clean error. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S11.3 `-fmax-errors` implemented with alias `-ferror-limit=`; cap message exact; exit-code contract holds on all ERROR_EXPECTED fixtures (harness-verified). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S11.4 Poison contract documented in `parse.h` comments and enforced: suppressed-diagnostic counter asserted in ≥3 fixtures. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S11.5 100k-iteration fixed-seed fuzz run green under ASan+UBSan in CI; all four invariants (crash/hang/diag-or-parse/span-bounds) mechanically checked. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S11.6 ≥20 golden diagnostic fixtures; zero XFAILs without ledger IDs; zero stubs. --- — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).

## Open audit findings against this phase

None. At remediation baseline `89b68ead`, [the Sprint 61 burndown](burndown.md) records 0 Critical / 0 High / 0 Medium / 0 Low findings, and the audit lifecycle reports 55 PASS / 0 XFAIL / 0 XPASS / 0 FAIL.

## Verdict

Every numbered phase DoD item is evidenced and no audit findings remain.

READY
