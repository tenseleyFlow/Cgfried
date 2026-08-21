# Closeout: Phase 09 — Memory Safety

Date:            2026-08-20
Baseline commit: `89b68ead`
Reviewer:        Independent Sprint 61 closeout review

## DoD items (from the phase's sprint files, every numbered item)

- [x] S41.1 `doc/memsafe.md` committed with the six-layer table, per-layer non-guarantees, and the gcc/-fanalyzer/ASan comparison table. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S41.2 `AllocSite` + offset-range API live in `src/opt/alias.c`; zero points-to logic under `src/memsafe/`; Sprint 32 suite green. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S41.3 State lattice + transition table implemented; 25/25 join-table unit asserts pass. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S41.4 `MsTrace` implemented; dump fixtures show correct event chains for alloc/free/escape on ≥ 10 fixtures. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S41.5 Path-splitting worklist honors all three caps; torture fixture compiles in < 2 s and reports degraded-to-may. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S41.6 `CGF_MEMSAFE_DUMP=1` output deterministic; CI lane added running the foundation fixture directory. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S42.1 All seven default-tier checks implemented, each with ≥ 3 firing and ≥ 3 MUST-NOT-fire fixtures passing (42+ fixtures minimum). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S42.2 `-Wmem` default-on; `-Wno-mem`, per-check flags, `-Werror=mem`, and pragma push/pop all honored (Sprint 37 machinery, fixture-verified). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S42.3 Realloc state-forking catches the use-old-after-success case; all four realloc fixtures pass. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S42.4 musl-sweep CI lane green: zero diagnostics at default `-Wmem` over all musl TUs, < 90 s. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S42.5 Every diagnostic renders a full `MsTrace`; trace fixtures pin ≥ 10 exact note-sequences. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S42.6 `-Wmem-strict` tier exists and holds ≥ 1 demoted heuristic (pass-to-unknown UAF) with its own fixtures. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S42.7 `doc/memsafe.md` updated: per-check sections, FP budget law, demotion procedure. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S43.1 `MsSummary` computed bottom-up for every function; recursion and function pointers conservatively topped; dump visible under `CGF_MEMSAFE_DUMP=1`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S43.2 Leak/UAF/double-free checks consult summaries; ≥ 12 interprocedural fixture pairs pass. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S43.3 All five `cgf_*` attributes parse, validate arity, override inference; unknown `cgf_` attribute hard-errors (fixture-pinned). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S43.4 `<cgfried/memsafe.h>` installed; gcc-compilation CI check green. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S43.5 `-Wmem-annotation-mismatch` fires on every enforceable direction of the five-attribute vocabulary and is silent on stronger-than-inferred `cgf_takes_ownership`. The earlier "all five violated" wording was internally contradictory: failing to consume a takes-ownership argument is explicitly a stronger contract, not a body-side violation. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S43.6 Builtin summary table covers ≥ 40 libc functions including the FILE\* family; FILE\* leak/double-close fixtures pass. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S43.7 musl-sweep still zero-FP with summaries enabled. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S44.1 Header/canary/quarantine implemented in `src/rt/`; all seven firing e2e fixtures abort with pinned diagnostics at `-O0` and `-O2`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S44.2 All MUST-NOT-abort twins pass, including the foreign-pointer and mixed-TU fixtures (the §4 law proven). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S44.3 Discharge statistics show > 0 discharged checks on the annotated fixture set (the static-buys-runtime claim demonstrated numerically). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S44.4 `--wrap` table complete for the nine-function family; static and dynamic link fixtures green. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S44.5 Abort path is stdio-free; diagnostic text deterministic (invariant 3); `CGF_SAFE_ABORT=trap` honored. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S44.6 Micro-bench ratios recorded in-repo; < 2.5×/< 2× on the micro set. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S44.7 `doc/memsafe.md` L3 section updated: quarantine bounds, probabilistic caveat, boundary table, mixed-link story. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S45.1 Parseable-fixit emission byte-compatible with gcc's format; ≥ 12 format fixtures pass. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S45.2 `=all` writes `.cgf-fixed`, never touches the original (fixture-proven); `interactive` TTY-gated; conflict rule enforced. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S45.3 All four transform families ship with firing + MUST-NOT-fire pairs; `strncpy` provably never suggested. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S45.4 `-ftrivial-auto-var-init=zero|pattern` implemented; layering fixture (warn fires + binary reads zero) passes; parity-matrix deviation row committed. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S45.5 `-Wmem-suggest-annotations` emits must-fact fixits only; the round-trip test (apply then mismatch-silent) passes. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S45.6 Workflow example (annotate-then-ratchet) written into `doc/memsafe.md`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S45.7 musl-sweep green; opt-level equivalence corpus green with `=zero` on. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S46.1 `doc/safe-mode.md` committed: guarantees-with-limits table, rejection table with alternatives, mixed-program rules, threat-model section, roadmap; doc-lint green. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S46.2 All six rejections hard-error with alternative-naming messages; 6 rejection + 6 acceptance fixtures pass. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S46.3 uintptr_t whitelist grammar implemented; 16 grammar fixtures pass. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S46.4 `-fsafe` composition asserted by driver fixture; ELF note emitted and link-checked; mixed-link fixtures pass. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S46.5 `make safe-dogfood` green in CI: cgf compiles cgf under `-fsafe`; safe-built cgf passes smoke tests. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S46.6 `ci/safe-mode-allowlist.txt` exists with owner + justification per entry; ratchet script fails on growth (fixture-proven), passes on shrink. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S46.7 The claim is live and provable in one command: a C compiler that proves its own memory safety — `make safe-dogfood`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).

## Open audit findings against this phase

None. At remediation baseline `89b68ead`, [the Sprint 61 burndown](burndown.md) records 0 Critical / 0 High / 0 Medium / 0 Low findings, and the audit lifecycle reports 55 PASS / 0 XFAIL / 0 XPASS / 0 FAIL.

## Verdict

Every numbered phase DoD item is evidenced and no audit findings remain.

READY
