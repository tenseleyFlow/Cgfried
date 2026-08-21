# Closeout: Phase 12 — Campaigns

Date:            2026-08-20
Baseline commit: `89b68ead`
Reviewer:        Independent Sprint 61 closeout review

## Release XFAIL bar

- [x] Zero Critical-tagged XFAIL IDs and total open project XFAIL IDs ≤ **N = 0**. The actual post-triage release ledger contains zero open runner XFAIL IDs; the closed historical `XD-S08-FPHOST` row is not a runner fixture ID. — EVIDENCE: [post-triage XFAIL ledger](xfail-debt.md); repository scan `rg '^// XFAIL\\(' tests` finds only the runner meta-suite's isolated fake-ledger fixtures.

## DoD items (from the phase's sprint files, every numbered item)

- [x] S55.1 Tier table exists; every row has ≥1 fixture; CI greps for tier/fixture sync. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S55.2 musl b306b16a `arch/x86_64/syscall_arch.h` + `src/internal/` headers compile clean under `cgf -std=gnu17` (compile-only smoke; full musl is S57). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S55.3 Extended asm: 100% of `tests/gnu/asm/` pass at O0–O3/Os on x86_64 and arm64; early-clobber execute-fixture passes at all levels. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S55.4 All 16 implemented attributes have semantics tests; packed differential vs gcc-8 shows 0 layout mismatches over ≥200 generated structs. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S55.5 `-std=gnu17` defines `__GNUC__=8`; `-std=c17` does not; both fixtured. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S55.6 Deferred constructs hard-error with sprint-naming messages (fixtured). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S55.7 Zero new warnings compiling cgf itself; bootstrap-relevant lanes green. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S56.1 **PASS** — `make torture-import` produces `tests/torture/` + `tests/ctestsuite/` with MANIFESTs; re-run is a no-op; sha256 gate green. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S56.2 **PASS** — Full matrix runs end-to-end on x86_64 (all 5 levels) and arm64 (nightly config exists); zero harness crashes, every cell classified. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S56.3 **PASS** — `.docs/audits/torture-triage.md` exists with 100% of failures bucketed and dispositioned (pre-triaged classes counted; no "misc" bucket >5% of total). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S56.4 **PASS** — `tests/torture/passing.txt` committed; ratchet gate live in CI and proven bidirectional by the self-test. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S56.5 **PASS** — Every `xfail:` disposition has a TORT-NNN ledger entry (the Sprint 56 baseline mints none, so coverage is vacuous and explicit). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S56.6 **PASS** — Baseline numbers recorded in the triage doc (pass counts per suite per level per target) — the phase-end ≥95%-of-applicable-execute bar is stated there as the target the fix-sprints burn toward (number renegotiable at triage; the METHOD is this sprint's deliverable). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S57.1 musl: 100% of non-`src/complex` TUs (report exact N of M) compile and link into `libc.a` + crt objects; static hello + Sprint 53 bench suite run musl-static; CAMP-MUSL-001 ledgered. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S57.2 libc-test parity: zero tests failing under cgf-musl that pass under gcc-musl (both failure sets archived in campaign artifacts). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S57.3 chibicc: builds; `make test` + `test/driver.sh` 100% pass. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S57.4 tinycc: configure+build with cgf as CC; `tests2` + `testspp` at their `.expected` bars; every configure-probe deviation filed as a finding. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S57.5 qbe: builds; `make check` 100%; `check-arm64` green on the arm64 runner. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S57.6 All four descriptors + `.expected` files committed; expected-vs-actual gate proven bidirectional; FINDINGS.md has zero unresolved critical entries. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S57.7 musl-twice determinism diff: byte-identical objects. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S58.1 `make bootstrap-O0` and `make bootstrap-O2` green on x86_64-linux: stage1 ≡ stage2 byte-identical, every intermediate object identical, zero normalization applied. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S58.2 Same on arm64-linux native (nightly lane configured and green). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S58.3 Cross-bootstrap ritual green once: x86-hosted arm64 stage2 ≡ native arm64 stage2, byte-identical. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S58.4 `bisect-nondet.sh` + `audit-determinism.sh` in CI; all seeded-fault playbook tests pass (correct TU + phase named for ≥3 injected suspects). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S58.5 Bootstrap lanes wired as required checks on every PR (x86_64) with the nightly/weekly cadence documented in `ci/bootstrap.yml`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S58.6 Stage1 self-compile time reporting into the Sprint 54 dashboard. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [ ] S58.7 MILESTONE gate armed: soak log started; Phase 13 sign-off requires 30 consecutive green days on all configured lanes — stability, not luck. — VERDICT: blocking — the required 30-consecutive-day all-lane soak is recorded at 2/30 on 2026-08-20.
- [x] S59.1 `ci/campaigns/FORMAT.md` committed; all S57+S59 campaigns conform (8 descriptors total lint-clean under `scripts/campaign-lint.sh`). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S59.2 zlib: configure+build+`make test` 100% on x86_64 and arm64; musl-static variant builds and self-tests. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S59.3 lua: `make generic` builds; `all.lua` 0 failures at O0 and O2 on both arches; musl-static lua passes `all.lua`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S59.4 sqlite: amalgamation compiles at all 5 levels; shell smoke diff-clean vs gcc build; speedtest1 completes; S52 baselines committed for sqlite3.c. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S59.5 curl: configure completes with zero driver ICEs, build succeeds, `src/curl --version` correct; probe-deviation findings all filed. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S59.6 Nightly ladder lane live; one full nightly cycle green; expected-file drift gate proven bidirectional. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S59.7 Phase-exit bar recorded: 4/4 rungs green at their defined bars; FINDINGS.md empty of unresolved criticals. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).

## Open audit findings against this phase

None. At remediation baseline `89b68ead`, [the Sprint 61 burndown](burndown.md) records 0 Critical / 0 High / 0 Medium / 0 Low findings, and the audit lifecycle reports 55 PASS / 0 XFAIL / 0 XPASS / 0 FAIL.

## Verdict

The XFAIL release bar is satisfied, but Sprint 58's required 30-day all-lane soak remains at 2/30.

NOT READY
