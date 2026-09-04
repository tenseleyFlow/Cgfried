# HANDOFF — read this before touching anything

You are picking up **Cgfried**, a from-scratch C17 compiler.

**WHERE THINGS STAND (2026-09-04): Sprints 0–57, 59, and 60 are CLOSED;
Sprints 59–60 closed out of order, so the contiguous ratchet remains 57.
Sprint 61 implementation and review are complete with an honest NOT READY
closeout. Phases 1–11 are CLOSED.**
Sprints 55–57 were completed out of numerical order while Sprint 54 collected
its controlled fleet soak; the current deterministic release report, closure
audit, and contiguous ratchet through Sprint 57 now close that gap. Sprint 58's
implementation, deterministic per-pass phase-dump playbook, and first complete
hosted native/cross activation are green; its 30-day bootstrap soak is RUNNING
at 10/30 after a required-lane reset on August 18 and remains operationally
OPEN. Matching-head August 28 x86/ARM runs
[`33168831416`](https://github.com/tenseleyFlow/Cgfried/actions/runs/33168831416)
and
[`33184794811`](https://github.com/tenseleyFlow/Cgfried/actions/runs/33184794811)
are green at `5277c7fc`. Scheduled Sunday full-lattice run
[`32617645741`](https://github.com/tenseleyFlow/Cgfried/actions/runs/32617645741)
is green at exact head `b54fda67`, and matching-head x86/ARM runs
[`32919964767`](https://github.com/tenseleyFlow/Cgfried/actions/runs/32919964767)
and
[`32928733136`](https://github.com/tenseleyFlow/Cgfried/actions/runs/32928733136)
are green at `69113c47` on August 26. Matching-head x86/ARM runs
[`33056638768`](https://github.com/tenseleyFlow/Cgfried/actions/runs/33056638768)
and
[`33080889159`](https://github.com/tenseleyFlow/Cgfried/actions/runs/33080889159)
are green at `6ef24ca2` on August 27; retained O0/O2 manifests record all four
fixed points plus both ARM O2 ABI differentials as successful. Supplemental
full-lattice manual run
[`32603828216`](https://github.com/tenseleyFlow/Cgfried/actions/runs/32603828216)
is green at exact head `8fb99082`; because it is a second August 22
observation, it neither advances the distinct-date count nor replaces the
scheduled Sunday weekly gate. Sprint 59's controlled Kasumi/Hasu
SQLite baselines and qualifying 15-variant nightly are independently audited
and complete. The
performance-gate lattice, native CI measurements, fleet runtime protocol,
reporting, policy checks, scheduler integration, and controlled-power model
are implemented. Kasumi, Hasu, and Nomad each have accepted controlled Sprint
54 evidence on three distinct UTC dates. Sprint 55's GNU tier table is **37
implemented / 6 parsed-ignored / 8 refused** on `trunk` after PR #47. PR #48's
extension-type-name repair is merged as `1d6da267`; it does not change that
tier count. PR #49's generic-qualified-association repair and target-complete
ratchet are merged as `d8ccfc04`. PR #50's non-defining GNU `extern void`
linker-symbol repair is merged as `e2439e79`, raising the `trunk` PASS ratchet
to 26,715 and its GNU tier table to 38 implemented / 6 parsed-ignored / 8
refused. PR #51's hosted GNU plain `alloca(...)` spelling is merged as
`d7d59fa`, raising the GNU tier table to 39 / 6 / 8 and the PASS ratchet to
26,725. PR #52's compound-literal array-completion repair is merged as
`cfaec8d`, raising the GNU tier table to 40 / 6 / 8 and the target-complete
PASS ratchet to 26,735. The `s56.5-torture-failure-decomposition` tranche is
merged through PR #53 as `008cf61`; it kept that PASS set unchanged while
replacing one host-diagnostic runtime-abort bucket with 15 testcase-stable
buckets. PR #54's `s56.5-gnu-always-inline` tranche is merged as `fc6ebe9`; it
raises the GNU tier table to 41 / 6 / 8 and the target-complete PASS ratchet to
26,745. PR #55's `s56.5-void-deref-and-pointer-composite` tranche is merged as
`91d282e`; it implements WG14 DR106 void-pointer dereference semantics and
enum-compatible-integer pointer composition, raising the PASS ratchet to
26,765. PR #56's GNU aggregate self-cast tranche is merged as `0295bd50`, and
PR #57's GNU scalar-to-union tranche is merged as `9b5d04d1`, raising the
target-complete PASS ratchet to 26,795 with 32 live repair rows representing
28 compiler tranches. PR #59's iterative symbol-finalization prerequisite is
merged as `ecc5cd36`; it removes the host-stack dependency exposed by the
100,000-declaration torture limits without changing the ratchet. PR #58's
constant-expression decomposition and const-scalar-object folding tranche is
merged as `ea708db7`; its target-complete publication raises the ratchet to
26,805 with 66 buckets, 57 applied decisions, zero stale/unresolved decisions,
and the same 32 live repair rows. PR #60's runtime VLA `__builtin_offsetof`
tranche is merged as `b56b4f5`; the committed ratchet contains 26,818 PASS
cells. PR #61's immediate-asm timing tranche is merged as `ff3b9d66`, raising
the ratchet to 26,830 PASS cells. PR #62's label-reachable-dead-regions
tranche is merged as `5cf4bbf`, raising the target-complete PASS ratchet to
26,840. PR #63's wide-character-preprocessor-constant tranche is merged as
`a8a23acd`, raising the target-complete PASS ratchet to 26,850. PR #64's
composite-array-bound tranche is merged as `1b8ec6ae`, raising the
target-complete PASS ratchet to 26,860. PR #65's unprototyped-call IR tranche
is merged as `3db23ef`, raising the target-complete PASS ratchet to 26,870.
PR #66's x86 immediate-materialization tranche is merged as `7b79b9ad`,
raising the target-complete ratchet to 26,875 lines. PR #67's ARM64 stacked
large-aggregate ABI tranche is merged as `01d95ccc`, raising the
target-complete ratchet to 26,880 lines (26,877 PASS keys). PR #68's x87 tied
double-operand-shape tranche is merged as `a80346d5`, raising the
target-complete ratchet to 26,885 lines (26,882 PASS keys) with 48 applied
policy decisions. PR #69's `s56.5-dead-code-link-elimination` tranche is
merged as `fe3fcf59`, raising the target-complete ratchet to 26,887 lines
(26,884 PASS keys). PR #70's `s56.5-asm-rmw-single-evaluation` tranche is
merged as `29d3c3d0`, raising the target-complete ratchet to 26,897 lines
(26,894 PASS keys). PR #71's `s56.5-vla-typedef-size` tranche is merged as
`c8dd9f18`, raising the target-complete ratchet to 26,917 lines (26,914 PASS
keys). PR #72's `s56.5-builtin-llabs-semantics` tranche is merged as
`2380c739`, raising the ratchet to 26,927 lines (26,924 PASS keys). PR #73's
`s56.5-aligned-typedef-object-layout` tranche is merged as `eb456acd`, raising
the target-complete ratchet to 26,937 lines (26,934 PASS keys). PR #74's
`s56.5-vla-va-arg` tranche is merged as `99b00740`, raising the target-complete
ratchet to 26,957 lines (26,954 PASS keys). The current
`s56.5-bitfield-integer-promotions` tranche is merged through PR #75 as
`698726f4`, raising the target-complete ratchet to 26,977 lines (26,974 PASS
keys). PR #76's `s56.5-bitfield-expression-precision` tranche is merged as
`363aa2db`, raising the target-complete ratchet to 27,017 lines (27,014 PASS
keys). The current `s56.5-huge-object-layout` tranche on PR #77 is
target-complete at 27,028 ratchet lines (27,025 PASS keys), with 33 applied
policy decisions, one retained stale cross-host variant, and zero unresolved
decisions; it awaits fresh post-publication standard and exact native ARM64
CI. Its documented GNU tier table remains 42 implemented / 6 parsed-ignored /
8 refused. Sprint 56's
campaign machine and triage map remain complete while Sprint 58 continues its
independent soak.
Sprint 57's pinned compile-the-world campaigns, truthful
staged-musl linkage proof, host baselines, exact gates, and campaign-driven
compiler repairs are integrated on `trunk`. Sprint 59's exact campaign
machine, compiler repairs, numeric scale policy, and closure evidence are
integrated while Sprint 58 continues collecting operational evidence. The old
D5 notes are retained only as implementation history. Sprint 60's defensive
audit is COMPLETE against full-CI-green baseline
`1c639e060ab38bf3daf9a4e2f2a431c9ca3041cb`: 12 fronts, 50 raw / 50
deduplicated findings (C12/H20/M16/L2), and 50 durable expected-failure
reproducers. Sprint 61 remediated all seeded and discovered debt, completed
its independent review, and now waits only on Sprint 58's independent soak.

**Known-wrong-but-SHIPPING is ZERO** — every open item on `trunk` is a named
refusal or a deliberate deferral.

On trunk, continue **Sprint 58's 30-day bootstrap soak**. Record only hosted
runs that satisfy the machine-readable daily/weekly lane contract in
`.docs/audits/bootstrap-soak.md`; a missing or red required run resets the
streak. No Sprint 61 remediation remains. Do not claim Sprint 58 closed, mark
Phases 12–13 READY, or begin the Sprint 62 release landing until the soak
reaches 30/30.

Sprint 55 came out of numerical order because campaign sprints 56–59 consume
it, 28 deferrals pointed at it, and it blocks HOSTED compilation on macOS and
FreeBSD. Confirmed empirically: extended asm and `__volatile` together took
musl from **716 to 1259 of 1361** translation units parsing.

---

## Sprint 60 defensive audit — COMPLETE; CLOSED OUT OF ORDER

Baseline `1c639e060ab38bf3daf9a4e2f2a431c9ca3041cb` is frozen for the audit.
Its required x86 bootstrap [run
31865512724](https://github.com/tenseleyFlow/Cgfried/actions/runs/31865512724)
passed O0/O2, and full standard CI [run
31865512754](https://github.com/tenseleyFlow/Cgfried/actions/runs/31865512754)
completed with all 20 executed jobs green and one expected skip. All twelve
fresh-context, reproducer-first fronts are closed. The required F04 -> F05 ->
F09 dependency chain was respected, F12 ran last, and an independent
cross-front review found no root-cause aliases.

- `.docs/audits/audit-00.md` records 50 raw / 50 deduplicated findings:
  12 Critical, 20 High, 16 Medium, and 2 Low. Eleven unverified observations
  are segregated and excluded.
- `tests/audit-regressions/manifest.tsv` and the front reports map exactly in
  both directions. `scripts/check-audit-fixtures.sh build/cgfried` reports
  50 XFAIL, 0 XPASS, and 0 FAIL.
- The complete normal regression suite is green at 713 unit tests,
  4,292,653 assertions, and 639 program fixtures. Bootstrap, all
  differentials, campaign/meta gates, audit aliases, format, and ban checks
  are green. `make BUILD=build-san test-san` also completed successfully,
  including all 50 expected audit failures, cross-target/ABI/runtime lanes,
  and all three sanitizer fuzz smokes.
- The frozen-baseline-to-closeout diff under `src/`, `runtime/`, and `include/`
  is empty: Sprint 60 landed only reproducers, audit tooling, and ledger
  evidence. Remediation begins in Sprint 61.

Sprint 61 has closed all Critical findings and all 20 seeded High findings.
The `IR-H-07` arithmetic review added and closed `IR-H-09`. Broad validation
after the seeded High tier then exposed `IR-H-12`, a valid-C SysV aggregate
stack-marker verifier ICE introduced by the `IR-C-10` repair; `IR-H-12` is
closed at `92c77528`, zero High findings remain, and Medium/Low remediation is
active. `DRV-M-01` then closed at `a91a8c42`; the test-integrity cluster closed
at `141ffcad`; the preprocessor/frontend diagnostic cluster closed at
`385f8aa8`; the backend Medium cluster closed at `fb641618`; `PP-L-03` closed
at `addf16b7`; `IR-L-02` closed at `7a4b9cb6`; the memory-safety Medium
cluster closed at `687c92a7`; and the determinism/performance-evidence cluster
closed at `89b68ead`, reducing Sprint 61 audit debt to zero.
Sprint 58 remains at 10/30; Sprint 60's
out-of-order closure does not advance the contiguous closure ratchet or permit
Phase 13/release sign-off.

---

## Sprint 61 remediation and review — COMPLETE; final verdict NOT READY

The phase-boundary preflight is complete. Commit `4b36ba37` promotes the
aggregate/front ledgers, burndown, and future closeout files to tracked
artifacts while leaving unrelated local audit material ignored. Commit
`b2e1ac18` gives every audit fixture an explicit `OPEN`/`PASS` lifecycle:
OPEN defects remain XFAIL and repair-before-ledger is XPASS/red; PASS repairs
stay green and any regression is FAIL/red. The fail-closed lifecycle meta-test
is part of `make test-audit-fixtures`; the starting inventory is 0 PASS / 50
XFAIL / 0 XPASS / 0 FAIL.

The initial burndown was 12 Critical / 20 High / 16 Medium / 2 Low. The first
cluster hunt added `SEMA-C-08`, an AAPCS64 unnamed-nonzero-bitfield aggregate
alignment mismatch, at `555fc32a`. `SEMA-C-01` closed at `0eca9832` and
`SEMA-C-02` closed at `d4d674ec`, and `IR-C-03` closed at `892435be`, so live
debt is 0/20/16/2 after `X64-C-02` closed at `45a9d0dc`, `MS-C-01` closed at
`7a003d68`, and `SEMA-C-08` closed at `bdd523c0`, followed by the `IR-C-11`
atomic-alignment discovery at `c7d43927` during the `X64-C-01` sibling hunt,
`IR-C-04` closed at `1b61459a`, `IR-C-01` closed at `eb528221`, and `IR-C-11`
closed at `eb364980`, `X64-C-01` closed at `45f0282c`, and `MS-C-04` closed at
`b1bc4f91`, and `IR-C-09` closed at `3bc1ae02`. The `MS-C-05` design review
then added `MS-C-06`, the unregistered `asprintf`/`vasprintf` allocator-model
mismatch. `IR-C-10` closed at `ea41dd88`, `MS-C-06` closed at `88211779`, and
`MS-C-05` closed at `b287c2ef`; the Critical barrier is clear. `SEMA-H-07`
then closed at `21acc9fc`, followed by `RT-H-01` at `d3be586f`, `OPT-H-04`
at `f6ffc1d1`, `A64-H-01` at `2b4ab767`, `IR-H-06` at `1fc6bdb6`, and
`OPT-H-02` at `5b030fc5`, so live debt is 0/14/16/2 and High remediation is
active. The `IR-H-07` arithmetic review then recorded `IR-H-09` at `81ca7347`,
raising live debt to 0/15/16/2 until that parser-length defect is remediated.
`IR-H-07` then closed at `ed607727`, returning live debt to 0/14/16/2.
`OPT-H-01` closed at `264bb13d`, reducing live debt to 0/13/16/2.
`A64-H-02` and `A64-H-03` closed together at `2cd05946`, reducing live debt to
0/11/16/2.
`SEMA-H-03` closed at `1da90129`, reducing live debt to 0/10/16/2.
`SEMA-H-05` closed at `a2e158c9`, reducing live debt to 0/9/16/2.
`SEMA-H-04` closed at `40d84413`, reducing live debt to 0/8/16/2.
`SEMA-H-06` closed at `52eeaa91`, reducing live debt to 0/7/16/2.
`IR-H-09` closed at `7f3774f6`, reducing live debt to 0/6/16/2.
`IR-H-08` closed at `088c9934`, reducing live debt to 0/5/16/2.
`FE-H-01` and `FE-H-02` closed together at `b1c29124`, reducing live debt to
0/3/16/2.
`OPT-H-03` closed at `43b0de28`, reducing live debt to 0/2/16/2.
`PP-H-01` closed at `54cf3e67`, reducing live debt to 0/1/16/2.
`IR-H-05` closed at `54435c86`, reducing live debt to 0/0/16/2 and clearing
the High tier.
Broad validation then recorded `IR-H-12` at `074c83d9`, raising live debt to
0/1/16/2 until the caller/callee SysV aggregate stack-marker contract is
repaired.
`IR-H-12` closed at `92c77528`, restoring live debt to 0/0/16/2 and clearing
the High tier again.
`DRV-M-01` closed at `a91a8c42`, reducing live debt to 0/0/15/2. A clean
detached build, all 55 audit lifecycle checks, and the assembler exit/signal
matrix are green at that exact code commit.
`TI-M-01`, `TI-M-02`, and `TI-M-03` closed together at `141ffcad`, reducing
live debt to 0/0/12/2. Exact available/missing-tool skip profiles, the focused
c-testsuite debt contract, the real 220-file differential, and a clean
detached 41 PASS / 14 XFAIL / 0 XPASS / 0 FAIL audit run are green.
`PP-M-02`, `PP-M-04`, `FE-M-03`, `FE-M-04`, and `FE-M-05` closed together at
`385f8aa8`, reducing live debt to 0/0/7/2. Exact macro-note anchors and order,
adversarial recovery cases, 74+74 preprocessor differential comparisons, and
a clean detached 46 PASS / 9 XFAIL / 0 XPASS / 0 FAIL audit run are green.
`X64-M-03` and `A64-M-04` closed together at `fb641618`, reducing live debt to
0/0/5/2. Multi-digit inline-assembly execution, decoded ARM64 frame rows,
peephole-boundary survival, and a clean detached 48 PASS / 7 XFAIL / 0 XPASS /
0 FAIL audit run are green.
`PP-L-03` closed at `addf16b7`, reducing live debt to 0/0/5/1. Exact
300-frame unit coverage, the 272-frame unique-name audit fixture, preserved
capped presentation, and a clean detached 49 PASS / 6 XFAIL / 0 XPASS / 0
FAIL audit run are green.
`IR-L-02` closed at `7a4b9cb6`, reducing live debt to 0/0/5/0. Integer,
floating, and forward-reference mismatch coverage, verified textual round
trips, 5,000 IR-fuzzer iterations, and a clean detached 50 PASS / 5 XFAIL / 0
XPASS / 0 FAIL audit run are green.
`MS-M-02` and `MS-M-03` closed together at `687c92a7`, reducing live debt to
0/0/3/0. Path-local MUST origins now make standard-stream equality pruning
sound across selects, non-pointer overwrites, joins, and origin-capacity loss;
standard-stream `freopen` stays escaped while owned-stream controls stay
locally owned. The 67-test memory-safety unit lane, 92/92 warning corpus, 12
exact trace sequences, musl meta-gate, and a clean detached 52 PASS / 3 XFAIL
/ 0 XPASS / 0 FAIL audit run are green.
`DET-M-01`, `DET-M-02`, and `DET-M-03` closed together at `89b68ead`, reducing
live debt to 0/0/0/0. Blocking compile, musl, and stage1 gates now consume
paired wall/CPU/RSS dispersion and use the larger of their nominal allowance
and four MADs; stage1 takes three forced-build samples; legacy evidence cannot
silently become a gate; and every baseline-changing commit is checked against
its exact old/new metric union and rationale. `make -j2 test-bench`, `make -j2
test-bootstrap`, and a clean detached 55 PASS / 0 XFAIL / 0 XPASS / 0 FAIL
audit run are green at that exact code commit.
The full 55-finding lifecycle is therefore 55 PASS / 0 XFAIL / 0 XPASS / 0
FAIL with live debt 0/0/0/0. All 15 Critical fixes retain cluster-hunt records.
The post-triage release XFAIL bar is N=0 and is satisfied. Seed 61 selected six
of 55 remediated findings (10.9%); an independent fresh-detached-worktree
review rebuilt every exact resolving commit, reran the full audit lifecycle,
and read each fixing diff against its root cause. All 6/6 passed, so no sample
doubling or front-wide escalation fired.

Fourteen phase closeout documents now cover all 429 numbered DoD items from
Sprints 0–61. The structural/meta closeout gate is green. The production
Sprint 62 entry gate exits red for exactly two honest verdicts: Phase 12 is
NOT READY because Sprint 58 is 6/30, and Phase 13 inherits that dependency.
Phases 00–11 are READY. This is the sole remaining release-entry blocker.
The gate consumes tracked `ci/closeout-dod.tsv` rather than the ignored local
roadmap, so a fresh clone proves the exact 62-sprint / 429-item inventory.

Final closeout validation completed with `make -j2 test` green: 794 unit
tests / 4,293,663 assertions, 693 program fixtures, 104 permanent corpus
fixtures, 55/55 audit regressions PASS, all compiler differentials and fuzz
smokes green, and the expected missing-tool/sysroot skips ledgered exactly.
That run found and permanently repaired two stale test-policy ratchets:
`s36_isa_driver.sh` now expects 104 corpus files / 624 optimization-level
objects after `X64-M-03`, and `check_posix_sh.sh` now distinguishes valid POSIX
awk functions embedded in shell scripts from shell-only syntax. Both have
focused meta-tests in ordinary `make test`.
One central integrator owns `manifest.tsv`, the tracked front ledgers, and
`burndown.md` so
each finding's fixture-state flip, strikethrough, and count update travel with
its fix. The completed Critical lanes were split to avoid shared-file
conflicts:

- `SEMA-C-01` — RESOLVED at `0eca9832`; its cluster hunt also repaired the
  sibling multiplication overflow path.
- `SEMA-C-02` — RESOLVED at `d4d674ec`; its sibling-hunt discovery
  `SEMA-C-08` is also RESOLVED at `bdd523c0`.
- `IR-C-01` — RESOLVED at `eb528221`; pure 16-byte `long double` aggregates
  return through `st0`, with zero-width struct bitfields excluded from the
  wire classification.
- `IR-C-03` — RESOLVED at `892435be`; atomic pointer updates now use one
  scaled seq_cst RMW. This unlocks the `X64-C-01` backend repair.
- `IR-C-04` — RESOLVED at `1b61459a`; declaration-granular VLA checkpoints
  restore the stack across backward gotos, including same-scope and `for`-init
  cases.
- `IR-C-11` — RESOLVED at `eb364980`; target-neutral verification now rejects
  under-aligned scalar seq_cst accesses before either backend can rely on an
  invalid indivisibility guarantee.
- `IR-C-09` — RESOLVED at `3bc1ae02`; Linux AAPCS64 composite arguments and
  `va_arg` honor the even-register rule, Apple remains unchanged, and the
  shared marker is verifier-hardened.
- `IR-C-10` — RESOLVED at `ea41dd88`; Linux and Apple retain 16-byte source
  alignment after composite flattening, caller/callee stack walks agree, and
  Linux's adjacent bare-`_Float128` slot rule is pinned on both sides.
- `X64-C-02` — RESOLVED at `45a9d0dc`; frame sizes and far offsets are
  represented and emitted without 32-bit truncation.
- `X64-C-01` — RESOLVED at `45f0282c`; floating seq_cst accesses retain their
  atomic contract, wide helpers pull in `libatomic`, and content-aware `-###`
  remains free of filesystem and external-tool side effects.
- `A64-H-01` — RESOLVED at `2b4ab767`; tentative TLS definitions bypass ELF
  COMMON and retain `.tbss`, TLS symbol typing, and TLSLE relocations, while
  ordinary COMMON and the effective Mach-O zero-fill form remain unchanged.
- `A64-H-02` / `A64-H-03` — RESOLVED at `2cd05946`; the shared post-relocation
  address materializer retains immediate fast paths, uses reserved non-aliasing
  scratch registers for arbitrary signed addends, and keeps TLS/GOT relocation
  forms plus branch-relaxation size accounting correct.
- `IR-H-06` — RESOLVED at `1fc6bdb6`; lowering and verification share the
  exact returns-twice symbol set after asm-spelling normalization, including
  glibc's `__sigsetjmp` expansion while excluding near names.
- `IR-H-07` — RESOLVED at `ed607727`; relocation bounds are checked with
  ordered subtraction, so exact fits remain valid and wrapped or short
  eight-byte relocation ranges are rejected before backend consumption.
- `IR-H-08` — RESOLVED at `088c9934`; textual IR distinguishes symbolic
  indirect callees from direct calls and preserves that form through
  optimizer-created parser/printer structural round trips.
- `MS-C-01` — RESOLVED at `7a003d68`; the required `-fsafe` diagnostic floor
  is immutable.
- `MS-C-04` — RESOLVED at `b1bc4f91`; statically proven null accesses and
  indirect calls are unweakenable safe-mode errors, with path-aware zero-byte
  exemptions that do not suppress unrelated pointer checks.
- `MS-C-06` — RESOLVED at `88211779`; compatible external
  `asprintf`/`vasprintf` identities are rejected because their returned storage
  cannot enter the safe-runtime registry, while static, incompatible, and
  renamed non-allocator identities remain valid.
- `MS-C-05` — RESOLVED at `b287c2ef`; safe heap pointer derivations retain
  origin, index, scale, direction, and supported `uintptr_t` provenance until
  runtime validation, while analysis treats the guards as no-capture and
  no-dereference. Atomic pointer arithmetic is an explicit `-fsafe` refusal.
- `SEMA-H-07` — RESOLVED at `21acc9fc`; `_Generic` constant evaluation follows
  only sema's selected association in the caller's constant-expression mode,
  including nested selections and every integer-constant-expression context.
- `SEMA-H-03` — RESOLVED at `1da90129`; old-style declarations and definitions
  compare their promoted signatures symmetrically while K&R declaration-list
  scope, dependent arrays, counts, and GCC-compatible warning modes remain
  intact.
- `RT-H-01` — RESOLVED at `d3be586f`; `__SIZEOF_LONG_DOUBLE__` now comes from
  the canonical target layout, preserving Apple arm64's 8-byte binary64 model
  while every other current target remains 16 bytes.
- `OPT-H-04` — RESOLVED at `f6ffc1d1`; equal alias hulls no longer prove
  identity or pathwise store coverage unless the operand is identical or both
  locations are independently exact. GVN/DSE and the 25-cell oracle pin the
  soundness boundary.
- `OPT-H-02` — RESOLVED at `5b030fc5`; fusion dependence strides now include
  the signed induction step, so descending and non-unit loops use execution
  order while overflow and extreme steps conservatively block transformation.
- `OPT-H-01` — RESOLVED at `264bb13d`; alias points-to progress and offset
  growth are separated, with a finite precision window followed by monotone
  widening that preserves sound MAY results and converges on recursive updates.

Every Critical closure requires its own sibling-hunt record. A newly found
Critical or High reopens the barrier. The final ledger is 0/0/0/0, all Sprint
61 closeout verification and ledger finalization are complete, and the blunt
verdict remains NOT READY only until Sprint 58 reaches 30/30.

---

## Parallel Sprint 59 project ladder — COMPLETE; CLOSED OUT OF ORDER

Sprint 59's code, descriptors, exact-result contracts, compiler repairs,
designated-host SQLite baselines, and qualifying post-baseline nightly are
complete on `trunk`. Sprint 58 remains open, so this closure does not advance
the contiguous ratchet beyond 57.

- `ci/campaigns/FORMAT.md`, `ladder.yml`, and the retrofitted Sprint 57
  descriptors define one audited eight-project inventory. The linter sees
  eight descriptors, ten expected files, and one ladder; seeded missing,
  extra, reordered, duplicate, malformed, and changed rows all fail.
- The recurring matrix has exactly 15 variants across x86-64, native ARM64,
  and the two x86-64 musl-static lanes. Publication validates every artifact's
  exact rows and generates deterministic aggregate and failure reports.
- Native x86 zlib passes all ten exact rows, including all four upstream
  self-tests and the GCC output differential. Lua passes all nine native rows
  at O0 and O2 and reaches `final OK !!!` in the full 5.4.7 `all.lua`; the
  Cgfried-built musl-static stack passes its four-row full-suite bar. Curl
  passes its ten-row configure/build/test-support/version contract with zero
  driver ICEs and an exact five-row probe-deviation ledger.
- SQLite compiles the 3.46.1 amalgamation and shell at O0/O1/O2/O3/Os. Its
  32-statement shell smoke is byte-identical to the GCC lane at O0 and O2,
  and `speedtest1` completes with verification
  `111130:1e792c9db61996c477b8ab5ce2d690052e8dae74824a430a`. The canonical
  Sprint 52 scale profile records 10 measured runs after one warmup and keeps
  the absolute O2 ceiling at 60 seconds. Exact revision `e52b8891` produced
  controlled passing baselines at `2026-08-13T230920Z`: Kasumi O0
  2,094.713636 ms / 1,248,164 KiB and O2 18,203.856393 ms / 11,490,048 KiB;
  Hasu O0 2,406.116677 ms / 1,228,200 KiB and O2 17,733.074318 ms /
  11,450,836 KiB. The complete immutable artifacts live under
  `.benchmarks/runs/2026-08-13T230920Z-{kasumi,hasu}-sqlite/`.
- Eight stable compiler findings are fixed and regression-pinned:
  deterministic source-span interning; bounded alias/dependence/IPO/BCE
  scale; call-argument provenance through IPO/BCE; dynamic x86 block-edge
  copies beyond 32 values; GNU `__PRETTY_FUNCTION__`; static nested-member
  address folding; ABI provenance through loop clone/unswitch/unroll; and
  final materialization of out-of-range ARM64 frame addresses. The last repair
  makes Curl's 37.8 KiB spill frame compile and cross-assemble correctly.
- The TinyCC warning ratchet legitimately advances from 14 parsed / 16
  deferred to 15 / 15: `x86_64-gen.c` was rejected only because glibc
  `assert` expands to `__PRETTY_FUNCTION__`. It now has zero Cgfried-only
  format warnings. The permanent fixtures now pin the frontend-fuzz sequence
  to `ca30f3f7c77c82be`.
- Fresh closure validation passes the complete repository suite: 713 unit
  tests, 4,292,653 assertions, all 639 program fixtures, every
  corpus/differential/optimizer/runtime/cross/policy gate, and clang-format 22.
  A 2,000-iteration frontend-fuzz smoke completed with zero findings; the crash
  ledger is clean.

### Sprint 59 closure evidence

1. **COMPLETE:** exact revision `e52b8891444ff883d8dfc6ae5202e9410e039cae`
   is pushed; its required hosted x86-64/native ARM64 activation and O0/O2
   bootstrap jobs are green (CI `31752532760`, bootstrap `31752532771`).
2. **COMPLETE:** `scripts/fleet-sqlite.sh` captured one warmup plus ten O0/O2
   measurements on both controlled designated hosts at that exact revision.
   Both immutable artifacts classify fleet-control-v2 as controlled, pass the
   absolute gate, preserve the original policy byte-for-byte, and have been
   independently checked against their raw samples.
3. **COMPLETE:** all eight numeric medians/max-RSS values are committed in
   `ci/campaigns/sqlite-baselines.conf`. The +30% wall, +20% RSS, and 60-second
   absolute O2 thresholds are unchanged; partial/unmeasured policy fixtures
   remain fail-closed, and the public `compile.baseline-policy` row is
   invariant.
4. **COMPLETE:** qualifying nightly
   [`31761472470`](https://github.com/tenseleyFlow/Cgfried/actions/runs/31761472470)
   passed all 15 jobs at exact head
   `f8aed0f5aa16331af4dbaa600212b4bf25df2583`. Its artifact inventory is the
   exact 28-item contract: 13 campaign-result envelopes, 14 detailed evidence
   artifacts, and `campaign-ledger-evidence`. The reporter recorded
   `campaign-publish-reports: PASS captured=15 matched=15 published=0`; all 15
   metadata envelopes are exact `source=campaign` / `state=match`, all checks
   pass in both directions, all producer/aggregate pairs are byte-identical,
   and `failures/manifest.tsv` records zero failures. The downloaded aggregate
   ZIP matches API SHA-256
   `dcbeb83c2ac991a6df9baf15eaa1ac6cfde991ad585463bf6c1983bf850700d4`.

`ci/closed_sprints.txt` is 57. Sprint 59 is closed out of order; its evidence
does not manufacture Sprint 58 bootstrap-soak days or advance the contiguous
ratchet while Sprint 58 remains open.

---

## Parallel Sprint 58 self-host campaign — IMPLEMENTED; SOAK RUNNING (10/30)

Sprint 58's compiler, runtime, deterministic bootstrap/playbook machinery, and
hosted CI definitions are integrated. The first hosted streak reached 5/30
through August 17, then reset on August 18 when the required x86 O0 job was
cancelled before bootstrap and retained no artifact. August 19–28 are days
1–10 of the current streak; the ledger still needs 20 consecutive dates, so
do not call the sprint closed until that operational obligation is complete.

- `make bootstrap-O0` and `make bootstrap-O2` perform raw stage1/stage2
  comparisons over all 113 compiler/runtime assembly files, all 113 objects,
  `libcgf_rt.a`, and `cgfried`; no normalization is permitted. Both local x86
  fixed points pass. `make bootstrap-repro-O2` also passes across `-j8` versus
  `-j1` and distinct output roots.
- The bootstrap compiles the runtime with Cgfried. The safe allocator lock is
  strict C11 `_Atomic`, int128 helpers use the two-`u64` ABI without host
  `__int128`, and the binary128 boundary uses `_Float128` when self-hosted.
  The int128 ABI/value differential passes under GCC, Clang, Cgfried O0, and
  Cgfried O2.
- O2 bootstrap feasibility repairs are deliberately bounded: inline analysis
  caches call/fact data and caps oversized callers, GVN/DSE cap deterministic
  memory work, and LICM retains canonicalization/verification while declining
  motion in oversized loop functions. Inliner growth state persists across a
  top-level fixed point so `20020506-1.c` converges instead of repeatedly
  consuming a reset budget. Unit coverage pins every cutoff.
- The first O2 fixed point found a real historical nondeterminism bug:
  `GvnOperand` equality compared padding bytes. Keys are now zero-initialized
  and compared field-by-field. The static audit rejects this whole-object
  `memcmp` pattern and carries a seeded regression alongside the existing
  readdir, padded-write, and pointer-output faults.
- `CGF_DUMP_IR=all` emits an exclusive-create ordered tree spanning parse,
  sema, lowering, every optimizer invocation with deterministic
  sequence/fixpoint indices, legalization, MIR, and assembly.
  `scripts/bisect-nondet.sh` fails closed on absent groups and identifies the
  first differing TU and phase boundary. `make test-bootstrap` exercises the
  real phase tree, collision refusal, exact localization, and three injected
  fault families.
- `.github/workflows/bootstrap.yml` defines required x86 O0/O2 checks, nightly
  native ARM64 O0/O2, and weekly x86 reproducibility plus cross-host ARM64.
  Cross-host evidence authenticates both hosted run manifests against the
  workflow commit and retains both bootstrap reports, consumed stage
  manifests, and the exact ARM header archive in the final 90-day artifact.
- The controlled Kasumi/Hasu O2 stage1 timing receipt is wired into the Sprint
  54 dashboard under the existing fleet-control-v2 protocol. It is a trial
  gate and does not manufacture a baseline or Sprint 54 evidence.
- ARM flag selection now treats calls, opaque asm, and atomic CAS as NZCV
  clobbers, with table-driven regressions requiring a fresh comparison after
  each. Conditional branches use compact encodings only when range is proved;
  otherwise the emitter uses a local inverse branch plus an unconditional
  long edge. The former 505 KiB hosted assembly failure is pinned.
- x86 selection has one authoritative pre-append MIR effect table and
  materializes a pending compare before arithmetic, calls, stack-allocation
  expansions, atomic XADD/CMPXCHG, or inline asm with a `cc` clobber. Atomic
  AND/OR/XOR retry loops materialize before their synthetic block transition;
  an impossible cross-block EFLAGS provenance now ICEs. Direct-clobber and
  retry-loop tables pin the invariant. This repaired the `loop-3.c` O2/O3/Os
  miscompile and the related O3 `20000815-1.c` failure.
- Full hosted run
  `https://github.com/tenseleyFlow/Cgfried/actions/runs/31665602629` is green
  for x86 O0/O2, native ARM64 O0/O2, x86 `-j8` versus `-j1`
  reproducibility, raw 113-file cross-host ARM64 assembly identity, and
  same-toolchain objects, runtime archive, and compiler identity. The final
  90-day artifact is `sprint58-bootstrap-arm64-cross-final`; all manifests
  bind commit `c6bf3cf6a91f50dbd561afd9c1cecd19f8a72f83` with
  `normalization=none`. A fresh local download independently reverified all
  seven embedded provenance hashes, 113/113 assemblies, 113/113 objects, the
  runtime archive, and the compiler. The exact x86 required contexts also
  passed in push run `31665586963` and are configured on `trunk`.
- Full hosted run
  [`31762814206`](https://github.com/tenseleyFlow/Cgfried/actions/runs/31762814206)
  supplies the second distinct UTC date at exact head
  `e75ff34c6b03214281ed637c7bbcb38228e76496`. All seven jobs passed and the
  exact eight-artifact set is retained. A fresh audit matched every raw ZIP to
  its GitHub API SHA-256, verified all eight fixed-point stage manifests and
  seven embedded cross-provenance hashes, and byte-compared all 113 raw ARM64
  assembly files plus the complete 228-file same-toolchain final payload with
  `normalization=none`.
- Matching-head x86 push run
  [`31857187648`](https://github.com/tenseleyFlow/Cgfried/actions/runs/31857187648)
  and scheduled native ARM64 run
  [`31863013882`](https://github.com/tenseleyFlow/Cgfried/actions/runs/31863013882)
  supply the third consecutive UTC date at exact head
  `d9693498ef236d5088c45e3e7adb120593808eed`. All four applicable O0/O2 jobs
  passed. Their exact four artifacts match the GitHub API's retained sizes and
  raw SHA-256 digests; 1,356 run-manifest hashes, eight 228-entry stage
  manifests, and 912 stage1/stage2 payload comparisons verified with zero
  differences. ARM O2's int128 and binary128 ABI/value differentials passed.
  August 15 was Saturday, so the weekly cross-host and reproducibility lanes
  were correctly not due. Independent review approved the paired evidence.
- Matching-source hosted torture streams atomically regenerated the ratchet
  and triage report. At that hosted checkpoint the totals were 25,933 PASS,
  6,620 SKIP, and 8,097 classified failures across 40,650 cells; 90 buckets
  and 81 live policy decisions, with zero stale/unresolved decisions. Reversed
  stream order produced
  both committed outputs byte-identically. Final candidate run `31665586870`
  then passed the exact x86 ratchet plus the complete standard CI matrix,
  including ARM-QEMU/native, sanitizers, the 100k frontend fuzz lane,
  musl/QBE campaigns, toolchain, format, and policy gates. Hosted x86 run
  `31686587082` subsequently promoted 15 additional PASS cells with zero
  regressions; its retained matrix regenerates the 25,933-cell ratchet
  byte-identically.
- `.docs/audits/bootstrap-soak.md` is **RUNNING at 10/30**. The first streak
  started on August 13, included the complete Sunday activation on August 16,
  and reached 5/30 on August 17. It reset on August 18 at `9ec43d92`: x86 run
  [`32089117040`](https://github.com/tenseleyFlow/Cgfried/actions/runs/32089117040)
  cancelled O0 during system-toolchain installation, skipped bootstrap,
  failed the evidence-manifest step, and retained no O0 artifact. ARM run
  [`32097369403`](https://github.com/tenseleyFlow/Cgfried/actions/runs/32097369403)
  passed both lanes but cannot cure the missing daily x86 lane. Matching-head
  x86/ARM pairs on August 19 (`af914c89`, runs `32205833254`/`32214058949`),
  August 20 (`6460c3c2`, runs `32321924998`/`32330135984`), August 21
  (`db6f114a`, runs `32437193020`/`32445410094`), August 22 (`9cdc3e87`,
  runs `32544159147`/`32550348530`), full Sunday activation on August 23
  (`b54fda67`, run `32617645741`), and matching-head x86/ARM evidence on
  August 24 (`83f846e8`, runs `32686915858`/`32688703871`), August 25
  (`65cd4928`, runs `32798572708`/`32807232448`), and August 26
  (`69113c47`, runs `32919964767`/`32928733136`), August 27
  (`6ef24ca2`, runs `33056638768`/`33080889159`), and August 28
  (`5277c7fc`, runs `33168831416`/`33184794811`) are current days 1–10.
  Continue recording distinct UTC dates and every due weekly
  cross/reproducibility result; any missing or red required run breaks the
  streak. Supplemental manual run
  [`32603828216`](https://github.com/tenseleyFlow/Cgfried/actions/runs/32603828216)
  passed all seven jobs and retained all eight artifacts at exact head
  `8fb99082`. A fresh download verified all seven embedded provenance hashes,
  both 113-entry assembly manifests, and all 228 corresponding payload files
  byte-for-byte. It is additional current-head evidence, not a fifth UTC date
  or a substitute for the scheduled Sunday run. The August 16–22 scheduled
  ZIP/internal hashes have not yet been independently recomputed, so do not
  overstate that separate evidence. Fresh August 23–26 downloads verified all
  1,356 daily run-manifest hashes per date, all eight stage manifests per date,
  and all 912 fixed-point payload comparisons per date. August 23's final
  cross artifact also verified 7/7 embedded provenance hashes, 226/226
  assembly-manifest hashes, and 228/228 paired final payloads with
  `normalization=none`. For August 27–28, all four retained daily artifacts
  per date have successful GitHub job/workflow conclusions at their exact
  matching heads, and their API-reported ZIP digests are recorded in the
  ledger; the separate full payload-rehash audit has not been repeated for
  those dates.

`ci/closed_sprints.txt` is 57. Sprint 58 remains operationally open during its
soak and therefore owns the next contiguous closure step.

---

## Parallel Sprint 57 compile-the-world campaign — COMPLETE

Integrated worktree and branch:
`/home/mfwolffe/GithubOrgs/tenseleyFlow/Cgfried` on `trunk`.

- All inputs are immutable and verified before use: musl
  `b306b16a`, libc-test `12343315`, Chibicc `90d1f7f1`, TinyCC `38059770`,
  and QBE `d62b154d`. Campaigns export pristine archives below
  `build/campaigns/`; no reference checkout is built in place.
- The musl hybrid builds route 1,254 non-complex C translation units through
  Cgfried, 68 explicitly deferred `_Complex` units through GCC, and 32
  assembler inputs through the host toolchain. Two clean builds reproduce all
  1,354 routed objects byte-for-byte. Static hello, all 21 Sprint 53 kernels,
  and libc-test parity against a separately GCC-built musl pass with zero
  Cgfried-only failures. Both libc-test lanes use the pinned musl GCC specs;
  retained link maps prove the exact staged `Scrt1.o` and sole staged
  `libc.a`, while ELF checks reject `PT_INTERP` and `DT_NEEDED`. Both lanes
  retain the same 29 upstream/environment failure records.
  `CAMP-MUSL-001` (`_Complex`) and `CAMP-MUSL-002` (shared/TLS) are the only
  published scope exclusions.
- Chibicc passes 41/41 programs in both the Cgfried and pristine host-GCC
  lanes. TinyCC passes 132/132 `tests2` and 24/24 `testspp` cases in both
  lanes. Native x86-64 QBE passes 32/32 cases in both lanes. On real ARM64,
  both independently built QBE lanes pass the same 28 cases and share four
  pinned-upstream exclusions: declared-skip `dark.ssa`, an unselected
  non-entry-block `alloc8` in `dynalloc.ssa`, and two vararg tests whose emitter
  violates AAPCS64 stack alignment. The exact gate requires zero Cgfried-only
  failures; QEMU is not used because it masks the two hardware SIGBUS faults.
- `ci/campaigns/FINDINGS.md` records 37 fixed compiler defects and eleven fixed
  campaign-integrity defects, each with a minimized regression or exact gate.
  Notable repairs include distinct `__func__` objects, aggregate override
  semantics, global definition emission after forward relocations, undefined
  weak/hidden ELF attributes, local aggregate template relocations, optimizer
  provenance preservation, inline-asm operand locality, soft-float carry, and
  absolute numeric-pointer member/index initializer folding.
- Exact bidirectional results contain 13 musl rows, seven Chibicc rows, ten
  TinyCC rows, seven x86 QBE rows, and eight ARM64 QBE rows. The expected-gate
  meta-test rejects missing, extra, reordered, duplicate, malformed, or
  changed rows.
- PR CI runs musl and native x86 QBE. The scheduled/manual workflow runs all
  four campaigns and QBE on real x86-64 and ARM64 runners, retaining configs,
  routes, logs, failure sets, and exact result artifacts.
- Fresh local validation is green: 695 unit tests / 4,284,201 assertions,
  full GCC and Clang strict builds, the complete repository suite, ShellCheck,
  campaign expected-gate meta-tests, and all four locally runnable campaign
  gates. Expected local skips remain only optional-tool/reference lanes and
  match their committed ledgers.
- The last matching-source-provenance x86-64 and real-ARM64 torture baseline
  preserved every committed PASS cell and added 985. A final stable-source
  x86-64 gate after the x87 special-value repair added five more x86-64 PASS
  cells whose ARM64 counterparts were already present: the Sprint 57 close
  ratchet contained exactly 25,910 PASS cells. Sprint 58's matching-source
  refresh above supersedes those totals. Reversed input order regenerated both
  Sprint 57 published artifacts byte-identically.

No Sprint 57 implementation work remains. Sprint 54's real three-date evidence
has closed the gap, so `ci/closed_sprints.txt` now includes Sprint 57.

---

## 0. Sprint 56 parallel campaign worktree — COMPLETE

Integrated campaign implementation:
`/home/mfwolffe/GithubOrgs/tenseleyFlow/Cgfried` on `trunk`.
The latest integrated Sprint 56.5 range-designator tranche landed through PR
#43 at merge commit `6c1cd12b`. The GNU `#ident` tranche is integrated through
PR #44 at merge commit `d52e44d1` after exact-head target-complete publication
and green post-publication CI.

- Imported byte-pristine gcc c-torture and c-testsuite corpora contain 2,016
  compile, 1,752 execute, 78 IEEE, and 219 c-testsuite cases.  Both import
  verification and deterministic fixture suites pass.
- Full O0/O1/O2/O3/Os matrices completed for `x86_64-linux-gnu` and
  `arm64-linux`: 20,325 cells per target, 40,650 total. After the Sprint 58
  bootstrap repairs, Sprint 61 remediation, and the Sprint 56.5 declarator,
  packed-bitfield, enum-bitfield, enum-integer-mode, static-subobject
  pointer-difference, VLA-semantics, aggregate-initializer,
  flexible-array-initializer, nested-flexible-array-member,
  old-style-designator, range-designator, and GNU-ident tranches, outcome totals
  are 26,605 PASS, 6,620 SKIP, and 7,425 classified failures.
- The v2 streams share source/compiler/harness/manifest provenance.  The final
  harness hash is
  `b6e50c45f810d83e0b9e5b5adcc722f8ec2a5e2afdc98a0611386507b01a07b5`.
  Volatile GNU-ld identifiers and section offsets are normalized; 451 linker
  failures per target collapse into four semantic fingerprints.
- `.docs/audits/torture-triage.md` has 100% bucket coverage: 65 total buckets,
  56 durable overlay decisions, zero stale, zero unresolved, and no misc
  bucket. The overlay contains 32 `fix-sprint:s56.5-*`, 18 `out-of-scope`,
  and six `wontfix-0.1.0` decisions. No TORT XFAIL was minted.
- `tests/torture/passing.txt` is the exact sorted 26,605-cell PASS set. The
  `e941403d` refresh promoted 103 x86-64 and 98 arm64 cells with zero
  regressions; `3eb97e5c` then promoted all ten `pr43188.c` cells and retired
  the declarator-type-attributes bucket, again with zero regressions.
  `e227d4f1` implemented suffix attributes on bitfields plus packed-bitfield
  layout/lowering and promoted 110 cells (11 cases across both targets and all
  five optimization levels) with zero regressions. `7fa70484` then recorded
  enum value-range signedness on bit-field members without changing Cgfried's
  general enum compatible-type policy. It promoted 30 cells with zero
  regressions: `ctestsuite/00218.c` and GCC torture cases `20000914-1.c` and
  `20030714-1.c`, on both targets at all five levels. The current
  `s56.5-enum-integer-mode` tranche implements QI/HI/SI/DI representations on
  enum definitions and attributed enum views, including exact promotion,
  layout, effective-type metadata, and ABI extension behavior. It promoted 40
  cells with zero regressions: `20040901-1.c`, `20050107-1.c`,
  `20050119-1.c`, and `20050119-2.c`, on both targets at all five levels. The
  arm64 stream uses the repository's cross binutils, QEMU, and
  `/usr/aarch64-linux-gnu/include`; an earlier host-header stream was
  quarantined and never published. A bare-QEMU local stream that omitted the
  loader sysroot was likewise rejected by the regression check and replaced
  with a qualifying `scripts/qemu-run.sh` stream before ledger publication.
  The current `s56.5-static-subobject-pointer-difference` tranche then retired
  three buckets and promoted 100 cells with zero PASS regressions: ten GCC
  torture sources (`20001116-1.c`, `920928-1.c`, `920928-6.c`, `930326-1.c`,
  `builtin_constant_p.c`, `pr19449.c`, `pr50565-1.c`, `pr50565-2.c`,
  `pr64067.c`, and `builtin-nan-1.c`), on both targets at all five optimization
  levels. The generic `adf974...` diagnostic bucket proved to contain four
  distinct constant-expression gaps, so the repair deliberately covers
  same-object subobject pointer differences, `__builtin_constant_p`, static
  file-scope compound-literal member addresses, and static infinity/NaN
  initializers. Pointer differences between distinct objects remain rejected
  by a focused negative regression.
  The `s56.5-vla-semantics` tranche then retired three more buckets
  and promoted 30 cells with zero PASS regressions: `20040317-1.c`,
  `20071108-1.c`, and `970217-1.c`, on both targets at all five optimization
  levels. Parameter array bounds now evaluate once on function entry after
  incoming values are bound; their expression trees are rebound from the
  parser's prototype-scope symbols to the definition-scope parameters.
  Lowering preserves the unadjusted parameter declaration type separately
  from the pointer-adjusted body type, and runtime `sizeof` recursively
  multiplies fixed outer dimensions around inner VLA dimensions. The local
  ARM evidence used a runtime-backed driver with shipped headers, the ARM
  sysroot include, cross binutils/CRT, and QEMU loader all explicit; streams
  missing any of those routes were quarantined before publication.
  The current `s56.5-aggregate-initializers` tranche makes incomplete-array
  completion follow the initializer current-object cursor instead of counting
  raw scalar syntax items. Static, automatic, designated, aggregate-valued,
  and compound-literal regressions are green; `ctestsuite/00205.c` is
  byte-identical to GCC at O0/O1/O2/O3/Os on native x86 and compiles for
  arm64-linux at all five levels. Exact implementation-head PR CI
  [run 33023706534](https://github.com/tenseleyFlow/Cgfried/actions/runs/33023706534)
  passed every non-torture job, including the repaired 100k-iteration fuzz
  lane, and refused only the five uncommitted x86 PASS cells. Exact-head native
  full-lattice
  [run 33023617265](https://github.com/tenseleyFlow/Cgfried/actions/runs/33023617265)
  passed every non-ARM-torture job and refused only the matching five native
  ARM64 PASS cells at `9df93a59708a6730681fb7cb3ef0711e539d872d`.
  The downloaded ARM stream SHA-256 is
  `22ed4efcd90418652b26ce239aa5c3d4f779e507cde3f42ccc2cbea82cd91d15`;
  the hosted PR merge stream, used only as confirmation, is
  `1cd5021429c7109bbde5adca435719220acbd251c5177cd94319b6f7c2389fdd`.
  `make torture-baseline` then regenerated the exact implementation-head x86
  stream locally as
  `10a5a5c654748cdffb5fae98433c763c785941e74775aafe36bb1701b3536344`
  and atomically combined it with the native ARM stream. Their common
  compiler-source SHA-256 is
  `9575bde4df3a3430f2261c85f77ad312602958fac88d810670b91a5b9676c8fe`;
  the shared harness and both manifest hashes also match. The publication
  promoted exactly the ten `00205.c` cells with no PASS regression, retired
  the resolved `s56.5-flattened-aggregate-initializers` policy row, and
  reconciled the four previously unresolved report buckets to durable policy
  decisions. The resulting overlay is 62 applied / 0 stale / 0 unresolved.
  Reversing the two input streams regenerates both outputs byte-identically;
  the committed PASS and triage SHA-256 values are respectively
  `dea222492d826b7bbe70dc49e62f80e3ec7358d0d22a0d715ecbd02f58e12ca5`
  and
  `f6fa98b3a4ab16137e0ccdccafd510623600ba2382f4642edcee11b06bce5f82`.
  PR #30 subsequently passed its post-promotion PR/native reruns and merged.
  PR fuzz seed `38877` also exposed a prototype-first K&R definition whose
  declaration-list pointer parameter was unrelated to the prototype's scalar
  parameter. Sema now rejects that constraint violation before the composite
  prototype ABI reaches lowering; the classic short/float default-promotion
  mismatches retain their existing traditional-warning policy.
  Combined gating passes, and reversed input order regenerates both committed
  artifacts byte-identically.
  The current `s56.5-flexible-array-initializer` tranche implements GNU
  initialization of a flexible array member for file-scope and block-scope
  static objects. The semantic record type stays unchanged, so `sizeof` keeps
  its declared value, while symbol-level storage metadata enlarges only the
  emitted definition by `sizeof(record) + initialized payload`; this includes
  GCC's counterintuitive tail-padding case. Positional, string, sparse
  designated, and local-static regressions pass; automatic-storage use is
  rejected and `-pedantic` retains the extension warning. The complete
  historical bucket comprised four GCC torture cases:
  `compile/pr45919.c`, `compile/pr98407.c`, `execute/20010924-1.c`, and
  `execute/pr33382.c`. All four now pass at O0/O1/O2/O3/Os on both targets.
  Pre-promotion PR CI
  [run 33035847559](https://github.com/tenseleyFlow/Cgfried/actions/runs/33035847559)
  passed every non-torture job, including the repaired ordinary test lane,
  sanitizers, 100k frontend fuzzing, QEMU, toolchain, macOS/ARM, and campaign
  jobs; its only failure was the expected 20 uncommitted x86 PASS cells. The
  hosted merge stream SHA-256 is
  `91414cd9bc163422e07ac3e1ab46c5b6d14f35716729730360a02428496ceb52`.
  Exact-head native full-lattice
  [run 33035855464](https://github.com/tenseleyFlow/Cgfried/actions/runs/33035855464)
  passed every non-ARM-torture job and refused only the matching 20 native
  ARM64 PASS cells at `b10c4f71765b354672222b0171575d5425920354`; its stream
  SHA-256 is
  `6a3a4a88aae7b6624ea9347f8ebff1bf9aaa062a568397e5161ae5487f46c2cc`.
  GitHub's PR checkout records synthetic merge revision
  `91f613ca1788777116b347cab135af18865497e6`, so that hosted x86 stream is
  confirmation-only. `make torture-baseline` regenerated the publishable
  same-head x86 stream locally as
  `10f59a4ed0d14e68b570a73fe68a6479a690e0bc32691609f04c617e1eee1f1f`
  and atomically combined it with native ARM. The streams share compiler-source
  SHA-256
  `d2b327606f3ae8be7b09160125b4e9247d39eb3dd2148d83082fb4ee2d192f79`
  plus identical harness and manifest hashes; hosted and local x86
  classification rows are byte-identical.
  The publication promoted exactly those 40 cells with zero PASS regression
  and retired fingerprint `d9666dc4...`. It also removed the stale duplicate
  x86 timeout fingerprint `090a233b...`: its 15 cells now normalize under the
  existing `c0cda523...` timeout class alongside the same ten ARM cells, with
  no behavioral movement. The resulting overlay is 60 applied / 0 stale / 0
  unresolved and has 7,515 classified failures. Reversed input order
  regenerates both outputs byte-identically; the PASS and triage SHA-256 values
  are respectively
  `7fba03a00166ffda46f1935ce8294b645962d5dbcd8f5daaf14f41703a1b7852`
  and
  `99c51451a7fab2230b6f167e4d3e6ebb53d623f139a4ae32920bed353066b229`.
  PR #40 subsequently passed its post-promotion PR/native reruns and merged.
- The current `s56.5-nested-flexible-array-members` tranche separates a
  record's direct final FAM from recursive FAM containment. ISO-valid union
  containment propagates silently; GNU struct-member and array-element uses
  retain the record's fixed `sizeof` and warn under `-pedantic`. Initializing
  a buried flexible tail remains a hard error, matching GCC: the containment
  extension does not invent storage inside an enclosing object. Unit coverage
  pins direct/recursive flags, union and array propagation, direct static-FAM
  preservation, and braced/flat/designated nested-initializer rejection. The
  permanent program fixtures pin the PR16566 non-lvalue expressions, the
  pedantic diagnostic, and the nested-initializer safety boundary.
  Pre-promotion PR CI
  [run 33044896781](https://github.com/tenseleyFlow/Cgfried/actions/runs/33044896781)
  refused only the ten uncommitted x86 PASS cells; its hosted stream SHA-256 is
  `e0a0571894fcc500092810fd20fd2db2eeabe6028cd7da689073a7aa74046e91`.
  Exact-head native full-lattice
  [run 33044919989](https://github.com/tenseleyFlow/Cgfried/actions/runs/33044919989)
  passed every non-ARM-torture job and refused only the matching ten native
  ARM64 PASS cells at `384cf82aed68bb842f892a5ab569987c5c3d9550`; its stream
  SHA-256 is
  `5b180bf498db0d1307ccd8021b15e2d4ab214e5a09969526a845ad911f797b42`.
  GitHub's PR checkout records synthetic merge revision
  `752ae66e9544db5c4bf4716c0231bc6cbfddb0d8`, so that hosted x86 stream is
  confirmation-only. The publishable same-head x86 stream was regenerated
  locally as
  `f625ec23653e3006d10bb571c9f1549385be25b85283a5f8880bc18a8a90fcad`
  and atomically combined with native ARM. The streams share compiler-source
  SHA-256
  `7371392480559c37a122ed342d1eb7de092fb1fe0646ad991cfd33a9841895a3`
  plus identical harness and manifest hashes; hosted and local x86
  classification rows are byte-identical. The publication promoted exactly
  the 20 `pr16566-{1,3}.c` cells with zero PASS regression and retired
  fingerprint `eb35ff3a...`. The resulting overlay is 59 applied / 0 stale /
  0 unresolved and has 7,495 classified failures. Reversed input order
  regenerates both outputs byte-identically; the PASS and triage SHA-256 values
  are respectively
  `9d7a6165417dc5c24880d034486e7c6ab1c73ccfa50d46006c144442ec9a668c`
  and
  `ebee8ef52aae36f86cd1189e2fc2faa84aab37253b61cbcb0d7a79186340ed51`.
  PR #41 subsequently passed its post-promotion native rerun and bootstrap;
  it merged as `4408a873`. The GNU tier table on `trunk` was then 31 implemented
  / 6 parsed-ignored / 8 refused.
- The merged `s56.5-old-style-designators` tranche recognizes GNU's
  historical `field: value` initializer spelling only at the start of an
  initializer-list item and materializes the existing field-designator AST.
  Current-object traversal, override handling, static images, automatic
  initialization, compound literals, and both backends therefore reuse the
  standard `.field = value` path. `-pedantic` emits GCC's obsolete-designator
  warning, while `__extension__` suppresses it. Permanent fixtures cover
  static, automatic, nested, and compound-literal initializers plus the
  warning and suppression boundary; the GNU tier table is 32 / 6 / 8 on
  `trunk`. Adding those two deterministic fuzz inputs intentionally changes
  the 5,000-iteration sequence digest from `acde131a08f5ca93` to
  `389f09592ae84eb1`, reproduced twice before repinning.
  The original 70-cell parse bucket decomposes honestly after this repair:
  six sources pass at all five levels on x86-64 and compile to ARM assembly at
  all five levels, while `compile/init-3.c` advances to the already-documented
  GNU empty-record refusal `b28fda1f...`. Thus exactly 60 cells are eligible
  for target-complete promotion and the remaining ten belong to the existing
  `wontfix-0.1.0` bucket. Pre-promotion PR CI
  [run 33051366758](https://github.com/tenseleyFlow/Cgfried/actions/runs/33051366758)
  produced hosted x86 stream SHA-256
  `e755a8084f9412ec7e847c278343f0d85c25fba1dbfbc2032719ac87f6c4a621`
  at synthetic merge revision `091fac4718cfa4d1799972b383b053b03d35cf75`
  and refused exactly the 30 uncommitted x86 PASS cells. Exact implementation-
  head native full-lattice
  [run 33051386292](https://github.com/tenseleyFlow/Cgfried/actions/runs/33051386292)
  produced ARM stream SHA-256
  `50f5e743e305f93d898b9362219b65cbef14dbff39eee0fe0ec6ab09bf13ab51`
  and refused exactly the matching 30 native PASS cells at
  `333876424a085ca7b61ea1cb656fc6cd739c3dc5`. The publishable same-head x86
  stream was regenerated locally as
  `4f5638f6075ec465ba2c5f924bc8b57b1e7d42f059da178aa63d9b717314ab6f`.
  All three streams share compiler-source SHA-256
  `ff31cafeae29c8e1ba19c4ed2139fb621bae4d542f9f99a68ec0c0d4657a51ae`
  plus identical harness and manifest hashes; hosted and local x86
  classification rows are byte-identical with SHA-256
  `cd12b88b6cb82bfdf89f68eb262ce1d61a8ab30ca5118586203b4f9e1c752e30`.
  Atomic publication promoted exactly those 60 cells, retired fingerprint
  `90b3c7e7...`, and preserved all ten `init-3.c` cells under `b28fda1f...`.
  The resulting overlay is 57 applied / 0 stale / 0 unresolved and has 7,435
  classified failures. Reversed input order regenerates both outputs byte-
  identically; the PASS and triage SHA-256 values are respectively
  `e10c32ea7c0e7486654f66d244980503ea0f9025bc44af61daa3a2eee64b7749`
  and
  `ba039f0eca2b3476027ee506e1e9c02943726bc02914e11480b69b5f08f8ce15`.
- The merged `s56.5-range-designators` tranche recognizes GNU's inclusive
  `[first ... last]` spelling, folds each endpoint once as a nonnegative ICE,
  completes unsized arrays from the highest selected endpoint, and expands
  chained ranges as a Cartesian product through the existing current-object
  machinery. Static images, relocations, automatic aggregates, compound
  literals, later overrides, and both backends reuse the ordinary designator
  paths. Runtime values are materialized once per original range initializer,
  preserving GNU's side-effect-once rule even for aggregate leaves.
  `-pedantic` warns while `__extension__` suppresses the warning. The tracked
  GNU tier table is 33 implemented / 6 parsed-ignored / 8 refused on `trunk`.
  Focused unit, runner,
  x86-64 O2 execution, ARM64 O0/O2 assembly, and the 220-file parser
  differential are green. Adding the two deterministic fuzz inputs changes
  the 5,000-iteration sequence digest from `389f09592ae84eb1` to
  `08590e5f9c6ebe35`, reproduced twice after formatting before repinning. The
  full repository suite is also green: 810 unit tests / 4,293,858 assertions,
  720 program fixtures, 104 corpus fixtures, and every differential, fuzz,
  cross, policy, and formatting gate. Full sanitizer and strict-Clang runs are
  green. Exact implementation-head PR CI
  [run 33134253176](https://github.com/tenseleyFlow/Cgfried/actions/runs/33134253176)
  passed every job, including the 100k-iteration fuzz lane and hosted x86
  torture. Exact-head native full-lattice
  [run 33135159935](https://github.com/tenseleyFlow/Cgfried/actions/runs/33135159935)
  passed at `dc9af72d7fd307e984205d493740da4eb1cc7d55`; its ARM stream
  SHA-256 is
  `f66340ec1e8efd5ffbfa0c2030f5b51e121ae66accfe208ca778ae3d6342a0e7`.
  The publishable same-head x86 stream was regenerated locally as
  `9b7d6918bb7767470e6be76052896d4950963870a12b6c7dede5e5bd9ab9eb25`.
  Both streams share compiler-source SHA-256
  `df2640077d2cf19a6ed1a69ba4c888603f629777b7ab62ca3a345c961438bf31`
  plus identical harness and manifest hashes. Atomic publication retired
  range-designator fingerprint `33116fbd...` without changing the PASS
  ratchet: the ten residual `ctestsuite/00216.c` cells now normalize under the
  existing GNU empty-record refusal `b28fda1f...`. The resulting overlay is
  57 applied / 0 stale / 0 unresolved and retains 7,435 classified failures.
  Reversing the two inputs regenerates both outputs byte-identically; the PASS
  and triage SHA-256 values are respectively
  `e10c32ea7c0e7486654f66d244980503ea0f9025bc44af61daa3a2eee64b7749`
  and
  `ba039f0eca2b3476027ee506e1e9c02943726bc02914e11480b69b5f08f8ce15`.
  Post-publication CI
  [run 33138150747](https://github.com/tenseleyFlow/Cgfried/actions/runs/33138150747)
  then passed all 21 jobs at `52b627af`, including hosted torture, sanitizers,
  and the full fuzz budget; both bootstrap workflows were also green. PR #43
  merged as `6c1cd12b`.
- The current `s56.5-gnu-ident-directive` tranche implements GNU `#ident` and
  deprecated `#sccs` end to end: macro-expanded directive parsing, exact-byte
  IR metadata and round trips, target-aware string decoding, and ELF
  `.comment` emission shared by x86-64 and ARM64. Darwin consumes the directive
  without emitting an ELF-only section. GNU tiers are now 34 implemented / 6
  parsed-ignored / 8 refused. Full local validation is green with 812 unit
  tests / 4,293,894 assertions, 724 program fixtures, 104 corpus fixtures, and
  every differential, fuzz, cross-target, policy, and formatting gate. The
  5,000-iteration frontend sequence digest is `c936967b364a5c43`, reproduced
  twice before repinning. Pre-publication PR CI
  [run 33144370788](https://github.com/tenseleyFlow/Cgfried/actions/runs/33144370788)
  passed every completed non-torture job; hosted torture refused only the five
  newly passing x86 cells. Its synthetic-merge x86 stream SHA-256 is
  `99c80ad9fbb8ae26ebd2fbcdba2c84cd194074f228e54e42df39273325737192`,
  and its classification body is byte-identical to the publishable local
  stream. Exact-head native full-lattice
  [run 33144389270](https://github.com/tenseleyFlow/Cgfried/actions/runs/33144389270)
  refused only the matching five ARM64 cells; that stream SHA-256 is
  `a77e947f9004140beb29728b9acf67ebce787d7a60dc2e37c911a553e150beb7`.
  `make torture-baseline` regenerated the exact-head x86 stream as
  `fc4105f49dcc73d4b544553e95d26be0dab21b649093d5070a063c6a417608d1`.
  Both publishable streams share compiler-source SHA-256
  `09ab4987c601f7f96ff13bf8b3e8e42f5ce361c7ecb6d89e8b2c17c3af0ed1fc`
  plus identical harness and manifest hashes. Atomic publication promoted
  exactly the ten `20010510-1.c` cells with zero PASS regression and retired
  fingerprint `35631614...`. The resulting overlay is 56 applied / 0 stale /
  0 unresolved with 7,425 classified failures. Reversing the two input streams
  regenerates both outputs byte-identically; the PASS and triage SHA-256 values
  are respectively
  `e878f6e83bc5cfafb9e4e9d053c7e41b7946676f6e87a0f88e4345712b5973bb`
  and
  `5de49e9e6e873238e50717c0494be87ebba785e4f576b6ba561e717f6dd6d3e9`.
  Post-publication CI
  [run 33146881168](https://github.com/tenseleyFlow/Cgfried/actions/runs/33146881168)
  passed all 21 jobs at `58b4a385`, including hosted x86 torture, sanitizers,
  QEMU, and the 100k frontend fuzz lane. Exact-head native full-lattice
  [run 33146886319](https://github.com/tenseleyFlow/Cgfried/actions/runs/33146886319)
  passed the ARM64 ratchet, and bootstrap runs
  [33146878030](https://github.com/tenseleyFlow/Cgfried/actions/runs/33146878030)
  and
  [33146881396](https://github.com/tenseleyFlow/Cgfried/actions/runs/33146881396)
  passed O0/O2. PR #44 merged as `d52e44d1`.
- The current `s56.5-gnu-pp-assertions` tranche implements deprecated GNU
  cpplib assertions end to end: raw `#assert`/`#unassert` directives, multiple
  answers in a macro-independent namespace, exact raw-token answer matching,
  answer-less predicate tests, macro-produced assertion operators, skipped-
  group behavior, and GCC-compatible pedantic/deprecation precedence. GNU
  tiers are 35 implemented / 6 parsed-ignored / 8 refused on this branch.
  Permanent fixtures cover the extension semantics and malformed inputs; the
  live GCC torture exemplar `torture-compile/950919-1.c` passes locally at all
  five optimization levels. Adding those fixtures intentionally changes the
  deterministic frontend-fuzz digest from `c936967b364a5c43` to
  `5b0f8176143e751e`, independently reproduced twice with the normal build and
  once under ASan+UBSan before repinning. Full `make test` is green at 814 unit
  tests / 4,293,918 assertions, 727 program fixtures, and 104 permanent corpus
  fixtures; full ASan+UBSan, both 74-case ppdiff modes, and a strict Clang build
  are also green. `make bootstrap-O0` and `make bootstrap-O2` each reproduce
  114 assembly files, 114 objects, the runtime archive, and the compiler byte-
  identically with no normalization.
  The exact fingerprint is
  `91d2215b56a7eef42a5038abc931eace3e3f0478ed15892b898474014e9ab6d1`.
  Pre-publication PR CI
  [run 33152515629](https://github.com/tenseleyFlow/Cgfried/actions/runs/33152515629)
  passed every completed non-torture job at implementation head `dcf58165`;
  hosted torture refused only the five newly passing x86-64 cells. Exact-head
  native full-lattice
  [run 33152534012](https://github.com/tenseleyFlow/Cgfried/actions/runs/33152534012)
  passed all fourteen non-torture jobs and refused only the matching five
  ARM64 cells. The publishable x86-64 and ARM64 streams have SHA-256 values
  `e9c57d187452752b4b847539942e9055c43f676994b4588e248437e2ffa3b5fa`
  and
  `c0f34a451447c1127d69b6bc7b0db6f35121479f9170f6b559cde5fa0a93f10c`.
  They share source revision `dcf58165`, compiler-source SHA-256
  `3b45a7f0698486cda6384e6487b2251f75540194594b75966fe4591364b290be`,
  harness SHA-256
  `b6e50c45f810d83e0b9e5b5adcc722f8ec2a5e2afdc98a0611386507b01a07b5`,
  torture-manifest SHA-256
  `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`,
  and ctestsuite-manifest SHA-256
  `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`.
  Atomic publication promotes exactly the ten `950919-1.c` target-level cells
  with zero PASS regression and retires the assertion fingerprint. The result
  is 26,615 PASS cells, 7,415 classified failures, 64 buckets, 55 applied
  decisions, zero stale/unresolved decisions, and 31 live `s56.5-*` repair
  decisions. Reversing the two evidence streams regenerates both outputs
  byte-identically; the PASS and triage SHA-256 values are respectively
  `60feb4bac9c8a388d39911a65b38dd28a9e4f2684008178ab6475b9d5ea9e028`
  and
  `98b9b11129eac39cfe6cbcf6f07aa49977e4e43f8020fb0beaed40ed91aadfb9`.
  Post-publication CI
  [run 33154192442](https://github.com/tenseleyFlow/Cgfried/actions/runs/33154192442)
  passed all required jobs, including the 38m23s frontend-fuzz lane, and PR
  #45 merged as `90d1b397`.
- The `s56.5-pragma-macro-stack` tranche implements GNU
  `#pragma push_macro` / `#pragma pop_macro` and their `_Pragma` forms with
  arena-owned definition snapshots, independent per-name LIFO stacks,
  undefined-state restoration, and point-sensitive macro-history events.
  Ordinary, `L`, `u`, `U`, and `u8` string-token operands follow GCC's raw
  spelling behavior; unmatched pops are silent, valid prefixes with trailing
  tokens apply then warn, and malformed operands hard-error. GNU tiers are 36
  implemented / 6 parsed-ignored / 8 refused on this branch. Full `make test`
  is green at 816 unit tests / 4,293,945 assertions, 728 program fixtures, and
  104 permanent corpus fixtures. The deterministic frontend-fuzz digest moved
  from `5b0f8176143e751e` to `4f3b5b345891f2a3`, was independently reproduced
  twice before repinning, and passes under ASan+UBSan. Full `make test-san`, a
  strict Clang build, and byte-identical 114-file `bootstrap-O0` and
  `bootstrap-O2` runs are green.
  Exact-head x86-64 torture at `2723597b` produces only ten new PASS cells:
  `ctestsuite/00206.c` and `torture-execute/pushpop_macro.c`, each at
  O0/O1/O2/O3/Os. Its stream SHA-256 is
  `35a71bda1748dbb5912bf425514e931d69b5a14d7acbf0322b11dfb5fe3b18bc`;
  compiler-source, harness, torture-manifest, and ctestsuite-manifest SHA-256
  values are respectively
  `d69b68eff22ed81ad0988d054feb31147571fba7e23653e2be38e2fe4dc42b19`,
  `b6e50c45f810d83e0b9e5b5adcc722f8ec2a5e2afdc98a0611386507b01a07b5`,
  `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`,
  and `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`.
  The dedicated `00206.c` fingerprint is
  `2082b1f4d8085345bd3ead486cdd5244655c0b103571ee5a780f92679bac374c`.
  `pushpop_macro.c` was hidden inside the coarse runtime-SIGABRT fingerprint
  `de25f493e2a030af329f5f01121c9f9249da8508936bfa0a119d0a5c8f638731`;
  publication promotes its five cells but retains that policy row for the
  unrelated abort families. Pre-publication PR #46 CI
  [run 33157598323](https://github.com/tenseleyFlow/Cgfried/actions/runs/33157598323)
  passed every non-torture job, including fuzz, sanitizers, QEMU, and both
  compiler builds; hosted torture refused only the ten x86-64 cells above.
  Exact-head native full-lattice
  [run 33157603729](https://github.com/tenseleyFlow/Cgfried/actions/runs/33157603729)
  passed all fourteen non-torture jobs and refused only the matching ten ARM64
  cells. The ARM64 stream SHA-256 is
  `e8e08b9df061fb2296c99c6c5f5dc4e81678fb902fd2fb310f7a50c14ed4f9fe`;
  its common provenance matches the x86-64 stream exactly.
  Atomic publication promotes exactly twenty target-level cells with zero
  PASS regression, retires only fingerprint `2082b1f4...`, and preserves the
  generic runtime-abort decision. The result is 26,635 PASS cells, 7,395
  classified failures, 63 buckets, 54 applied decisions, zero stale/unresolved
  decisions, and 30 live `s56.5-*` repair decisions. Reversing the two evidence
  streams regenerates both outputs byte-identically; the PASS and triage
  SHA-256 values are respectively
  `161fbcc12e25be9ae2260f173ed1d44d898f4bc0fbea606a93a0fc20e0c08de1`
  and
  `9cfc7d3a01c1c31764c571ea41422c77b5d35ca140782f33f5f32a9f96882d46`.
  Post-publication CI
  [run 33160370673](https://github.com/tenseleyFlow/Cgfried/actions/runs/33160370673)
  passed every required job, including the 35m frontend-fuzz lane, and PR #46
  merged as `eba78f27`.
- The current `s56.5-alignof-extensions` tranche implements GNU
  `__alignof__` / `__alignof` type and unevaluated-expression forms. Sema
  preserves exact aligned-declaration, effective-member, packed-member, and
  parenthesized-lvalue alignment; bitfields are rejected; GNU void and
  function forms return the target extension alignment with GCC-compatible
  pedantic warning groups. GNU tiers are 37 implemented / 6 parsed-ignored /
  8 refused on this branch. Full `make test` is green at 816 unit tests /
  4,293,948 assertions, 731 program fixtures, and 104 permanent corpus
  fixtures. The deterministic frontend-fuzz digest moved from
  `4f3b5b345891f2a3` to `d9ec583ab2f56992`, was reproduced twice before
  repinning, and passes under ASan+UBSan. Full `make test-san` and a strict
  Clang build are green.
  Pre-publication PR #47 CI
  [run 33162872170](https://github.com/tenseleyFlow/Cgfried/actions/runs/33162872170)
  passed all nineteen non-torture jobs and refused only the expected fifteen
  x86-64 PASS additions. Matching-head native full-lattice
  [run 33162897964](https://github.com/tenseleyFlow/Cgfried/actions/runs/33162897964)
  passed all fourteen non-torture jobs and refused only the matching fifteen
  ARM64 additions. Exact-head x86-64 and ARM64 streams have SHA-256 values
  `e67599c70160bbf87a9e55161ade19f3153ccf675c9145b71a81a304202fceab`
  and
  `6adc440ea5bc7d9bea521dbd345ba18c1c749f988e2136930a418b26aabe49df`.
  Both are stamped at `4e247d4b` and share compiler-source, harness,
  torture-manifest, and ctestsuite-manifest SHA-256 values
  `256baaf51ceb54fedf5dbc249396d51ec9131ac644d6be149ec8fd68a37bcf5a`,
  `b6e50c45f810d83e0b9e5b5adcc722f8ec2a5e2afdc98a0611386507b01a07b5`,
  `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`,
  and `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`.
  Atomic publication promotes exactly thirty target-level cells with zero
  PASS regression and retires fingerprints `03642ec0...` and `48e045cd...`.
  The result is 26,665 PASS cells, 7,365 classified failures, 61 buckets, 52
  applied decisions, zero stale/unresolved decisions, and 28 live `s56.5-*`
  repair decisions. Reversing the two evidence streams regenerates both
  outputs byte-identically; the PASS and triage SHA-256 values are
  respectively
  `98d1cd1e25e77b1b990cb2eeb755d973de6f9eb72c0be58cb9a8bcf0f096e07c`
  and
  `28ac212395a7ec59be985949b5c037cd14eb2220221e2a95185609e9219c5596`.
- The current `s56.5-extension-type-name` tranche treats GNU `__extension__`
  as a prefix on whole declarations and cast expressions while keeping an
  expression-position marker out of declaration/type-name lookahead. Parser,
  diagnostic, executable, and targeted torture regressions are green; the
  complete local suite passed with 816 unit tests / 4,293,958 assertions, 731
  program fixtures, and 104 corpus fixtures. PR #48 pre-publication CI
  [run 33168882651](https://github.com/tenseleyFlow/Cgfried/actions/runs/33168882651)
  passed every completed ordinary lane and refused only fifteen new x86-64
  PASS cells; exact-head native full-lattice
  [run 33168896428](https://github.com/tenseleyFlow/Cgfried/actions/runs/33168896428)
  refused only the identical fifteen ARM64 cells. The publishable exact-head
  x86-64 and ARM64 streams have SHA-256 values
  `4edce9ce45d6a656f6dde4c9b77f493402e91a464df0acc9276e237d5667028c`
  and
  `611bcad69505ad6a31f3291763ed880c178077a1e11b7147188f0847ced94730`.
  Both are stamped at `e0c5604c` and share compiler-source, harness,
  torture-manifest, and ctestsuite-manifest SHA-256 values
  `fc6a8ca3288e0f9e4606fbbcd70c787f49c52c0c6a2dfde32be402ce8200cbbd`,
  `b6e50c45f810d83e0b9e5b5adcc722f8ec2a5e2afdc98a0611386507b01a07b5`,
  `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`,
  and `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`.
  Atomic publication promotes exactly thirty cells: `20011217-2.c`,
  `990117-1.c`, and `pr37056.c` at all five levels on both targets, with zero
  PASS regression. The residual compile-only `pr33382.c` cells now normalize
  under the existing broad unsupported-builtin fingerprint `70f8ab7d...` and
  its `wontfix-0.1.0` policy, so only the resolved `36853079...` row was
  retired. The result is 26,695 PASS cells, 7,335 classified failures, 60
  buckets, 51 applied decisions, zero stale/unresolved decisions, and 27 live
  `s56.5-*` policy rows. Reversing the two evidence streams regenerates both
  outputs byte-identically; the PASS and triage SHA-256 values are
  respectively
  `fb0e6b81a39dc9e1e8efce814deee4e1bf30edf74f4fac2965fa5f85cbe3e142`
  and
  `040ce629a331fc2e34487da2c792d10518cc636b6822a00da4c46b03f2753a13`.
- The merged `s56.5-generic-qualified-associations` tranche preserves
  top-level qualifiers while validating `_Generic` association compatibility,
  so `int` and `const int` remain distinct association types. Targeted
  conversion/sema tests and `ctestsuite/00219.c` pass at O0/O1/O2/O3/Os on
  native x86-64, native ARM64, and the local Darwin ARM64 compiler. PR #49
  pre-publication CI
  [run 33188071491](https://github.com/tenseleyFlow/Cgfried/actions/runs/33188071491)
  passed every ordinary lane, including macOS ARM64, native ARM, QEMU,
  sanitizers, campaigns, and the 100k frontend fuzz lane; only the expected
  five uncommitted x86-64 PASS cells failed. Exact-head native full-lattice
  [run 33188196659](https://github.com/tenseleyFlow/Cgfried/actions/runs/33188196659)
  likewise refused only the matching five ARM64 cells at `216b85ad`.
  GitHub's PR checkout produced a synthetic-merge x86 stream, so the
  publishable x86 stream was regenerated from the exact branch head on
  Kasumi. Its SHA-256 is
  `7a2340937a097c7e00797b8a57d1aa3e4d742186edea05ceda2d04630cf5fec6`;
  the native ARM stream SHA-256 is
  `fd5037dda9535681e6d13399cbcf91251865c503606ba0876712e660bb7e8fdf`.
  Both name source revision `216b85adccf9e5fbc292629977cc57dd214a8574`
  and share compiler-source SHA-256
  `b98affe80005b19d25a48719d510283e8a60c751ffe10ff61f3f4c593e66fe8f`
  plus identical harness and manifest hashes. `make torture-baseline`
  regenerated the x86 stream byte-identically and atomically promoted exactly
  the ten `00219.c` cells with zero PASS regression. It retired fingerprint
  `501d37ef...`; Kasumi's GCC 16 build reaches Cgfried's explicit 256-level
  bracket-nesting limit for `limits-exprparen.c` before the ARM-shaped harness
  output guard, so exact fingerprint `e52eb570...` now records the same
  deliberate translation-limit stress as `out-of-scope`. The result is 26,705
  PASS cells, 7,325 classified failures, 60 buckets, 51 applied decisions,
  zero stale/unresolved decisions, and 26 live `s56.5-*` repair rows. Reversing
  the two streams regenerates both outputs byte-identically; the PASS and
  triage SHA-256 values are respectively
  `97f08637f9496d2b66050713cda5d48f0d10545e5176d0ebdbb2d8271f4fd329`
  and
  `3b13ec4287013fc059068850421ac9d2a22d636154a12b53034d3532dfe9e916`.
  Publication commit `c79f551a` then passed complete standard CI
  [run 33193846144](https://github.com/tenseleyFlow/Cgfried/actions/runs/33193846144),
  including hosted x86 torture and the 100k sanitizer fuzz lane, plus a green
  exact-head native full-lattice
  [run 33193961044](https://github.com/tenseleyFlow/Cgfried/actions/runs/33193961044).
  PR #49 merged as `d8ccfc04`; 26,705 PASS cells and 26 live repair rows are
  now the `trunk` baseline.
- The merged `s56.5-extern-void-symbols` tranche accepts only a non-defining
  `extern void marker;` declaration for linker-script boundary symbols. Its
  address can retain a linker relocation and constant integer addend in a
  static pointer-width integer initializer; removing `extern`, adding an
  initializer, or otherwise defining a void object still errors. Restacked
  commit `55a5b2a6` is patch-identical to the transferred implementation.
  Native Darwin validation builds the compiler, passes the focused sema unit
  and `extern_void_symbol.c` fixture, and compiles torture exemplar
  `20011114-1.c` at O0/O1/O2/O3/Os as Mach-O arm64. PR #50 pre-publication CI
  [run 33197652607](https://github.com/tenseleyFlow/Cgfried/actions/runs/33197652607)
  passed every non-torture job, including macOS ARM64, native ARM, QEMU,
  sanitizers, campaigns, ordinary tests, and the 100k frontend fuzz lane;
  hosted torture refused only the expected five uncommitted x86-64 PASS cells.
  Exact-head native full-lattice
  [run 33197694401](https://github.com/tenseleyFlow/Cgfried/actions/runs/33197694401)
  passed every non-ARM-torture job and refused only the matching five ARM64
  cells at `4376c643`.
  The publishable exact-head x86-64 and ARM64 streams have SHA-256 values
  `76cad4d95ae5c26aacb31fe428afb4be4ef348470a2373663e0ec2d80ba1570e`
  and
  `89839c97b0b747910f30b2a322c44e5ef4a2f61a3be8f05144d3b3095d5c037d`.
  Both name source revision `4376c6434f85bf4d769bfe50e0319583203a95ca`
  and share compiler-source SHA-256
  `e5e1a15fec23ff59131824835f38e77633a0f2cf842435211c0a552991fd7675`
  plus identical harness and manifest hashes. A formal `make
  torture-baseline` regenerated the exact x86 stream byte-identically and
  atomically promoted exactly the ten `20011114-1.c` cells with zero PASS
  regression. It retired fingerprint `356cb2e2...`; the result is 26,715 PASS
  cells, 7,315 classified failures, 59 buckets, 50 applied decisions, zero
  stale/unresolved decisions, and 25 live `s56.5-*` repair rows. Reversing the
  two evidence streams regenerates both outputs byte-identically; the PASS and
  triage SHA-256 values are respectively
  `8ba6feb371b5291b134a36fdf88d6f527f3c3745e0fd182800f9d00aeb594d03`
  and
  `d7257d510dfa591bc62a5e9c0b5cd2049d6278297a325406e900e688d3e5f261`.
  Publication commit `d27275f` then passed complete standard CI
  [run 33201125831](https://github.com/tenseleyFlow/Cgfried/actions/runs/33201125831),
  exact-head native full-lattice
  [run 33201150078](https://github.com/tenseleyFlow/Cgfried/actions/runs/33201150078),
  and push/PR O0/O2 bootstrap runs
  [33201122115](https://github.com/tenseleyFlow/Cgfried/actions/runs/33201122115)
  and
  [33201125834](https://github.com/tenseleyFlow/Cgfried/actions/runs/33201125834).
  PR #50 merged as `e2439e79`; 26,715 PASS cells and 25 live repair rows are
  now the `trunk` baseline.
- The merged `s56.5-gnu-alloca-alias` tranche recognizes a direct plain
  `alloca(...)` call as the existing compiler builtin only in hosted GNU mode.
  Strict ISO and freestanding modes retain their ordinary external-call
  behavior, and a local function-pointer object named `alloca` remains an
  indirect call. Focused sema coverage and the `alloca_alias.c` executable are
  green on Darwin ARM64; the emitted assembly contains no external `alloca`
  call. PR #51 pre-publication CI
  [run 33219000576](https://github.com/tenseleyFlow/Cgfried/actions/runs/33219000576)
  passed every completed non-torture job while hosted torture refused only the
  expected five uncommitted x86-64 PASS cells. Exact-head native full-lattice
  [run 33219281094](https://github.com/tenseleyFlow/Cgfried/actions/runs/33219281094)
  passed every non-ARM-torture job and refused only the matching five ARM64
  cells at `c5a1c09c886026e1fffa2b96d0b78239a24cc124`; push and PR bootstrap
  runs
  [33218928682](https://github.com/tenseleyFlow/Cgfried/actions/runs/33218928682)
  and
  [33219000571](https://github.com/tenseleyFlow/Cgfried/actions/runs/33219000571)
  both pass O0/O2.
  The publishable exact-head x86-64 and ARM64 streams have SHA-256 values
  `db9f150b61391ce48e66b30d00299389b1d2d96c00e0b6e94caaf66dc36dfd21`
  and
  `5cfa4b5c884c0e19a76da728566a85fa82a15591b734f2128129023493efe9fb`.
  Both name source revision `c5a1c09c886026e1fffa2b96d0b78239a24cc124`
  and share compiler-source SHA-256
  `e579a74bf34bff0b5efcb552cad1360f2671daf0c253ce90fa59b2825928865c`
  plus identical harness and manifest hashes. A formal `make
  torture-baseline` regenerated the exact x86 stream byte-identically and
  atomically promoted exactly the ten `941202-1.c` cells with zero PASS
  regression. It retired fingerprint `8d61a6cd...`; the result is 26,725 PASS
  cells, 7,305 classified failures, 58 buckets, 49 applied decisions, zero
  stale/unresolved decisions, and 24 live `s56.5-*` repair rows. Reversing the
  two evidence streams regenerates both outputs byte-identically; the PASS and
  triage SHA-256 values are respectively
  `4529e1d33c03b9b499bee6cc7ac8ee1e67b07ae7df31cc84adcb6d3907f920ea`
  and
  `25f26a7aa93fd7aaf8874206b15048d7885d493562b06e59181360ed830d7dc6`.
  Publication commit `574cf3f` then passed complete standard CI
  [run 33221425350](https://github.com/tenseleyFlow/Cgfried/actions/runs/33221425350),
  exact-head native full-lattice
  [run 33221503684](https://github.com/tenseleyFlow/Cgfried/actions/runs/33221503684),
  and push/PR O0/O2 bootstrap runs
  [33221423351](https://github.com/tenseleyFlow/Cgfried/actions/runs/33221423351)
  and
  [33221425367](https://github.com/tenseleyFlow/Cgfried/actions/runs/33221425367).
  PR #51 merged as `d7d59fa`; 26,725 PASS cells and 24 live repair rows are
  now the `trunk` baseline.
- The `s56.5-compound-literal-array-completion` tranche treats a
  compatible array compound literal as a GNU static whole-array initializer.
  An incomplete destination inherits the literal's completed bound; normal
  pedantic mode diagnoses the extension, while `__extension__` suppresses
  that diagnostic without suppressing unrelated warnings. Focused sema
  coverage passes 21 tests / 334 assertions on Darwin ARM64, both native GNU
  fixtures pass, and `pr48517.c` compiles at O0/O1/O2/O3/Os. PR #52
  pre-publication CI
  [run 33227213979](https://github.com/tenseleyFlow/Cgfried/actions/runs/33227213979)
  passed every non-torture job, including sanitizers and the 100k frontend
  fuzz lane; hosted torture refused only the expected five uncommitted x86-64
  PASS cells. Exact-head native full-lattice
  [run 33227213051](https://github.com/tenseleyFlow/Cgfried/actions/runs/33227213051)
  passed every non-ARM-torture job and refused only the matching five ARM64
  cells at `0e0578f64a7c3a119230cf0bed103a0c8afd6a44`; push and PR bootstrap
  runs
  [33227212388](https://github.com/tenseleyFlow/Cgfried/actions/runs/33227212388)
  and
  [33227213994](https://github.com/tenseleyFlow/Cgfried/actions/runs/33227213994)
  both pass O0/O2.
  GitHub's PR checkout produced source revision `24e1677b...` for its
  synthetic-merge x86 stream, so it could not truthfully combine with the
  exact-head ARM artifact. The publishable x86 stream was regenerated at the
  exact branch head on Kasumi. Its SHA-256 is
  `0d11d5cb07dffdf31bdd31b7f24571cdc2d5e267d83ab89d62732f50bcc3612e`;
  the retained native ARM stream SHA-256 is
  `31ce76e34af6ceee55024278c5c4072e1a26ab9454e72e00d42d0281f1157c3c`.
  Both name source revision `0e0578f64a7c3a119230cf0bed103a0c8afd6a44`
  and share compiler-source SHA-256
  `7e1ab9aaa5960d6daf68ad17c8a25ae520f3e42f1816a71507af1209730b068e`
  plus identical harness and manifest hashes. A formal `make
  torture-baseline` atomically promoted exactly the ten `pr48517.c` cells
  with zero PASS regression and retired fingerprint `85d8b226...`. The
  result is 26,735 PASS cells, 7,295 classified failures, 57 buckets, 48
  applied decisions, zero stale/unresolved decisions, and 23 live `s56.5-*`
  repair rows. Reversing the two evidence streams regenerates both outputs
  byte-identically; the PASS and triage SHA-256 values are respectively
  `50cbd08e0820fad479deb207d419db66d2b0f1a23973f1e393fd855cb198bdf4`
  and
  `fc8bee62348f5a48b9da90f4d5325fe13d885f40ebf7f1ac9cd212ea5a25870d`.
  The Apple-silicon `yew-s57` x86 emulator completed the same exact-head
  matrix, but triage correctly refused its diagnostic-only stream because
  GNU `timeout` surfaced SIGSEGV/SIGABRT as host-specific `Segmentation
  fault`/`Aborted` text. Do not add policy variants for those emulator-only
  fingerprints; Kasumi remains the documented native x86 publication host.
  Publication commit `e57a72e` passed complete CI and PR #52 merged as
  `cfaec8d`; the 26,735 PASS cells and 23 live repair rows became the `trunk`
  baseline.
- The current `s56.5-torture-failure-decomposition` tranche fixes the harness
  truth before selecting another compiler repair. Runtime `SIGNAL` failures
  now hash `(suite, testcase, signal)` rather than host `timeout`/core-dump
  wording: one testcase converges across target and optimization lanes, while
  unrelated wrong-result families cannot collapse. The former 150-cell abort
  bucket is now 15 ten-cell testcase buckets. Its policy also corrects
  `stdarg-4.c`: both globals are emitted; the undefined `f1i`/`f2i` calls come
  from ignored GNU `always_inline` combined with C17 inline fallback rules.
  Implementation head `42b8f95f02e257e8b494ad26e857338aa11dc73a` passed
  complete PR CI
  [run 33279454060](https://github.com/tenseleyFlow/Cgfried/actions/runs/33279454060)
  and both required bootstrap levels
  [run 33279454012](https://github.com/tenseleyFlow/Cgfried/actions/runs/33279454012).
  Exact-head full-lattice
  [run 33279462432](https://github.com/tenseleyFlow/Cgfried/actions/runs/33279462432)
  is green on attempt 2 after attempt 1's isolated `raise-race-static` runtime
  race; both attempts' native ARM torture streams are byte-identical. The
  retained ARM stream SHA-256 is
  `b8185e6327aeb9bf31f51f0944179861ab66fa7c0a8ac62474e196172e4dff91`;
  Kasumi's independently regenerated exact-head x86 stream is
  `895fd3bfe0a2f4b7c079458bb78ea0afaa685b3785c255d4a48697bbd979cea4`.
  Both name the implementation head and share compiler-source SHA-256
  `7e1ab9aaa5960d6daf68ad17c8a25ae520f3e42f1816a71507af1209730b068e`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  and identical manifest hashes. Formal `make torture-baseline` regenerated
  the x86 stream byte-identically and kept the target-complete PASS ratchet at
  26,735 with zero regression. The published result is 7,295 classified
  failures, 71 buckets, 62 applied decisions, zero stale/unresolved decisions,
  and 37 live repair rows representing 32 unique compiler tranches. Reversing
  the evidence streams regenerates both outputs byte-identically; PASS and
  triage SHA-256 values are respectively
  `50cbd08e0820fad479deb207d419db66d2b0f1a23973f1e393fd855cb198bdf4`
  and
  `c2b6c2a3a34c98f51596725e2e9fa15add41b649ff28573bf3c7d2f5500ca7d5`.
- The current `s56.5-gnu-always-inline` tranche implements the GNU
  `always_inline` function attribute, including bare and parenthesized
  spellings, union across compatible redeclarations, C inline-only body
  retention, and mandatory direct-call inlining at every optimization level.
  Mandatory calls bypass profitability and debug-info bailouts but diagnose
  unsafe unsupported shapes; forced-recursion detection follows only
  `always_inline` edges. Exact old-style signatures and caller-loop constant
  allocas are supported, dynamic allocas are refused, address references stay
  external, and the temporary inline-only analysis bodies are stripped after
  inlining. Darwin ARM64 focused validation passes 3 optimizer tests / 44
  assertions, 1 sema test / 14 assertions, the native fixture, strict compiler
  construction, and native `stdarg-4.c` execution at O0/O1/O2/O3/Os. Kasumi
  passes full `make test` (824 unit tests / 4,294,044 assertions, 736 program
  fixtures, and all 104 corpus cases plus bootstrap/differential/fuzz/campaign/
  policy/format gates) and full Clang 22 ASan+UBSan validation with zero
  findings. GCC 16's sanitizer frontend ICEs in its own `vartrack` pass while
  compiling unchanged x86 instruction selection; strict normal GCC and Apple
  Clang builds are green. All 270 runnable existing `always_inline` torture
  compile-status cells preserve their prior accept/refuse result.
  Implementation head `bfdb8f151e59149a0b995e84db74f817ee1af948` passed every
  non-torture job in pre-publication PR CI
  [run 33285501834](https://github.com/tenseleyFlow/Cgfried/actions/runs/33285501834),
  including sanitizers and the 100k frontend fuzz lane; torture refused exactly
  the five expected uncommitted x86-64 `stdarg-4.c` PASS cells. Push and PR
  bootstrap runs
  [33285490181](https://github.com/tenseleyFlow/Cgfried/actions/runs/33285490181)
  and
  [33285501875](https://github.com/tenseleyFlow/Cgfried/actions/runs/33285501875)
  both pass O0/O2. Exact-head native full-lattice
  [run 33285511199](https://github.com/tenseleyFlow/Cgfried/actions/runs/33285511199)
  passes every non-ARM-torture job and refuses only the matching five ARM64
  cells. The retained Kasumi x86 stream SHA-256 is
  `d2bde40536c78d84415e5d1147e57d0a29c4839522a3eab623c27b5d95993197`;
  the native ARM stream is
  `4e38cac4cc51720c87603b579efd18dc1b31908e5511a695f15895b5a4c9bfa4`.
  Both name the exact implementation head and share compiler-source SHA-256
  `5c082b98dd1668bbb4f53783829603c665bc61505f6852738fc6383c5cddc115`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  torture-manifest SHA-256
  `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`,
  and ctestsuite-manifest SHA-256
  `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`.
  Formal `make torture-baseline` regenerated the x86 stream byte-identically,
  atomically promoted exactly ten `stdarg-4.c` cells with zero PASS regression,
  and retired fingerprint `a1baad615656922dd0c5cf03bae7bde4254cdc850813b578887e94bd88520db6`.
  The published result is 26,745 PASS cells, 7,285 classified failures, 70
  buckets, 61 applied decisions, zero stale/unresolved decisions, and 36 live
  repair rows representing 31 unique compiler tranches. Reversing the evidence
  streams regenerates both outputs byte-identically; PASS and triage SHA-256
  values are respectively
  `fba35b5fac513ae1c381da34af8165411f8f900be37e9c6bc3f4286a343c3db6`
  and
  `0564e5d8baebe52b6fcf5faa826f72aff3ecf4043f6fbc2633693375445b895c`.
- The merged `s56.5-void-deref-and-pointer-composite` tranche implements
  WG14 DR106 semantics for dereferencing plain and qualified `void *`:
  semantic analysis preserves a non-lvalue qualified-void expression, emits
  the default-on suppressible `-Wvoid-ptr-dereference` diagnostic, and lowering
  retains pointer-expression side effects without issuing a load. It also
  implements C 6.7.2.2p4 compatibility between an enum and its chosen integer
  representation, repairing conditional pointer composition between the
  corresponding pointer types while preserving distinct enum-tag rules.
  Darwin ARM64 focused sema/lowering validation passes, warning controls and
  the warning matrix pass, and `enum-3.c` executes at O0/O1/O2/O3/Os on both
  native Apple ARM64 and Kasumi x86-64 Linux. Kasumi's full unit suite passes
  826 tests / 4,294,061 assertions with zero failures. Implementation head
  `cf82fee9473feab06cd6a3c1927e56d78ea30cdb` passed every non-torture job in
  pre-publication PR CI
  [run 33290766860](https://github.com/tenseleyFlow/Cgfried/actions/runs/33290766860);
  torture refused exactly the ten expected uncommitted x86-64 PASS cells.
  Push and PR bootstrap runs
  [33290765612](https://github.com/tenseleyFlow/Cgfried/actions/runs/33290765612)
  and
  [33290766846](https://github.com/tenseleyFlow/Cgfried/actions/runs/33290766846)
  both pass O0/O2. Exact-head native full-lattice
  [run 33290781792](https://github.com/tenseleyFlow/Cgfried/actions/runs/33290781792)
  passes every other job and refuses only the matching ten ARM64 cells. The
  formally regenerated Kasumi x86 stream SHA-256 is
  `1909524633a0453bd8c2e039de5e739fe495339d44470550bf667ae2fb0f14fc`;
  the native ARM stream is
  `24b4ea8cddabfe208756d2f39baa8cf2de749814b241be00f427b9094656e151`.
  Both name the exact implementation head and share compiler-source SHA-256
  `e24c081b0efe2fea60a38e014814ef5024d44ccbb96aac9f6d42b3d99d831405`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  torture-manifest SHA-256
  `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`,
  and ctestsuite-manifest SHA-256
  `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`.
  Formal `make torture-baseline` atomically promotes exactly twenty cells and
  retires the repaired decision plus one independently proven stale host
  variant. The publication result is 26,765 PASS cells, 7,265 classified
  failures, 68 buckets, 59 applied decisions, zero stale/unresolved decisions,
  and 35 live repair rows representing 30 unique compiler tranches. Reversing
  the evidence streams regenerates both outputs byte-identically; PASS and
  triage SHA-256 values are respectively
  `d14d4a64ab2a29a56d04b675213a111ecd2288092d65226bab5cf63491279c64`
  and
  `8a5d5097c088bcd0201bafc470688577bb6f6a320b07f006e496c9a13212acc1`.
- The merged `s56.5-gnu-aggregate-self-cast` tranche accepts explicit casts
  between compatible same-tag struct or union types as address-preserving GNU
  aggregate identity conversions. It emits the GCC-compatible pedantic
  diagnostic `ISO C forbids casting nonscalar to the same type`, promotes it
  under `-pedantic-errors`, and preserves `__extension__` suppression by
  carrying the parser's suppression state on the explicit-cast AST node until
  semantic analysis. Distinct aggregate tags and scalar/pointer-to-union casts
  remain rejected. Focused Darwin ARM64 validation passes 1 semantic test / 5
  assertions, all 23 semantic tests / 349 assertions, both new program
  fixtures, and native O0/O1/O2/O3/Os execution. Kasumi passes the full 827
  unit tests / 4,294,066 assertions, both fixtures, native execution at all
  five optimization levels, and the frontend fuzz smoke. Adding the two
  fixture sources intentionally changed the deterministic frontend-fuzz
  sequence; Apple ARM64 and Kasumi independently reproduced digest
  `5593b66bc5bf02cf` before it was repinned.
  Implementation head `b61f598f3441a52b2f81a67ea97f95d74d812f73`
  passed every non-torture job in pre-publication PR CI
  [run 33294900014](https://github.com/tenseleyFlow/Cgfried/actions/runs/33294900014),
  including sanitizers and the 100k frontend fuzz lane; torture refused
  exactly the five expected uncommitted x86-64 `20010605-2.c` PASS cells.
  Push and PR bootstrap runs
  [33294898204](https://github.com/tenseleyFlow/Cgfried/actions/runs/33294898204)
  and
  [33294900011](https://github.com/tenseleyFlow/Cgfried/actions/runs/33294900011)
  both pass O0/O2. Exact-head native full-lattice
  [run 33294922631](https://github.com/tenseleyFlow/Cgfried/actions/runs/33294922631)
  passes every other job and refuses only the matching five ARM64 cells. The
  retained native ARM stream (artifact `9727253525`) has SHA-256
  `a5c702113e14fa4f500b3388c50b4a96f96d798d742999c677c931fed9b2fb2b`.
  The hosted PR x86 stream has SHA-256
  `281dc963b90d5811821ad6c5c0b98892d993255c834f5016c4a6fdd21247c1ae`
  and independently confirms the same five promotions with zero regression;
  it is confirmation-only because Actions checked out GitHub's synthetic
  merge revision. Formal Kasumi publication regenerated the exact branch-head
  x86 stream as
  `cf5bb0e92b25a060bca1654349eed7e395300b48ae0c2e113a3383bd4581f3d4`.
  The two authoritative streams name the implementation head and share
  compiler-source SHA-256
  `4b3005e4f0f9a151d884fd81974c9391b126ef8cba54b657bcf9009545861a19`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  and identical manifest hashes. Formal `make torture-baseline` promotes
  exactly ten `20010605-2.c` cells with zero PASS regression and retires
  fingerprint `f9c7d944f95255e125b87ba41cc8c076edc1534f3b1f60329baf2d2af9660dfb`.
  Source audit proved that fingerprint
  `ae6394ea751369cfa3f15c078e72a70e5989fc74012072b928877865cabac9cb`
  is instead a pointer-to-union cast, so its durable policy moves to the
  existing `s56.5-gnu-scalar-to-union-casts` tranche. The publication result
  is 26,775 PASS cells, 7,255 classified failures, 67 buckets, 58 applied
  decisions, zero stale/unresolved decisions, and 34 live repair rows
  representing 29 unique compiler tranches. Reversing the two evidence
  streams regenerates every output byte-identically; PASS and triage SHA-256
  values are respectively
  `fee438d61501aefe19374eca3d4756af5615ffc4eb69d6df1c3848064e4aa057`
  and
  `995a91ff47d4ab1eef7d2484545af9e0f209173cb8b477903d394e19a99d320f`.
  PR #56 merged as `0295bd50`; the next completed semantic tranche was
  `s56.5-gnu-scalar-to-union-casts`, with 20 target-complete cells across
  `pr42708-1.c` and `960416-1.c`.
- The merged `s56.5-gnu-scalar-to-union-casts` tranche implements GCC's
  cast-to-union extension for an operand whose post-lvalue-conversion type
  exactly matches
  an ordinary non-bit-field member. Top-level qualifiers are ignored, while
  enum/integer compatibility, arithmetic conversions, pointer conversions,
  distinct aggregate tags, and bit-field base types do not create a match.
  Runtime lowering materializes a union temporary, evaluates the operand once,
  and supports scalar, pointer, and aggregate members; duplicate matching
  member types select the first member. Static images accept arithmetic and
  relocatable pointer members, while aggregate-valued casts remain
  nonconstant, matching GCC. ISO modes emit the GCC-compatible pedantic
  diagnostic `ISO C forbids casts to union type`, promote it under
  `-pedantic-errors`, and honor `__extension__` suppression. Apple ARM64 and
  Kasumi both pass all 24 semantic tests / 355 assertions and the two focused
  program fixtures. Kasumi passes the full 828 unit tests / 4,294,072
  assertions; native Apple ARM64 and Kasumi x86 execution passes at
  O0/O1/O2/O3/Os, both Linux backends compile the repaired torture cases at
  all five levels, and GCC 16 independently passes the runtime oracle while
  rejecting the deliberately nonconstant static aggregate boundary. Adding
  the two fixture sources intentionally changes the deterministic frontend
  fuzz corpus; Apple and Kasumi independently reproduce digest
  `1c9763a611841567`, and the 2,000-iteration smoke has zero findings.
  Implementation head `36cc1587f2d82ab6daaf01a14e71b2e8d3124514`
  passes every non-torture job in pre-publication PR CI
  [run 33299790343](https://github.com/tenseleyFlow/Cgfried/actions/runs/33299790343),
  including sanitizers and the 100k frontend fuzz lane; torture refuses
  exactly the ten expected uncommitted x86 cells. Push and PR bootstrap runs
  [33299782548](https://github.com/tenseleyFlow/Cgfried/actions/runs/33299782548)
  and
  [33299790338](https://github.com/tenseleyFlow/Cgfried/actions/runs/33299790338)
  both pass O0/O2. Exact-head native full-lattice
  [run 33299836646](https://github.com/tenseleyFlow/Cgfried/actions/runs/33299836646)
  passes every other job and refuses only the matching ten ARM64 cells. The
  retained ARM stream (artifact `9728741604`) has SHA-256
  `f560fd721e77fe19f474bbd5160ab3fd57a3b400bfbe8efb6687974ad4ee2043`.
  The hosted PR x86 stream (artifact `9728786023`) has SHA-256
  `48855d239e8778275e2c022fd0584ec2156408b674a425d191bab4772817c369`
  and independently confirms the same ten promotions with zero regression;
  it is confirmation-only because Actions checked out GitHub's synthetic
  merge revision. Formal Kasumi publication regenerated the exact branch-head
  x86 stream as
  `964b41bd6b047692ec57f58fcafd76720f54338f06dfa2420057feb06b69de8b`.
  The two authoritative streams name the implementation head and share
  compiler-source SHA-256
  `134e083558f2ab8698f455e2c76a12e70be97e818095a940b1f6c1473d196be0`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  torture-manifest SHA-256
  `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`,
  and ctestsuite-manifest SHA-256
  `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`.
  Formal `make torture-baseline` promotes exactly twenty cells with zero PASS
  regression and retires fingerprints
  `087e22d20269bf72f0f467ee68df2ecc04c2ceeb7dd9d73b38b716c761f5b1cb`
  and
  `ae6394ea751369cfa3f15c078e72a70e5989fc74012072b928877865cabac9cb`.
  Publication yields 26,795 PASS cells, 7,235 classified failures, 65 buckets,
  56 applied decisions, zero stale/unresolved decisions, and 32 live repair
  rows representing 28 unique compiler tranches. Reversing the two evidence
  streams regenerates both outputs byte-identically; PASS and triage SHA-256
  values are respectively
  `38e1cd03ad031062b53ce242f802f24833ed8d5489b96e66ff4e0a4207cb93a9`
  and
  `b2f9361018d0febdb5e0d2ad30c486abeaeb4ed22820a6fcf1691ee6fee9ab32`.
  PR #57 merged as `9b5d04d1`; the next tranche is the isolated
  `pr83222.c` const-scalar-object static-initializer folding gap.
- PR #59 isolated and repaired the prerequisite exposed while recapturing
  exact evidence for that next tranche. `finish_symbol()` recursively walked
  the parser's newest-first symbol chain before reversing it; the imported
  100,000-declaration limit therefore exhausted the host stack on Darwin
  ARM64 and could also corrupt the diagnostic location on Linux. The repair
  accumulates the chain in an explicit `SymbolVec` and finalizes it
  iteratively in declaration order, with a 100,000-symbol unit regression.
  Apple ARM64 passes the 25-test semantic suite / 357 assertions and 50/50
  repeated limit compiles. A Clang-built x86 compiler passes 100/100 repeated
  compiles and the 829-test / 4,294,074-assertion unit suite; a GCC 15 build on
  Hasu passes another 100/100. Focused ASan/UBSan and pinned clang-format 22
  are green. PR CI
  [run 33325396417](https://github.com/tenseleyFlow/Cgfried/actions/runs/33325396417),
  including the complete x86 torture matrix and 36-minute frontend fuzz job,
  and both O0/O2 bootstrap runs are green. PR #59 merged as `ecc5cd36` from
  implementation commit `ca8cf5e0`; PR #58 is rebased onto that prerequisite.
- The merged `s56.5-constexpr-context-decomposition` PR #58 first source-
  audited fingerprint
  `719f040fbd028be5e8a9f8add4109a1d3215b8a6e44d8084de50159c6182fe7c`
  into three independent ten-cell gaps: `pr83222.c` const scalar objects in
  later static initializers, `pr38789.c` immediate asm validation before
  `__builtin_constant_p` branch elimination, and `pr41935.c` runtime VLA
  indices in `__builtin_offsetof`. This tranche implements only `pr83222.c`.
  Semantic analysis retains an already-typed initializer only for a top-level
  const, nonvolatile, non-atomic arithmetic or pointer object after that
  initializer folds; later non-ICE constant contexts may reuse its value. A
  distinct opportunistic VLA-folding mode keeps const object names out of
  enum values, case labels, and fixed array bounds. GCC 16 confirms the same
  acceptance and rejection boundary. Apple ARM64 passes 25 semantic tests /
  359 assertions and native runtime execution at O0/O1/O2/O3/Os. Rebased PR
  CI passes 830 unit tests / 4,294,078 assertions, every ordinary corpus and
  policy gate, and 100,000 sanitizer-backed frontend-fuzz iterations with
  zero findings. Both Linux backends compile `pr83222.c` at all five levels.

  Implementation head `1ac926f56a611b433f9ddcd365bd49a4beb8c10a`
  passes every non-torture job in pre-publication PR CI
  [run 33327197597](https://github.com/tenseleyFlow/Cgfried/actions/runs/33327197597);
  hosted torture refuses exactly the five expected uncommitted x86 cells.
  Push and PR bootstrap runs
  [33327196820](https://github.com/tenseleyFlow/Cgfried/actions/runs/33327196820)
  and
  [33327197606](https://github.com/tenseleyFlow/Cgfried/actions/runs/33327197606)
  both pass O0/O2. Exact-head native full-lattice
  [run 33327204072](https://github.com/tenseleyFlow/Cgfried/actions/runs/33327204072)
  passes all fourteen other jobs and refuses only the matching five ARM64
  cells. Its retained ARM stream (artifact `9736778213`) has SHA-256
  `141d01ae8cd59800f2ed18d5006f30064d76df17a6ab27d81b1b3900f6d19bb0`.

  Hasu's NixOS deployment has no FHS headers, CRT, or default binutils paths;
  a bare-host capture was therefore rejected by the old-PASS gate rather than
  mistaken for compiler evidence. Formal publication uses the repository's
  argv-preserving sysroot wrapper over a coherent GCC 15 glibc target root and
  explicit native assembler/linker paths. Its validation capture and fresh
  `make torture-baseline` run regenerate the exact branch-head x86 stream
  byte-identically as
  `2c88bacc6d677f2ca3fb93afb11ed0093ea676558cd66512014dca6a9bb6281b`.
  The authoritative streams name the implementation head and share compiler-
  source SHA-256
  `12ee678b9b2e24d02cd90c405202836818fa2d875ff48212ef152bd94911af8b`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  and the unchanged manifest hashes; their ARM and x86 compiler binary hashes
  are respectively
  `07530b6ed35a9c64c1fb6726dd9d2b445e6dd1c5d96334febb7cf7447edca052`
  and
  `66db8d56b9f82738e379972e5268f4b400659c4ae3b6b1f81a7b2de39ba57b22`;
  the corresponding driver hashes are
  `07530b6ed35a9c64c1fb6726dd9d2b445e6dd1c5d96334febb7cf7447edca052`
  and
  `da88a93bd43c72a6aaf081150b28a8154bbec87322e05faac5239135d0747bdb`.
  Formal `make torture-baseline` promotes exactly ten `pr83222.c` cells with
  zero PASS regression. The old fingerprint remains a 20-cell bucket for the
  two source-audited gaps above. Hasu's required wrapper makes two existing
  identifier-limit timeouts normalize with a `scripts/` command path; that
  host-specific fingerprint is separately classified under the same compiler-
  scalability tranche. Publication is 26,805 PASS cells, 7,225 classified
  failures, 66 buckets, 57 applied decisions, zero stale/unresolved decisions,
  and 32 live repair rows representing 28 compiler tranches. Reversing the
  two evidence streams
  regenerates both outputs byte-identically; PASS and triage SHA-256 values
  are respectively
  `4d4ad8bbe70285e29aa3b4f557eb1ba74ef6d694cb7ccc17168709803e58bcde`
  and
  `59a4dbae2157719a612d49d1b978d53497140903ee2d331d1a0475a2f6368154`.
  PR #58 merged as `ea708db7` from publication head `3ae4b471`; its successor
  is the `pr41935.c` runtime VLA `__builtin_offsetof` tranche.
- The `s56.5-runtime-vla-offsetof` tranche (PR #60) implements GNU runtime
  array indices in `__builtin_offsetof`, retiring the isolated `pr41935.c`
  gap. Fully constant designators still fold as `size_t` integer constant
  expressions; runtime indices are ordinary expressions and remain rejected
  where an ICE is required. Lowering recursively accumulates static member
  offsets and each evaluated array stride as integer byte arithmetic, retaining
  source-order index effects and VLA strides without materializing a pointer or
  emitting an object-bounds guard. Negative and out-of-range indices therefore
  remain arithmetic-only, while existing bit-field, bad-member, and bad-first-
  type diagnostics are preserved.

  Final behavior head `cb66dda41fc4ce73b93102c071cd10e281373d61` also repairs
  an independent pre-existing semantic hole exposed by the enlarged frontend
  fuzz corpus: postfix `++`/`--` now enforce the same arithmetic-or-pointer
  requirement as prefix operators, so record lvalues produce an ordinary source
  diagnostic rather than reaching lowering and ICEing. The permanent
  `err_postfix_record_incdec.c` fixture covers both operators. The fixture set
  changes the deterministic 5,000-iteration corpus pin to
  `fb220f5b6b11282e`; CI's ASan/UBSan 100,000-iteration fixed-seed run reports
  zero findings and an empty crash-reproducer check.

  Final five-level evidence uses the same behavior head on both targets. The
  native ARM64 matrix [run 33347735075](https://github.com/tenseleyFlow/Cgfried/actions/runs/33347735075)
  artifact stream has SHA-256
  `a5afb18b27ce93fbd7f9d1a74b7d25d1d065b5810763b1ac3d04e8098874fb6c`;
  the fresh sysroot-corrected Hasu x86-64 stream has SHA-256
  `7688d80ce8dadbea502d30923e292a14cdec2b3b269740d29edddb9b81108c77`.
  Both share compiler-source SHA-256
  `85cec107be41cc17387bd45fb7cc140517c72f78cfaba177e974d81e8c5697fb`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  and unchanged manifest hashes. ARM's compiler/driver hash is
  `2a4db38fac974d7ea268e72c674a19c2a9cc2f0fb0cc1385730750f10292c9df`;
  x86's compiler and driver hashes are respectively
  `9279920b7f8f0fd910400d68b368d0fa92082cdd52ad375b544a2a46764699a3`
  and `da88a93bd43c72a6aaf081150b28a8154bbec87322e05faac5239135d0747bdb`.
  Each target contributes exactly its five `pr41935.c` execute passes, with no
  regression. Formal `make torture-baseline` publishes 26,815 PASS cells,
  7,215 classified failures, 67 buckets, 58 applied decisions, and zero stale
  or unresolved decisions. The fresh x86 host again reaches the documented
  256-level bracket-nesting limit for `limits-exprparen.c`; its prior durable
  `out-of-scope` policy row is restored rather than treated as a new compiler
  gap. Reversed evidence-stream order regenerates the PASS ledger and triage
  audit byte-identically; their SHA-256 values are respectively
  `e9b25a92b4412b0f388a6f0bcb6bff44f16b37239fc3518ec3625ed8a93655dc`
  and `cbe920e7bcfd93441af82f75dcbc7e4a91879906b11a54fdd36c2e8be642d1f4`.
  The next isolated compiler tranche is `pr38789.c` immediate-asm timing.
- The `s56.5-immediate-asm-constant-p-timing` tranche (PR #61) resolves that
  `pr38789.c` gap. Statement lowering now recognizes direct
  `__builtin_constant_p` conditions (including parens, implicit casts, and
  `!`) using the same lowering-time constant and va-pack specialization rules
  as the builtin expression. It emits the known CFG edge before target-aware
  asm validation, while still lowering both source arms so labels, gotos, and
  switch entries can retain a syntactically dead arm when it is reachable by a
  real edge. Immediate-operand diagnostics are deferred until the completed
  CFG proves their asm block reachable; macro configuration
  provenance is likewise recorded after reachability rather than erased by
  early folding. The associated IR asm table is canonicalized in print order,
  so `-emit-ir` round-trips functions whose source/lowering order differs from
  their final CFG layout.

  The permanent fixtures cover the dead immediate arm, a live nonconstant
  immediate rejection, a label-reachable dead arm, and IR round-tripping.
  Apple-silicon targeted builds and both Linux code-generation targets pass the
  focused O0/O1/O2/O3/Os checks. Exact behavior head
  `3b55af5d5d1daa0b74e34e92fc4ca73c72114f32` passed every non-torture job in
  pre-publication PR CI
  [run 33357926160](https://github.com/tenseleyFlow/Cgfried/actions/runs/33357926160),
  including the full ordinary and sanitizer suites, macOS/ARM, native/QEMU
  ARM, toolchain and campaign lanes, and 100,000 ASan/UBSan frontend-fuzz
  iterations with zero findings and a clean crash ledger. Its x86 torture gate
  refused only the six uncommitted x86 cells. Exact-head native ARM64 lattice
  [run 33358002679](https://github.com/tenseleyFlow/Cgfried/actions/runs/33358002679)
  likewise refused only the matching six ARM cells; its retained stream SHA-256
  is `b8574cf1fa34ed539aec1a13b315e2a107cf43fe8f8ccbfc54d7a950a29c04bc`.

  The publishable Hasu x86-64 stream has SHA-256
  `d1606b67845395e6e5f11bcd3b044f62d6e4880af80d7fb194c0d663758ecbb7`.
  It and the native ARM stream share source revision `3b55af5`, compiler-source
  SHA-256 `22a860a0f33b8c59954ab3650195ea263bf1973cfd5dc500cde2f7769c1e57b7`,
  harness SHA-256 `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  and both manifest hashes. The ARM compiler/driver SHA-256 is
  `c7c8eb9787e95c50bc5c2a95165179476f6e4a48b0bcb38c95d86437537ab27d`;
  x86 compiler and driver hashes are respectively
  `4121da44792b21c688322e64d5be36937408dfd5a2e28b20d3abfb0dd6d7ffec`
  and `da88a93bd43c72a6aaf081150b28a8154bbec87322e05faac5239135d0747bdb`.
  The first target-complete baseline attempt surfaced no compiler movement but
  rekeyed the pre-existing Hasu identifier-limit timeout because the absolute
  sysroot-wrapper path normalizes differently: stale
  `f3ebc21b...` is replaced by `add5d1b...` under the same
  `fix-sprint:s56.5-compiler-scalability-timeout` disposition. The required
  end-to-end `make torture-baseline` rerun then completed cleanly.

  Publication promotes exactly twelve cells with zero PASS regression: the
  five `torture-compile/pr38789.c` levels on each target, plus the O0
  `torture-execute/20030330-1.c` cell on each target. The latter is the same
  lowering improvement deleting an unreachable `link_error` reference; the
  still-failing `medce-1.c` keeps the broader dead-code-link policy live. The
  committed output has 26,830 PASS cells, 7,203 classified failures, 66
  buckets, 57 applied decisions, and zero stale or unresolved decisions. The
  immediate-asm policy fingerprint `719f040f...` is retired. Reversing the two
  identical-provenance input streams regenerates both outputs byte-identically;
  PASS and triage SHA-256 values are respectively
  `f2bf6dfbbc6510429b852e67f621c3bb287e1ed2fcaa684b2e2914b6f5d37cdc`
  and `2ca63462e5df48690b37686e122ea2db0544320a41a776f5dc11db58a96231a3`.
  Fresh post-publication PR and native-ARM CI must be green before merging.
- The `s56.5-label-reachable-dead-regions` tranche (PR #62) resolves the
  isolated `ctestsuite/00213.c` gap: gotos can now enter labels embedded in
  syntactically dead GNU statement expressions and loop regions. The lowering
  label prepass recurses through expression children and declaration
  initializers, including statement-expression bodies. A terminated typed
  statement expression now materializes a typed `undef` and a dead
  continuation block, preserving the real terminator while leaving a valid
  target for a precollected label. The permanent GNU fixture covers entry into
  a dead statement expression, terminated-expression cleanup, and the outer
  conditional; focused lowering tests pin both the prepass and typed
  continuation behavior. Its corpus addition re-pins the deterministic
  frontend-fuzz digest.

  Behavior head `6b773a8046440aa976d3bdcf42f88a95b2263111` passed all
  non-torture PR checks in
  [run 33413949242](https://github.com/tenseleyFlow/Cgfried/actions/runs/33413949242);
  its hosted x86 gate reported only the five uncommitted `00213.c` PASS cells.
  Exact-head native ARM64
  [run 33414115169](https://github.com/tenseleyFlow/Cgfried/actions/runs/33414115169)
  reported only the matching five ARM cells. The bare Hasu Nix capture was
  rejected because it did not use a coherent target root; the repository's
  argv-preserving `fleet-cgf-sysroot.sh` wrapper, explicit native assembler and
  linker, and GCC-derived glibc sysroot then produced the publishable exact-head
  x86 stream. Its SHA-256 is
  `0845d48c7ba8c2d50e13465b6de3046f903b3aec9de26fb1d5e9bca6d39ae2c0`;
  the native ARM stream SHA-256 is
  `ed689e321cec65d7972c5767836a693e6a2774c0a70785a2ae011fef2d2c2d66`.
  They share the behavior revision, compiler-source SHA-256
  `03546ecf9dede1f7ea626c886b5ba3acfcfbbed056418ee2a43cf7613160c802`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  and both manifest hashes. A fresh end-to-end `make torture-baseline` run
  atomically promotes exactly the ten `00213.c` target-level cells with zero
  PASS regression. It retires fingerprint `616e5e0e...` and the independently
  stale `e52eb570...` parse-limit decision: both targets now classify that
  stress case through the existing generic `cg` output-limit policy. The
  published state is 26,840 PASS cells, 7,193 classified failures, 64 buckets,
  55 applied decisions, and zero stale or unresolved decisions. Reversing the
  two evidence streams regenerates both outputs byte-identically; PASS and
  triage SHA-256 values are respectively
  `02d899a3d64c62de969bdacd27106282830466a3c700e8c069b817a224b2050d`
  and `1ca9959ee675fea7a23e593e9cd9670172cfe9cf0ad40d2456d9fcd0fb0283e3`.
  Post-publication standard CI
  [run 33440592643](https://github.com/tenseleyFlow/Cgfried/actions/runs/33440592643)
  and exact-head native ARM64
  [run 33440632683](https://github.com/tenseleyFlow/Cgfried/actions/runs/33440632683)
  are fully green. PR #62 merged as `5cf4bbf`.
- The `s56.5-wide-character-pp-constant` tranche (PR #63) resolves the
  isolated `torture-execute/widechar-1.c` gap. Preprocessor character-constant
  evaluation now preserves decoded code-unit values for `L`, `u`, and `U`
  prefixes instead of narrowing every constant through a signed byte.
  Prefixed multicharacter constants retain the final decoded code unit and
  warn, matching the ordinary lexer and GCC behavior, while ordinary
  multicharacter packing is unchanged. Focused preprocessor unit coverage pins
  `L'\400'`, `L'\x100'`, `u'\400'`, `U'\400'`, and `L'ab'`; a permanent
  program fixture pins preprocessing/ordinary-lexer agreement and imported
  macro structure. The new corpus member re-pins the deterministic frontend
  fuzz digest.

  Apple-silicon validation builds the native compiler, passes all 57 native
  preprocessor fixtures, the focused 72-assertion expression suite, both
  differential modes, sanitizer frontend checks, and 5,000 fixed-seed
  sanitizer fuzz iterations with a clean crash ledger. Cross-target source
  emission passes for x86-64 and ARM64 at O0/O1/O2/O3/Os, and the imported
  `widechar-1.c` executable passes natively on Darwin ARM64. Behavior head
  `af3481a7a3e2a994355f67a1f1377e3be8a4d559` passed every non-torture PR
  check in
  [run 33447732579](https://github.com/tenseleyFlow/Cgfried/actions/runs/33447732579);
  its hosted x86 gate refused only the five newly passing `widechar-1.c`
  cells. Exact-head native ARM64
  [run 33447767719](https://github.com/tenseleyFlow/Cgfried/actions/runs/33447767719)
  refused only the matching five ARM cells.

  The publishable exact-head x86 stream was regenerated in the dedicated
  Ubuntu 24.04 x86-64 QEMU verification guest and has SHA-256
  `009721f67d859bd0cddc13a91829b97e2b78d23a1efcd5d94c987421b336b98e`;
  the native ARM stream SHA-256 is
  `826127d6ef52813c9498c15305676f5e9842b3e2b151a430c47d038d763fa5e3`.
  Both streams contain all 20,325 target cells and share the behavior
  revision, compiler-source SHA-256
  `5dfa0e371575c077a1986b48867a51f02bf0eff346145ac684a085c5d51807cc`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  and unchanged manifest hashes. The x86 compiler/driver SHA-256 is
  `b4defb6814c2ea41d1c31fe2aa53f47b6d3164b0b9e22424870a8800810bd471`;
  ARM's is
  `caf00d66d9eb53d64aa05073f8fae6a1218050c3c4eb21868fd54332ee0ff1ec`.

  Target-complete publication promotes exactly the ten `widechar-1.c`
  target-level cells with zero PASS regression and retires fingerprint
  `34623465...`. The coherent QEMU stream restores the documented
  `e52eb570...` bracket-depth translation-limit class and replaces the stale
  Hasu-only `add5d1b...` path-derived timeout with `bf558482...`, the same
  existing 100,000-case-label scalability gap surfaced as SIGSEGV in the
  constrained guest. The published state is 26,850 PASS cells, 7,183
  classified failures, 64 buckets, 55 applied decisions, and zero stale or
  unresolved decisions. Reversing the evidence streams regenerates both
  outputs byte-identically; PASS and triage SHA-256 values are respectively
  `28122dd67e086353d0f9651aa16024a55cb84dcfc5cf3e279574b529cb2761b0`
  and `7dc4306bea6c7290f0ba28b246ce381ebb4761f038d9473eaab8f43db66a3100`.
  Post-publication standard CI
  [run 33466095214](https://github.com/tenseleyFlow/Cgfried/actions/runs/33466095214)
  and exact-head native ARM64
  [run 33466098793](https://github.com/tenseleyFlow/Cgfried/actions/runs/33466098793)
  are fully green. PR #63 merged as `a8a23acd`.
- The `s56.5-composite-array-bound` tranche (PR #64) resolves the isolated
  `torture-compile/20001018-1.c` gap. An explicit block-scope `extern` that
  redeclares a visible linked entity now gets the compatible composite type
  in its own scope. A prior sized array therefore supplies the inner
  incomplete array's bound, while an inner completion deliberately does not
  mutate the spelling visible again after that scope ends. Incompatible
  visible array, object, and function redeclarations now diagnose rather than
  being silently accepted; compatible old-style/prototype function
  composition, internal linkage inheritance, and no-linkage shadowing retain
  their measured behavior. Entity-wide reconciliation between declarations
  in sibling blocks remains a separate compiler gap because neither
  declaration is visible at the other.

  Apple-silicon validation builds the native compiler, passes the focused
  16-assertion linkage test, all 26 semantic unit groups / 368 assertions, and
  all 36 native semantic program fixtures. Clang accepts the positive C17
  oracle and rejects all three negative redeclarations. The imported exemplar
  and permanent fixture emit x86-64 and ARM64 source at O0/O1/O2/O3/Os.
  Deterministic frontend fuzzing reports zero findings and matches digest
  `28abc9975c254c14`; ASan+UBSan pass the semantic suite, the strengthened
  focused case, syntax fixtures, a 2,000-iteration frontend run, and a final
  500-iteration replay. Imports, crash ledger, policy, seam, target, coverage,
  registry, and formatting gates are clean; the exact clang-format 22 check
  remains CI-owned because the Apple host has clang-format 23.

  Behavior head `b38a86bf03da244d0dbadcd28ef0ef4ca5740693` passed every
  non-torture PR job in
  [run 33469517438](https://github.com/tenseleyFlow/Cgfried/actions/runs/33469517438);
  hosted x86 torture refused only the five newly passing `20001018-1.c`
  cells. Exact-head native ARM64
  [run 33469541732](https://github.com/tenseleyFlow/Cgfried/actions/runs/33469541732)
  refused only the matching five ARM cells. GitHub's hosted PR stream records
  a synthetic merge revision, so the publishable x86 stream was regenerated
  at the exact behavior head in the dedicated Ubuntu 24.04 x86-64 QEMU
  verification guest.

  The exact x86 and native ARM streams have SHA-256 values
  `e0bf994d64e748647de53335016117762264427b6099aa7d95187fd6802a5f1b`
  and
  `763965b4fb7c9276ce31e4f845adfde9acb817cf91a76ff1df8998edb5e931f8`.
  Each contains all 20,325 target cells. They share behavior revision
  `b38a86bf`, compiler-source SHA-256
  `8c59f4908a00f54341e2b47fcc4fdf54750dd711bbb6e86439915ac413b0ac4b`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  and unchanged manifest hashes. The x86 compiler/driver SHA-256 is
  `475f0761dfd36305b65f2028f4e24a80a20bcabf9314a04d6e1560e30d5269a8`;
  ARM's is
  `352bad7009c5bd3ea7a65c5f8072bfffc409efc9ec32e4a208120bb05792e6bd`.

  Target-complete publication promotes exactly the ten `20001018-1.c`
  target-level cells with zero PASS regression and retires fingerprint
  `f3a8a201...`. The published state is 26,860 PASS cells, 7,173 classified
  failures, 63 buckets, 54 applied decisions, and zero stale or unresolved
  decisions. Reversing the evidence streams regenerates both outputs
  byte-identically; PASS and triage SHA-256 values are respectively
  `1cb02241a6b32fb44232c6831546dc94ba6d3517caeb9fc1602d9523c7dde5aa`
  and `541bd4e238f8c6648302dff89965dd37b3fcd43a465eace3b7496560e5015d91`.
  Post-publication standard CI and exact-head native ARM64 were fully green.
  PR #64 merged as `1b8ec6ae`. The next evidence-led compiler candidate was
  `s56.5-unprototyped-call-ir`.
- The `s56.5-unprototyped-call-ir` tranche (PR #65) resolves the isolated
  `torture-compile/pr71109.c` gap. Lowering now obtains a direct call's
  function type from the declaration visible at that call site rather than
  rereading the symbol's final whole-translation-unit composite type. An
  explicit IR `unproto` call flag preserves that provenance through text
  round trips: verification leaves the default-promoted argument count and
  types unconstrained while still checking a hidden return pointer, and
  inlining/IPO conservatively decline transformations that require a fixed
  signature. x86-64 emits the SysV `%al` XMM-register count for an
  unprototyped call; Apple ARM64 deliberately retains ordinary register
  placement rather than variadic stack placement. The marker is per call and
  remains distinct from both an old-style function definition and a variadic
  prototype.

  Apple-silicon validation passes 108 affected unit tests / 856 assertions,
  the permanent lowering and x86 MIR fixtures, and x86-64/ARM64 source
  emission for the imported case at O0/O1/O2/O3/Os. Unsanitized and
  ASan+UBSan frontend fuzz runs each complete 2,000 iterations with zero
  findings; the affected sanitizer suites, import/policy/seam/registry gates,
  and the pinned clang-format 22 check are green. Behavior head
  `f5a1dfd870cf4212ad5576ccff7f88f7a1b59d61` passed every non-torture job in
  [run 33532567867](https://github.com/tenseleyFlow/Cgfried/actions/runs/33532567867),
  including the 100,000-iteration frontend fuzz lane; hosted x86 torture
  refused only the five new `pr71109.c` PASS cells. Exact-head native ARM64
  [run 33534736755](https://github.com/tenseleyFlow/Cgfried/actions/runs/33534736755)
  refused only the matching five ARM cells.

  The publishable x86 stream was regenerated at the exact behavior head in
  the dedicated Ubuntu 24.04 x86-64 QEMU verification guest. Its SHA-256 is
  `71f93b888f7c859544fde13637d9081a846407af9feae469ae443974d2539be0`;
  the native ARM stream SHA-256 is
  `bcd7a02b9f52ce78ccc1aa7bca649e4dbbab07f003f164767849e1f59dd11f9d`.
  Each stream contains all 20,325 target cells. They share the exact behavior
  revision, compiler-source SHA-256
  `212fc033d87d9f01902002f04ae3de2d63196f30c723fde381e44826e627a1b2`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  torture-manifest SHA-256
  `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`,
  and c-testsuite-manifest SHA-256
  `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`.
  The x86 compiler/driver SHA-256 is
  `3dd3414d12c81cea5a6a134e998007ddfa359ff287d578fb6a0e2494abbecdff`;
  ARM's is
  `519e47fe7f377da868f251668a801344e4fca6c80b51653b1acbe16feedf6b1f`.

  Target-complete publication promotes exactly the ten `pr71109.c`
  target-level cells with zero PASS regression and retires fingerprint
  `31dd1e91...`. The published state is 26,870 ratchet lines, 7,163
  classified failures, 62 buckets, 53 applied decisions, 28 live repair rows
  representing 23 repair tranches, and zero stale or unresolved decisions.
  Reversing the evidence streams regenerates both outputs byte-identically;
  PASS and triage SHA-256 values are respectively
  `59fa746409e5618b6b6cfd6366eb61c7545fb96a8834d1ccef3495e85bafffe3`
  and `3b07952881d3d53dec16edd5e190a03985590071789a17850455699d2ac09e00`.
  Post-publication standard CI
  [run 33546057571](https://github.com/tenseleyFlow/Cgfried/actions/runs/33546057571)
  and exact-head native ARM64
  [run 33546067582](https://github.com/tenseleyFlow/Cgfried/actions/runs/33546067582)
  are fully green. PR #65 merged as `3db23ef`. The next evidence-led candidate
  was `s56.5-x86-imm64-materialization`.
- The `s56.5-x86-imm64-materialization` tranche (PR #66) resolves the isolated
  `torture-compile/20030323-1.c` x86-64 gap. x86-64 `cmpq` and `subq` encode
  only sign-extended 32-bit immediates, so switch lowering now routes wide
  qword case constants through ordinary source materialization. The sparse
  `0x80000000` boundary uses a zero-extending `mov.l`; the dense true-wide
  `0x100000000` minimum uses `movabs.q`. Both materializations are deliberately
  emitted before the flag-defining compare or subtract. Non-qword immediates
  retain the existing direct-immediate path.

  Apple-silicon validation passes the eight affected x86 instruction-selection
  tests / 78 assertions, the permanent sparse/dense MIR fixture before and
  after register allocation, and source emission for both Linux targets at
  O0/O1/O2/O3/Os. Fifty of the 52 x86-named unit groups pass locally; the two
  exceptions are the already-documented Darwin ARM64 x87/long-double simulator
  limitation. Unsanitized and ASan+UBSan frontend fuzz runs each complete 2,000
  iterations with zero findings, and the affected sanitizer, import, policy,
  seam, verifier, registry, ban, closeout, and formatting gates are green.
  Behavior head `03a8d941ef999b93878f02317f2e998b9ba55962` passed every
  non-torture PR job in
  [run 33552023363](https://github.com/tenseleyFlow/Cgfried/actions/runs/33552023363),
  including 100,000 ASan+UBSan frontend-fuzz iterations; hosted x86 torture
  refused only the five newly passing `20030323-1.c` cells. Exact-head full
  lattice
  [run 33552075202](https://github.com/tenseleyFlow/Cgfried/actions/runs/33552075202)
  passed all 15 jobs and supplied the native ARM64 publication stream.

  The publishable x86 stream was regenerated at the exact behavior head in a
  disposable Ubuntu 24.04 x86-64 QEMU guest. Its SHA-256 is
  `d2fc063e8a274c61a4614e7f4067afed6281236295097b15db6bf6904ab03b0f`;
  the native ARM stream SHA-256 is
  `d540ccca6bc3aff8e78940288d2267c9182e7a750c53666af7aed82d121ac9a3`.
  Each stream contains all 20,325 target cells. They share the exact behavior
  revision, compiler-source SHA-256
  `bf947db09fabea77a2b290d610a4b9cd36ad5e8a2f123264c9df1d2a51199c46`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  torture-manifest SHA-256
  `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`,
  and c-testsuite-manifest SHA-256
  `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`.
  The x86 compiler/driver SHA-256 is
  `3b9245f8a2e15a4eabb9ba756da753b6722d670ba033b3c971ec1a7e88a4ffbc`;
  ARM's is
  `daba209554e1569b7410fbee0ba317b55f86d7c7d3b076225835fc34b59db86a`.

  Target-complete publication promotes exactly the five x86-64
  `20030323-1.c` cells with zero PASS regression and retires fingerprint
  `737ff185...`. Both exact streams now surface `limits-caselabels.c` through
  the already-live `de25f493...` timeout fingerprint, so the stale x86-only
  `bf558482...` direct-SIGSEGV variant is also retired. The published state is
  26,875 ratchet lines, 7,158 classified failures, 60 buckets, 51 applied
  decisions, 26 live repair rows representing 22 repair tranches, and zero
  stale or unresolved decisions. Reversing the evidence streams regenerates
  both outputs byte-identically; PASS and triage SHA-256 values are
  respectively
  `7fb713dd5feaad3d34d3ae1109ad826b4e0284d2a7be4bed3f158252faa51da2`
  and
  `c6ce2740c4219ae92613523323aa824112361fbb21e4093eb90ca1d05b5bd534`.
  Post-publication standard CI
  [run 33564164263](https://github.com/tenseleyFlow/Cgfried/actions/runs/33564164263)
  and exact-head native ARM64
  [run 33564217151](https://github.com/tenseleyFlow/Cgfried/actions/runs/33564217151)
  are fully green. PR #66 merged as `7b79b9ad`; the next evidence-led
  candidate was `s56.5-arm64-stacked-large-aggregate-abi`.
- The `s56.5-arm64-stacked-large-aggregate-abi` tranche (PR #67) resolves the
  isolated `ctestsuite/00204.c` ARM64 gap. Linux AAPCS64 permits an HFA of four
  binary128 leaves in v-registers. Once earlier arguments exhaust v0-v7, that
  same 64-byte value moves to the stack as ordinary bytes and needs eight
  eightbyte carriers. Cgfried incorrectly shared the architectural four-leaf
  HFA limit with this flattened stack form and ICEd before producing IR.
  Argument planning now separates the four register leaves from the eight
  stack carriers, preserves the first carrier's 16-byte NSAA alignment, and
  leaves the exhausted FP budget pinned.

  Apple-silicon validation passes 13 affected AAPCS64 tests / 224 assertions,
  the broader 36-test ABI suite / 630 assertions, the permanent target-specific
  IR fixture, and full `00204.c` ARM64 emission plus AArch64 ELF assembly at
  O0/O1/O2/O3/Os. The affected tests, fixture, five-level source matrix, and
  2,000 fixed-seed frontend fuzz cases also pass under ASan+UBSan. Unsanitized
  fuzz completes 2,000 cases with zero findings; three independent 5,000-case
  digest runs agree before the intended corpus repin. Import, policy,
  target-seam, verifier, registry, ban, closeout, diff, and pinned
  clang-format 22 gates are green. Behavior head
  `9d83f1015bcd3cadf0e352271cf776d92eae4817` passes every standard PR job in
  [run 33569350627](https://github.com/tenseleyFlow/Cgfried/actions/runs/33569350627),
  including full test, both ARM lanes, x86 torture, and 100,000 ASan+UBSan
  frontend-fuzz iterations.

  Exact-head full-lattice
  [run 33569374675](https://github.com/tenseleyFlow/Cgfried/actions/runs/33569374675)
  passes all 14 non-gate jobs and retains the native ARM64 stream; its gate
  rejects exactly the five newly passing `00204.c` cells and no regressions.
  The publishable x86 stream was regenerated at the same exact behavior head
  in the retained Ubuntu 24.04 x86-64 QEMU guest and passes its committed
  ratchet gate. The x86 stream SHA-256 is
  `3ecc9f3e857f046ad05363dea2670f68ddc9e0113211f074b25549b37833bd7a`;
  the native ARM stream SHA-256 is
  `0a8d7a62130fb5fcbcd9106952487b13cfed893a64b8b8b0024a7624913af38c`.
  Each stream contains all 20,325 target cells. They share the exact behavior
  revision, compiler-source SHA-256
  `2399a08118c83410357a71e432d90f6d08473af78dbf3388ca8aed38b2e19040`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  torture-manifest SHA-256
  `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`,
  and c-testsuite-manifest SHA-256
  `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`.
  The x86 compiler/driver SHA-256 is
  `fbc43707c14ca5a1461db6c61760834ac8cdd8d543cd1773814b4c11a1b6f58e`;
  ARM's is
  `8b3a28d642f76443868e8467d1ec202d5e230147e53850e31526133f6a3e21ac`.

  Target-complete publication promotes exactly the five ARM64 `00204.c`
  cells with zero PASS regression and retires fingerprint `759ab6b2...`.
  Both exact streams now surface the deep-expression limit through the
  already-covered output guard, so the stale host-specific `e52eb570...`
  parse-diagnostic variant is also retired without counting it as a compiler
  repair. The published state is 26,880 ratchet lines / 26,877 PASS keys,
  7,153 classified failures, 58 buckets, 49 applied decisions, 25 live repair
  rows representing 21 repair tranches, and zero stale or unresolved
  decisions. Reversing the evidence streams regenerates both outputs
  byte-identically; PASS and triage SHA-256 values are respectively
  `676280470bdce1ce9a06a7199e4548fcc31ca6bbdb721cebec767cd3322ad1c9`
  and
  `6bdef25c3ef444d913b52b5afb2245c941f8825246acd830528b2e5f9c18f14b`.
  Post-publication standard CI
  [run 33577031792](https://github.com/tenseleyFlow/Cgfried/actions/runs/33577031792)
  and exact-head native ARM64
  [run 33577037721](https://github.com/tenseleyFlow/Cgfried/actions/runs/33577037721)
  are fully green. PR #67 merged as `01d95ccc`; the next evidence-led
  candidate was `s56.5-x87-tied-double-operand-shape`.
- The `s56.5-x87-tied-double-operand-shape` tranche (PR #68) resolves the
  isolated five-cell x86 `torture-compile/pr34966.c` gap. GNU permits a
  binary32, binary64, or extended value to be tied to `st(0)` through either a
  read-write `+t` operand or the equivalent `=t` output plus numeric `0`
  input. Cgfried left the numeric input in its generic register class,
  validated only the long-double `+t` spelling, and hard-coded `fldt`/`fstpt`
  in x86 selection. Matching inputs now inherit the output's x87 class and
  width. The backend keeps memory-resident f80 values unchanged, spills f32
  and f64 inputs from SSE to a local slot, and emits width-correct
  `flds`/`fstps`, `fldl`/`fstpl`, or `fldt`/`fstpt` pairs.

  The permanent GNU fixture checks exact float and double assembly mnemonics
  and runtime bit preservation. Apple-silicon validation compiles and
  assembles the imported case at O0/O1/O2/O3/Os, preserves the existing f80
  constraint fixture, passes focused sanitized emission and assembly, and
  completes 2,000 sanitized plus 2,000 unsanitized frontend-fuzz cases with
  zero findings. Three independent 5,000-case sequences agree on the intended
  corpus digest repin. Import, ban, closeout, strict clang-format 22, and
  deterministic fuzz-smoke gates pass. In the Ubuntu 24.04 x86-64 verification
  guest, the new and existing x87 fixtures pass normally, with
  `CGF_SPILL_ALL=1`, and under ASan+UBSan. The full native unit suite reports
  836 tests / 4,294,160 assertions / zero failures; the broad `make test` gate
  reaches only the documented minimal-image absence of the Clang ppdiff
  oracle after all preceding lanes pass.

  Behavior head `ba0f28846b0d95c6b0bba72af89819a33ac6925b` passes every
  non-torture standard PR job in
  [run 33583393103](https://github.com/tenseleyFlow/Cgfried/actions/runs/33583393103),
  including 100,000 ASan+UBSan frontend-fuzz iterations; its x86 gate rejects
  exactly the five newly passing `pr34966.c` cells and no regression. Exact-head
  full-lattice
  [run 33583428581](https://github.com/tenseleyFlow/Cgfried/actions/runs/33583428581)
  is fully green and retains the unchanged native ARM64 stream. Because the PR
  checkout artifact names GitHub's synthetic merge revision, the publishable
  exact-head x86 stream was regenerated from the clean behavior receipt in the
  retained Ubuntu x86-64 guest. Five optimization-level shards use the
  repository's canonical runner with isolated work roots; their identical
  provenance headers and deterministically sorted rows form all 20,325 unique
  target cells.

  The x86 stream SHA-256 is
  `dbdf05c58a6f6812443cf960e7f5fbbff507e8d981b34a2adc2b207daa845032`;
  the native ARM stream SHA-256 is
  `22805ff9467295bf73da60ededf65f90d9294b1e7ef5f941370eac313c52977f`.
  They share the exact behavior revision, compiler-source SHA-256
  `cfa83d4489ce65b5be8fce2a67dc4d75779d6f895a85b9c6999aaae50adeafd2`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  torture-manifest SHA-256
  `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`,
  and c-testsuite-manifest SHA-256
  `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`.
  The x86 compiler/driver SHA-256 is
  `a2936e9317ec6101f5a4534a0c532732a5406358bec9c0376e5fc3ad1e78d3e0`;
  ARM's is
  `2e6033becdf06770a3f47cca0ac3a19dfb596ee967f0c8401a588a13ec3e98c5`.

  Target-complete publication promotes exactly the five x86 `pr34966.c`
  cells with zero PASS regression and retires fingerprint `b97306b0...`. The
  published state is 26,885 ratchet lines / 26,882 PASS keys, 7,148 classified
  failures, 57 buckets, 48 applied decisions, 24 live repair rows representing
  20 repair tranches, and zero stale or unresolved decisions. Reversing the
  evidence streams regenerates both outputs byte-identically; PASS and triage
  SHA-256 values are respectively
  `d86997a0a9e993a0d659719f77f2a17629c516b41801965cf855cb094121c773`
  and
  `d4b605debe22cdbe1ee5abd8332b1267c7964d2298d360c973db2e2a475aaaaa`.
  Post-publication standard CI
  [run 33587631250](https://github.com/tenseleyFlow/Cgfried/actions/runs/33587631250)
  and exact-head native ARM64
  [run 33587641895](https://github.com/tenseleyFlow/Cgfried/actions/runs/33587641895)
  are fully green. PR #68 merged as `a80346d5`; the next evidence-led
  candidate was `s56.5-dead-code-link-elimination`.
- The `s56.5-dead-code-link-elimination` tranche (PR #69) resolves the exact
  two-cell `torture-execute/medce-1.c@O0` gap on x86 and ARM. The source uses
  nested constant control flow around an independently reachable switch case;
  Cgfried retained an entry-unreachable block containing a deliberately
  undefined `link_error` call at O0 even though its optimized levels already
  removed the reference. A narrow mandatory `OPT_PASS_PRUNE_CFG` pass now
  folds only exactly provable constant terminators and deletes only blocks
  unreachable from function entry at every optimization level. Full
  straight-line and diamond merging remains in the O1+ `simplify_cfg` pass,
  and independently reachable labels and case entries remain preserved.

  The permanent regression checks the imported shape at every optimization
  level and requires no `link_error` reference in x86 Linux, ARM64 Linux, or
  ARM64 macOS assembly. A focused pass-manager unit covers dead-link pruning
  alongside preservation of a separately reachable case entry. Native Apple
  ARM64 fixture, optimizer/lowering unit, sanitized focused, assembly, and
  frontend-fuzz validation is clean. The retained Ubuntu x86-64 guest reports
  837 unit tests / 4,294,170 assertions / zero failures and passes the fixture
  normally, with `CGF_SPILL_ALL=1`, and under focused ASan+UBSan. Three
  independent 5,000-case frontend sequences agree on the intended digest
  repin. The only local broad-test limitations remain the documented Apple
  x87 simulator `long double` assumption and a QEMU wall-clock timeout on an
  existing heavy preprocessor fuzz seed; neither produces a compiler or
  sanitizer finding.

  Corrected behavior head `3a15afd338ed62d0722f2be4e02a8bbf7dd1bbc6`
  passes every non-torture job in standard PR
  [run 33595484569](https://github.com/tenseleyFlow/Cgfried/actions/runs/33595484569),
  including 100,000 ASan+UBSan frontend-fuzz iterations; its x86 gate rejects
  exactly the new O0 `medce-1.c` PASS and no regression. Exact-head nightly
  [run 33595474814](https://github.com/tenseleyFlow/Cgfried/actions/runs/33595474814)
  passes all 14 non-ratchet jobs; native ARM rejects exactly its corresponding
  O0 `medce-1.c` PASS and no regression.

  The clean behavior-head x86 stream was generated as five isolated
  optimization-level shards in the retained Ubuntu guest and contains all
  20,325 unique target cells. Its SHA-256 is
  `761a2642b4eda6db659f2be52a543e4d98b7d2aa684226ad2a37d59043af427f`;
  the exact native ARM artifact SHA-256 is
  `5bd507a07af43a30f09345d6084caf6c5d191c31443a5ea0fa9d218bd0f83f5a`.
  They share the behavior revision, compiler-source SHA-256
  `77e97982c9dd6d2c7dbf6cc5d18d0c7f6aebb45d3a35d2a642dda8c343692849`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  torture-manifest SHA-256
  `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`,
  and c-testsuite-manifest SHA-256
  `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`.
  The x86 compiler/driver SHA-256 is
  `e3bfdd5fc92b7ea9ca98293c9a57ad3d5bcd04bdeefc019ab5141ac54c18cb9d`;
  ARM's is
  `7a8c15bfcc6e03872acebd1cdca75a6de79510badeca3d8265f6c3f403be9c2f`.

  Target-complete publication promotes exactly the two O0 `medce-1.c` cells
  with zero PASS regression and retires fingerprint `aace2c03...`. The
  published state is 26,887 ratchet lines / 26,884 PASS keys, 7,146 classified
  failures, 56 buckets, 47 applied decisions, 23 live repair rows representing
  19 repair tranches, and zero stale or unresolved decisions. Reversing the
  evidence streams regenerates both outputs byte-identically; PASS and triage
  SHA-256 values are respectively
  `23d9d64d84e527a57844975a25cdfd697c4faf983ecc3b0a31d50a04c1bceca4`
  and
  `82fe9ed6485af11cd3dcbbb49a1e5adc8ff6bdeee48d8c474ee88bb3afbfcedc`.
  Fresh post-publication standard CI and exact-head native ARM64 must be green
  before merging. Select the next compiler-gap candidate from this newly
  generated triage only after those gates are green.
- The `s56.5-asm-rmw-single-evaluation` tranche (PR #70) resolves all ten
  target-complete `torture-execute/990130-1.c` cells. A synthesized input for a
  read-write extended-asm operand previously lowered the output lvalue a second
  time, so address-side effects such as `*bar()` ran twice. Asm lowering now
  caches the complete output `Lvalue` during the first operand pass and loads
  the tied input from that descriptor, preserving the chosen constraint,
  early-clobber, volatility, and memory behavior while evaluating the source
  lvalue exactly once. Read-write immediate-only output constraints retain an
  explicit validity error rather than manufacturing an invalid load.

  A focused lowering unit proves one address-producing call and one volatile
  load marker, and the permanent GNU program exercises the runtime result at
  all optimization levels. Native Apple ARM64 regression, focused units,
  target assembly generation, imported x86/ARM execution, and independent
  frontend-fuzz sequences are clean. Behavior head
  `91a0c9c18bb8728b86085b66f8bcaedd0db2db06` passes every non-torture job in
  standard PR
  [run 33614117401](https://github.com/tenseleyFlow/Cgfried/actions/runs/33614117401),
  including macOS ARM64, native and QEMU ARM, sanitizers, campaigns, and the
  100,000-case frontend-fuzz lane; its x86 gate rejects exactly the five new
  `990130-1.c` PASS cells and no regression. Exact-head nightly
  [run 33614160202](https://github.com/tenseleyFlow/Cgfried/actions/runs/33614160202)
  likewise rejects only the matching five native ARM64 cells while every other
  campaign job passes. Bootstrap O0/O2 jobs are green in push
  [run 33614081861](https://github.com/tenseleyFlow/Cgfried/actions/runs/33614081861)
  and PR
  [run 33614117349](https://github.com/tenseleyFlow/Cgfried/actions/runs/33614117349).

  The clean behavior-head x86 stream was generated in the retained native
  Ubuntu x86-64 guest, then regenerated byte-identically by the formal
  publication command. Its SHA-256 is
  `636b094c5c5d2ce37dabfd276a761d815fa348cb5ed7ea1afd753ccc64d274ea`;
  the exact native ARM artifact SHA-256 is
  `ec947e20c48609c39f2f3626669ebc95cf534a0760a24a022a48fd38a18dddb0`.
  Both streams contain all 20,325 target cells and share the behavior revision,
  compiler-source SHA-256
  `2852b9cd865238df406dc3a0e358506d7daad79ea340a91df6ccd7fc5411bbe7`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  torture-manifest SHA-256
  `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`,
  and c-testsuite-manifest SHA-256
  `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`.
  The x86 compiler/driver SHA-256 is
  `b3fd5d4767ba3bc18cdfae4f1a6e8a9cd05ef77e824e473e625e11528c5ac06d`;
  ARM's is
  `a2395a1ca99b8f11542f86803a53a1efea48612423fd87225bdb9b4da39faba2`.

  Formal target-complete publication promotes exactly the ten
  `990130-1.c` cells with zero PASS regression and retires fingerprint
  `a6031ed7...`. This x86 host reaches Cgfried's deliberate 256-level bracket
  nesting limit for `limits-exprparen.c` before the ARM harness output guard,
  so it restores the previously established `e52eb570...` `out-of-scope`
  host-variant policy row. The published state is 26,897 ratchet lines /
  26,894 PASS keys, 7,136 classified failures, 56 buckets, 47 applied
  decisions, 22 live repair rows representing 18 repair tranches, and zero
  stale or unresolved decisions. Reversing the evidence streams regenerates
  both outputs byte-identically; PASS and triage SHA-256 values are
  respectively
  `38f28b572cfa3853b2879ae6865ea9fe33f8fa2d82edc44109ce2797962e9254`
  and
  `a29bcb825c9998067c0467c474c1265a57a2da2482a446fde6b651c29507f407`.
  Fresh post-publication standard CI and exact-head native ARM64 must be green
  before merging and selecting the next compiler-gap tranche.
- The `s56.5-vla-typedef-size` tranche (PR #71) resolves all twenty
  target-complete `torture-execute/20040411-1.c` and `20041218-2.c` cells.
  Sema now retains the resolved operand type of `sizeof` and `_Alignof`, and
  lowering evaluates variably modified typedef extents at their declaration
  point. Runtime-sized record layout covers packed and explicitly aligned
  structures, unions, bit-fields, nested runtime records, tag declarations,
  and automatic objects whose outer type is a fixed array over a runtime-sized
  record. Function-definition parameter rebinding also reaches variably
  modified record members without disturbing ordinary fixed records.

  The broader repair was required by failures that the original isolated case
  exposed rather than created. `20040423-1.c` had passed because two incorrect
  runtime `sizeof` values cancelled; it now computes both values correctly.
  Hosted ARM found an ICE in `20020210-1.c` from a prototype-scope bound symbol,
  repaired by the targeted parameter rebind. Hosted x86 found nondeterministic
  stack corruption in the existing `align-nest.c` baseline because a fixed
  array over a runtime-sized record was prebound as one byte; those objects now
  take the dynamic-allocation path. The adjacent `20070919-1.c` record-copy
  failure remains reproducibly red at all five levels and is deliberately not
  promoted by this tranche.

  Behavior head `f6eccbef25b0c33dc4bb8118f26e0751f1ee3cfe` passes 843
  unit tests / 4,294,204 assertions, focused ASan+UBSan coverage, the native
  Apple ARM64 corpus fixture, target assembly generation, and all focused
  imported cases. `align-nest.c` passes 100/100 repeated Linux executions and
  50/50 Apple executions. Standard PR
  [run 33657658321](https://github.com/tenseleyFlow/Cgfried/actions/runs/33657658321)
  passes every non-ratchet job, including both ARM lanes, macOS, sanitizers,
  all campaigns, and 100,000-case frontend fuzzing; its x86 gate rejects only
  the ten unpublished x86 cells and no regression. Exact-head
  nightly
  [run 33657686111](https://github.com/tenseleyFlow/Cgfried/actions/runs/33657686111)
  likewise rejects only the matching ten native ARM cells while every other
  campaign job passes. Push and PR bootstrap runs
  [33657654673](https://github.com/tenseleyFlow/Cgfried/actions/runs/33657654673)
  and
  [33657658298](https://github.com/tenseleyFlow/Cgfried/actions/runs/33657658298)
  are green at O0 and O2.

  The retained native Ubuntu x86-64 stream was generated once as five
  independent shards and once by the formal sequential publication command;
  the two files are byte-identical with SHA-256
  `607d9208b7597f94168a3c41b3551a10413a8ba9e397598ef4fb4d8ee6efff04`.
  The exact native ARM stream SHA-256 is
  `6ef819033f36c930a239cc714c5cd13d9653469dbb14b6bddeb40bfea3b21e63`.
  Both streams contain all 20,325 target cells and share the behavior revision,
  compiler-source SHA-256
  `696f2d372d58b13b220922077da50d53ebb7ed817b62f8c91b62499cd0c7f302`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  torture-manifest SHA-256
  `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`,
  and c-testsuite-manifest SHA-256
  `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`.
  The x86 compiler/driver SHA-256 is
  `8c31e32940ec6e4e6745fdc0ce4cb7e439deff5e883ba3feb025f636d2287743`;
  ARM's is
  `375f67ce14bdbc0e1c6a076b72b037279ee8efb9294527dedc2e947f66b0d1ed`.

  Formal target-complete publication promotes exactly those twenty cells with
  zero PASS regression and retires fingerprints `72f0a8de...` and
  `df844223...`. The published state is 26,917 ratchet lines / 26,914 PASS
  keys, 7,116 classified failures, 54 buckets, 45 applied decisions, 20 live
  repair rows representing 16 repair tranches, and zero stale or unresolved
  decisions. Reversing the evidence streams regenerates both outputs
  byte-identically; PASS and triage SHA-256 values are respectively
  `78beb6d77318f29a84d518d84a94b4f3cc527da5e58ac18665b33098fc0496ae`
  and
  `5bd7e1c22547990b211fa579e19426ae2d39b25787d3ad861c44a5eb32313a5f`.
  Fresh post-publication standard CI and exact-head native ARM64 are green;
  PR #71 merged as `c8dd9f18`. The next selected compiler-gap candidate was
  `s56.5-builtin-llabs-semantics` (`20021127-1.c`, ten cells).
- The `s56.5-builtin-llabs-semantics` tranche (PR #72) resolves all ten
  target-complete `torture-execute/20021127-1.c` cells. A direct call to the
  hosted C99 library spelling now uses compiler intrinsic semantics when a
  compatible external `long long (long long)` declaration is visible; GNU89
  follows GCC's hosted extension. Strict C89, freestanding mode, incompatible
  declarations, TU-local functions, shadowing function-pointer objects, and
  address-taking retain ordinary symbol semantics. `__builtin_llabs` remains
  available independently of hosted mode, converts its argument by its real
  prototype, evaluates it exactly once, and folds as an integer constant
  expression without turning the plain library alias into an ICE. Runtime
  lowering is a signed compare/subtract/select sequence; `LLONG_MIN` retains
  the library operation's undefined-behavior boundary.

  Behavior head `960f7215d33619c260e1936e8a05d579b67166a6` passes 845 unit
  tests / 4,294,234 assertions, focused ASan+UBSan coverage, all five imported
  levels on native Apple ARM64 and Linux x86-64, and all 15 target/level
  cross-codegen cells for x86_64-linux-gnu, arm64-linux, and arm64-macos. The
  closed x86-64/SSE2 ISA matrix passes exactly 642 objects across 107 permanent
  corpus sources. Standard PR
  [run 33677822185](https://github.com/tenseleyFlow/Cgfried/actions/runs/33677822185)
  passes every non-ratchet job, including macOS ARM64, both Linux ARM lanes,
  sanitizers, all campaigns, and 100,000-case frontend fuzzing; its x86 gate
  rejects only the five unpublished `20021127-1.c` cells and no regression.
  Exact-head nightly
  [run 33677860932](https://github.com/tenseleyFlow/Cgfried/actions/runs/33677860932)
  likewise passes every campaign and rejects only the matching five native
  ARM cells. Push and PR bootstrap runs
  [33677779256](https://github.com/tenseleyFlow/Cgfried/actions/runs/33677779256)
  and
  [33677822225](https://github.com/tenseleyFlow/Cgfried/actions/runs/33677822225)
  are green at O0 and O2.

  The clean exact-head x86 stream SHA-256 is
  `f139ed772c5c046869b81c0e80fc25772978243e61b2bbb8b0ce98996882d5e7`;
  the native ARM stream SHA-256 is
  `4d44985c812ea918389b4694075cc5bd7a87811baef80922d4038a1fffc92689`.
  The formal x86 and PR-CI PASS sets are byte-identical. Their only ten row
  differences are the already-policy-covered host-capacity variants for
  `limits-caselabels.c` (timeout versus SIGSEGV) and `limits-exprparen.c`
  (output guard versus explicit bracket-limit diagnostic). Both exact-head
  target streams contain all 20,325 cells and share the behavior revision,
  compiler-source SHA-256
  `72928e7e63e0a2a3326566d90813b074dfa9ab77fd4374c3ebd1a30d7989a7db`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  torture-manifest SHA-256
  `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`,
  and c-testsuite-manifest SHA-256
  `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`.
  The x86 compiler/driver SHA-256 is
  `cbe511c2bdd284473acafaff269cb0567eb2ae9eb276bc9cd9b7fac6cdce45e1`;
  ARM's is
  `b16b504675183c6e50e075c7e019ebbb3fb806e09de2fbe70a40d85bab4972d1`.

  Formal target-complete publication promotes exactly those ten
  `20021127-1.c` cells with zero PASS regression and retires fingerprint
  `8b3f9d0d...`. The published state is 26,927 ratchet lines / 26,924 PASS
  keys, 7,106 classified failures, 53 buckets, 44 applied decisions, 19 live
  repair rows representing 15 repair tranches, and zero stale or unresolved
  decisions. Reversing the evidence streams regenerates both outputs
  byte-identically; PASS and triage SHA-256 values are respectively
  `53769be6dedc9ec10cee51f6a534aefd2bffbd1ca9de398ff7fd6afbff1ced97`
  and
  `94c72743206bd43b4fa985faa262e02ecb6e0a8ccdfb98839b95396dabf69686`.
  Fresh post-publication standard CI and exact-head native ARM64 are green;
  PR #72 merged as `2380c739`. The next recommended target-complete compiler
  gap is
  `s56.5-aligned-typedef-object-layout` (`20050215-1.c`, ten cells).
- The `s56.5-aligned-typedef-object-layout` tranche (PR #73) resolves all ten
  target-complete `torture-execute/20050215-1.c` cells. GCC measurements pin
  the positional GNU `aligned` rules that the earlier implementation had
  conflated: record and ordinary-member requests are minimums, while typedef,
  declarator-type, and direct-object requests are exact and may reduce natural
  alignment. An aligned typedef now carries that exact layout through aliases,
  arrays, members, and object placement without mutating an underlying named
  tag. Direct under-aligned objects and members retain honest IR access
  alignment, compatible redeclarations keep their strongest effective object
  alignment, and an incomplete object declaration retains the completed type's
  natural alignment as a live floor. On AAPCS64, an over-aligned typedef
  aggregate is no longer misclassified as an HFA; a reducing request remains
  eligible because it adds no padding.

  Behavior head `cf7b21be5ff0f61754b2356aa29fd3d1ca576821` passes 847 unit
  tests / 4,294,279 assertions, focused ASan+UBSan coverage for exact typedef
  layout, object/member lowering, and AArch64 HFA classification, and a
  500/500 GCC layout differential. The closed x86-64/SSE2 ISA matrix passes
  exactly 648 objects across 108 permanent corpus sources. Exact runtime
  coverage passes 18/18 executions across the imported case and two permanent
  fixtures, native Apple ARM64 passes the imported case at five levels and both
  fixtures at six, and cross-codegen passes all 30 target/level cells for
  x86_64-linux-gnu, arm64-linux, and arm64-macos.

  The first native ARM nightly at implementation head `2f56f816` correctly
  caught five-level regressions in both `20001109-1.c` and `20001109-2.c`:
  incomplete extern records had frozen their object alignment before tag
  completion. The live natural-floor repair is pinned by focused semantic and
  lowering regressions. The repaired exact-head nightly
  [run 33695727338](https://github.com/tenseleyFlow/Cgfried/actions/runs/33695727338)
  passes every campaign and rejects only the five unpublished native ARM
  `20050215-1.c` cells. Final pre-publication standard PR
  [run 33695975607](https://github.com/tenseleyFlow/Cgfried/actions/runs/33695975607)
  passes every non-ratchet job, including native Apple ARM64, both Linux ARM
  lanes, sanitizers, all campaigns, and 100,000-case frontend fuzzing; its x86
  gate rejects only the matching five cells and no regression.

  Both exact-head target streams contain all 20,325 cells and share
  compiler-source SHA-256
  `514b997534b38e74f3417c71f028272e6c09e8841338e88a7d04d7dcef8f74e5`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  torture-manifest SHA-256
  `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`,
  and c-testsuite-manifest SHA-256
  `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`.
  The exact x86 compiler/driver SHA-256 is
  `8bf91d9d1d7c2096a4b24eaf505cae6b5e0efe46a8372d24050ad6b20aef4bd0`;
  ARM's is
  `134c83f53a278fed49ebe1802c7f1dc4e349501a6e58b885f63fc00e030bf4a6`.
  Exact x86 and native ARM stream SHA-256 values are respectively
  `26b4534e40e1f48929af24863fd750500c553bfd1ef5e28ec4971196b215e4c3`
  and
  `50bcea8ce68673daef4e3489eea698765f76165e80641e94a60b980a213f7b2f`.

  The exact x86 and PR-CI PASS sets are byte-identical. Their only ten row
  differences are the already-policy-covered host-capacity variants for
  `limits-caselabels.c` (timeout versus SIGSEGV) and `limits-exprparen.c`
  (output guard versus explicit bracket-limit diagnostic). Formal
  target-complete publication promotes exactly the ten `20050215-1.c` cells
  with zero PASS regression and retires fingerprint `f6d3c6c3...`. The
  published state is 26,937 ratchet lines / 26,934 PASS keys, 7,096 classified
  failures, 52 buckets, 43 applied decisions, 18 live repair rows representing
  14 repair tranches, and zero stale or unresolved decisions. Reversing the
  evidence streams regenerates both outputs byte-identically; PASS and triage
  SHA-256 values are respectively
  `38447ed2bbc0e627f3cd2b4d433d0786de5832dc29f63795727f883fbf9d9774`
  and
  `2fcb4d24a2dc681bc9b9a57a6b5a033872af4c0e2fc0368bbdc6cd7dce2d99fc`.
  Fresh post-publication standard CI and exact-head native ARM64 must be green
  before merging. The next recommended target-complete compiler gap is
  `s56.5-vla-va-arg` (`20020412-1.c`, ten cells).
- The `s56.5-vla-va-arg` tranche (PR #74) resolves the coupled twenty
  target-complete `torture-execute/20020412-1.c` and `20070919-1.c` cells.
  GCC 13.3 measurements on SysV x86-64 and AAPCS64 establish the extension
  ABI: every variably sized record argument is a caller-owned private copy
  passed through one ordinary pointer slot, independent of runtime size.
  `va_arg` consumes that single register or overflow slot, dereferences it,
  and copies the declaration-time cached extent. Cgfried now gives this shape
  a lowering-only indirect classification rather than a fixed-size `byval`
  annotation. Ordinary runtime record assignments and temporaries use the
  same cached extent through libc `memcpy`; volatile runtime records retain
  observable byte loads and stores through an explicit dynamic loop.

  The originally selected `20020412-1.c` ABI repair necessarily exposed the
  already-tracked dynamic-copy primitive needed by `20070919-1.c`, so the
  tranche publishes both honest buckets rather than leaving an incidental
  XPASS. Permanent fixtures cover sizes 1, 5, 8, 9, 16, and 17, consecutive
  register and forced-overflow varargs, caller snapshot isolation, assignment
  chains, statement-expression results, aligned pointer slots, named
  parameters, and volatile copies. Behavior head
  `a7697298086db01a7a9c951243a3d318080d2d19` passes 849 Linux unit tests /
  4,294,319 assertions, focused combined ASan+UBSan checks, 110/110 x86
  permanent corpus programs, 94/94 applicable ARM programs under the full
  double-emulated lane, and exactly 660 objects across 110 sources in the
  closed x86-64/SSE2 ISA matrix. Native Apple ARM64 passes both imported cases
  and both fixtures at O0/O1/O2/O3/Os.

  Pre-publication standard PR
  [run 33709458788](https://github.com/tenseleyFlow/Cgfried/actions/runs/33709458788)
  passes every non-ratchet job, including native Apple ARM64, both Linux ARM
  lanes, sanitizers, all campaigns, and 100,000-case frontend fuzzing; its x86
  gate rejects only the ten unpublished x86 cells and no regression. Exact
  native ARM64
  [run 33709456313](https://github.com/tenseleyFlow/Cgfried/actions/runs/33709456313)
  likewise rejects only the matching ten ARM cells while every other nightly
  campaign passes. Push and PR bootstrap runs
  [33709456205](https://github.com/tenseleyFlow/Cgfried/actions/runs/33709456205)
  and
  [33709458819](https://github.com/tenseleyFlow/Cgfried/actions/runs/33709458819)
  are green at O0 and O2.

  Both exact-head target streams contain all 20,325 cells and share compiler-
  source SHA-256
  `2917eb0f618f25c12d2e88170ee8ac8a4f393b330de257e2f84a8fa72b36bc95`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  torture-manifest SHA-256
  `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`,
  and c-testsuite-manifest SHA-256
  `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`.
  The exact x86 compiler/driver SHA-256 is
  `1db978481c1afdd099640b4c0732fc718039faed20fd6a01d8916235a8523c18`;
  ARM's is
  `6daee46da463b59ae4bb87348e4d9712134e049a80c48ab302033d82a05b1ed7`.
  Exact x86 and native ARM stream SHA-256 values are respectively
  `c107cbd6db130cb972bdcc0e035ecbc6c4db75a89415fef2748e2729a7f281c1`
  and
  `5ca477ea8c5892b6ec706e3de3f87cdfbc262c5404a54dfef58ae3b89559199e`.

  The exact x86 and PR-CI PASS sets are byte-identical. Their only five
  differing keys are the already-policy-covered `limits-caselabels.c` host
  capacity variant (ten differing rows). All current streams reach the output
  guard for `limits-exprparen.c`; its previously observed explicit bracket-
  limit fingerprint remains deliberately retained as one stale cross-host
  policy decision. Formal target-complete publication promotes exactly the
  twenty VLA-record cells with zero PASS regression and retires fingerprints
  `27f6214b...` and `cd599d47...`. The published state is 26,957 ratchet lines /
  26,954 PASS keys, 7,076 classified failures, 49 observed buckets, 40 applied
  decisions, 16 live repair rows representing 12 repair tranches, one retained
  stale host variant, and zero unresolved decisions. Reversing the evidence
  streams regenerates both outputs byte-identically; PASS and triage SHA-256
  values are respectively
  `64eb243ef9347b35995e77e8b5f92bca29cdf25302cd685f0dbf84e499630363`
  and
  `0a0e0d0b017237dcf063a049621a0fbf50dfdfda1bac8c0e7983d836078d8d22`.
  Fresh post-publication standard CI and exact-head native ARM64 must be green
  before merging. The next recommended target-complete compiler gap is
  `s56.5-bitfield-integer-promotions` (`bf-sign-2.c` and `bitfld-1.c`, twenty
  cells).
- The current `s56.5-bitfield-integer-promotions` tranche (PR #75) resolves
  the twenty target-complete `torture-execute/bf-sign-2.c` and
  `bitfld-1.c` cells. GNU permits `long` and `long long` bit-field bases, but
  integer promotion follows a field's effective precision and signedness, not
  the rank of that declared base. Cgfried now records the resolved width and
  signedness on member expressions, preserves those facts through parentheses,
  `_Generic`, selected `__builtin_choose_expr` arms, and qualifier-stripping
  lvalue conversion, and consumes them in integer promotion, usual arithmetic
  conversion, warning analysis, compound assignment, and atomic update
  lowering. Explicit casts still deliberately end bit-field identity.

  Adding the permanent runtime fixture changed the deterministic fuzz corpus
  and exposed a real Darwin-only prerequisite at seed 1924: Apple's pointer-
  form `va_list` cursor was lvalue-converted before validation, so a qualified
  cursor became an unaddressable implicit cast and reached `lower_lvalue` as an
  ICE. Cursor preparation is now target-form aware: array-form cursors retain
  their ordinary decay, while pointer-form cursors must be actual unqualified
  lvalues; selected GNU `choose_expr` lvalues are addressable in lowering.
  Seed 1924 now produces the intended diagnostic rather than an ICE. The new
  5,000-iteration mutation digest `c08ae8dbae4457b6` reproduced twice on both
  Apple ARM64 and Linux x86-64 before it was pinned, and 2,000-iteration smoke
  runs are clean on both hosts.

  Behavior head `ae511739a71fb62447e15ea006a4f7ba75edf1c7` passes 852
  Linux unit tests / 4,294,338 assertions, focused combined ASan+UBSan checks
  for all twelve bit-field tests / 198 assertions and the Apple `va_list`
  regression / eight assertions, and safe dogfood for all 107 compiler
  translation units with zero exemptions. The two imported cases and the
  permanent runtime fixture pass at O0/O1/O2/O3/Os on both Linux x86-64 and
  native Apple ARM64 (30/30 executions). Pinned clang-format 22 is clean. A
  clean emulated-x86 `make test` run passed the unit, program, corpus, ISA,
  differential, warning, memory-safety, and runtime gates before its late
  preprocessor differential stopped solely because that minimal VM lacks the
  required Clang oracle; exact-head hosted `test` passed with the oracle
  present.

  Pre-publication standard PR
  [run 33812735459](https://github.com/tenseleyFlow/Cgfried/actions/runs/33812735459)
  completed with nineteen successful jobs, one expected skip, and only the
  x86 torture gate's ten unpublished PASS cells; this includes native Apple
  ARM64, both Linux ARM lanes, sanitizers, all campaigns, safe dogfood, and
  100,000-case frontend fuzzing. Exact-head native full-lattice
  [run 33812743654](https://github.com/tenseleyFlow/Cgfried/actions/runs/33812743654)
  completed with fourteen successful jobs and only the ARM torture gate's
  matching ten unpublished PASS cells. Push and PR bootstrap runs
  [33812732128](https://github.com/tenseleyFlow/Cgfried/actions/runs/33812732128)
  and
  [33812735547](https://github.com/tenseleyFlow/Cgfried/actions/runs/33812735547)
  are green at O0 and O2.

  Both exact-head target streams contain all 20,325 cells and share source
  revision `ae511739a71fb62447e15ea006a4f7ba75edf1c7`, compiler-source
  SHA-256
  `1ab27e87d7af8d7380912cf82ac78d4c2885ae1e76f6539b980c031cb53e9082`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  torture-manifest SHA-256
  `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`,
  and c-testsuite-manifest SHA-256
  `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`.
  The exact x86 compiler/driver SHA-256 is
  `626c203391f884c1718d672d679cbe70efb255dd5804c38ad5e8e2aa8b9c14bd`;
  ARM's is
  `a0b8f4e916da2c3bd89964e5a4a089e6585899c95572aa4cd64c345c06d23d24`.
  Exact x86 and native ARM stream SHA-256 values are respectively
  `41e79ba9ed05cfa70193a19a6139ef49e5a36fbfcec312cea3be2bd6b36675c6`
  and
  `ca428edc9ceecf60c3f81ccdaac2a9fdf05aab99c37363ff6ec8e7fb938eb5e2`.

  GitHub's PR checkout produced synthetic-merge x86 source revision
  `eae0c9a7fa170a583679066daa7183a4b6f24449`, but the compiler-source hash
  and all 13,489 x86 PASS keys are byte-identical to the exact branch-head
  stream. Their only ten differing keys (twenty row representations) are the
  already-policy-covered host-capacity variants for `limits-caselabels.c`
  (SIGSEGV versus timeout) and `limits-exprparen.c` (explicit bracket limit
  versus output guard). The latter makes the previously retained stale host
  variant observable again, leaving no stale policy decisions.

  The baseline engine processed both complete streams in forward and reverse
  order and regenerated both outputs byte-identically. Atomic publication
  promotes exactly the twenty intended bit-field cells with zero PASS
  regression and retires fingerprints `311376d0...` and `eafbd9ee...`. The
  published state is 26,977 ratchet lines / 26,974 PASS keys, 7,056 classified
  failures, 48 observed buckets, 39 applied decisions, 14 live repair rows
  representing 11 repair tranches, and zero stale or unresolved decisions.
  PASS and triage SHA-256 values are respectively
  `6cfb81a58b8b6bc33f1f0d0d95e4510f163c4361d5cd1f646f57055a77f0d231`
  and
  `1c3cc17bf09bc59074a23b023a528669a2f276a916e9041fe1f79ab944d519fc`.
  Fresh post-publication standard CI and exact-head native ARM64 must be green
  before merging. The next recommended target-complete compiler gap is
  `s56.5-bitfield-expression-precision` (`bitfld-3.c`, `bitfld-5.c`,
  `pr32244-1.c`, and `pr34971.c`, forty cells).
- The current `s56.5-bitfield-expression-precision` tranche (PR #76) resolves
  the forty target-complete `torture-execute/bitfld-3.c`, `bitfld-5.c`,
  `pr32244-1.c`, and `pr34971.c` cells. GNU gives a bit-field wider than
  `int` but narrower than its declared carrier an anonymous extended integer
  type whose rank, signedness, compatibility, and value-producing operations
  observe the field's exact precision. Cgfried now models that type explicitly
  while retaining the ordinary 64-bit ABI carrier, applies the precision in
  integer promotion and usual arithmetic conversion, preserves GNU comma-
  expression bit-field identity, and narrows arithmetic, shifts, unary
  operations, increments, conversions, and atomic compare-exchange updates at
  the correct boundaries. Explicit casts and full-carrier-width fields retain
  ordinary standard integer semantics.

  Permanent unit coverage checks anonymous-type compatibility, rank,
  signedness, mixed-width conversions, GNU `typeof`, comma expressions,
  explicit-cast boundaries, post-operation narrowing, increments, and atomic
  retry lowering. The executed fixture covers unsigned 33-, 40-, and 41-bit
  arithmetic, struct argument passage, shifts, rotate-shaped expressions,
  casts, and `typeof` variables. Behavior head
  `dce7a90904a9cdda3252ade43a2f1e5fca76d6d0` passes 853 Linux unit tests /
  4,294,360 assertions, 760 program fixtures, 110 corpus fixtures, focused
  combined ASan+UBSan checks, and safe dogfood for all 107 compiler
  translation units with zero exemptions. The four imported cases and the
  permanent runtime fixture pass at O0/O1/O2/O3/Os on both Linux x86-64 and
  native Apple ARM64. Pinned clang-format 22 is clean. The new deterministic
  frontend-fuzz digest `de0e7ad32201685d` reproduced twice on each
  architecture, 2,000-iteration smoke runs are clean on both, and CI's
  100,000-case sanitized run is green.

  Pre-publication standard PR
  [run 33823553260](https://github.com/tenseleyFlow/Cgfried/actions/runs/33823553260)
  completed with nineteen successful jobs, one expected skip, and only the
  x86 torture gate's twenty unpublished PASS cells; this includes native Apple
  ARM64, both Linux ARM lanes, sanitizers, all campaigns, safe dogfood, full
  test, formatting, and 100,000-case frontend fuzzing. Exact-head native
  full-lattice
  [run 33823579402](https://github.com/tenseleyFlow/Cgfried/actions/runs/33823579402)
  completed with fourteen successful jobs and only the ARM torture gate's
  matching twenty unpublished PASS cells. Push and PR bootstrap runs
  [33823534799](https://github.com/tenseleyFlow/Cgfried/actions/runs/33823534799)
  and
  [33823553300](https://github.com/tenseleyFlow/Cgfried/actions/runs/33823553300)
  are green at O0 and O2.

  Both exact-head target streams contain all 20,325 cells and share source
  revision `dce7a90904a9cdda3252ade43a2f1e5fca76d6d0`, compiler-source
  SHA-256
  `d6e2d5712b174e4cf96826a79f4ea417520cd2bde0362f3b74c230845aa7831d`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  torture-manifest SHA-256
  `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`,
  and c-testsuite-manifest SHA-256
  `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`.
  The exact x86 compiler/driver SHA-256 is
  `b1ca5549d6f79fec4c39ca5fe1371b3a21d81352eaf9836928d833e7f2bcca9f`;
  ARM's is
  `90c31d61f6a7d83ff5052ea1be1dab88dffe59f00d88ee9134c06a8026e13efd`.
  Exact x86 and native ARM stream SHA-256 values are respectively
  `0a72f543fcc909ed511be447f0c3b4d8672c5154a653d33467bb8ff63b8451cf`
  and
  `063c1eecc39155af85bfb88351cddb0cdecc1f31382b4651fd0129567de36d81`.

  GitHub's PR checkout produced synthetic-merge x86 source revision
  `268b08f75491377a257b1dcf933d2e10cb61adc3`, but its compiler-source hash
  and all 13,509 x86 PASS keys are byte-identical to the exact branch-head
  stream. Their only five differing keys (ten row representations) are the
  already-policy-covered `limits-caselabels.c` host-capacity variant (native
  SIGSEGV versus CI timeout). Every current stream reaches the output guard
  for `limits-exprparen.c`, so its explicit bracket-limit fingerprint remains
  deliberately retained as one stale cross-host policy decision.

  The baseline engine processed both complete streams in forward and reverse
  order and regenerated both outputs byte-identically. Atomic publication
  promotes exactly the forty intended bit-field cells with zero PASS
  regression and retires fingerprints `225de8a3...`, `3e9eddc5...`,
  `6d8f6f38...`, and `75360aa5...`. The published state is 27,017 ratchet
  lines / 27,014 PASS keys, 7,016 classified failures, 43 observed buckets,
  34 applied decisions, 10 live repair rows representing 10 repair tranches,
  one retained stale host variant, and zero unresolved decisions. PASS and
  triage SHA-256 values are respectively
  `3740ba74749f767f206dcbcf2b53ff468127abd0edc35b736137fc1a58d1acf2`
  and
  `df8f4ae53bd8c6e838f90ce23bdd84b03ab4d5b0bebec79d8aaf6d1343248001`.
  Fresh post-publication standard CI and exact-head native ARM64 must be green
  before merging. The next recommended target-complete compiler gap is
  `s56.5-huge-object-layout` (`991014-1.c`, ten cells).
- The current `s56.5-huge-object-layout` tranche (PR #77) resolves the ten
  target-complete `torture-execute/991014-1.c` cells. Record layout now keeps
  an overflow-safe byte cursor plus a sub-byte bit shift instead of expressing
  the whole position in bits, so valid objects and member offsets near the
  implementation's `UINT64_MAX` size boundary remain exact. The same audit
  narrowed the stored bit-field metadata to the shift its consumers actually
  require. A separately exposed x86-64 instruction-selection alias is also
  fixed: materializing a wide `IR_PTRADD` constant can no longer overwrite the
  LEA slot after the selector rewinds its output cursor.

  Permanent unit coverage checks huge structure/union sizes, member offsets,
  alignments, array completion, and `offsetof` on all five supported target
  layouts (90 assertions), plus pre- and post-register-allocation x86 wide
  pointer addition (six assertions). The executed fixture covers the same
  huge-object paths without allocating the enormous object. Behavior head
  `3a179a720a20c9f103c76b42563d5f524f940898` passes 855 Linux unit tests /
  4,294,456 assertions, 761 program fixtures, 110 corpus fixtures, all
  affected sanitizer and safety lanes, exact Linux x86 execution at
  O0/O1/O2/O3/Os, and native Apple ARM64 execution and assembly checks. Pinned
  clang-format 22 is clean. The deterministic frontend-fuzz digest is
  `638a06f158ddc65c`; 2,000-case frontend and preprocessor smokes and a
  5,000-case / 63-seed IR smoke are clean, while CI's 100,000-case sanitized
  frontend run is green.

  Pre-publication standard PR
  [run 33832601852](https://github.com/tenseleyFlow/Cgfried/actions/runs/33832601852)
  completed with every non-ratchet job green, including native Apple ARM64,
  both Linux ARM lanes, sanitizers, all campaigns, safe dogfood, full test,
  formatting, and 100,000-case frontend fuzzing. Its x86 torture gate reports
  the five intended `991014-1.c` passes plus an independently observed
  `limits-blockid.c@O0` scalability pass on the official runner. Exact
  behavior-head native ARM64
  [run 33832617137](https://github.com/tenseleyFlow/Cgfried/actions/runs/33832617137)
  reports only the five intended ARM passes. The same-tree integration
  full-lattice
  [run 33835107676](https://github.com/tenseleyFlow/Cgfried/actions/runs/33835107676)
  completed with every campaign job green and only the ARM torture gate's five
  unpublished intended passes.

  The official x86 and native ARM streams contain all 20,325 cells and share
  synthetic integration source revision
  `8b1884c5471f557f9bfca09231c88de73bf62776`, compiler-source SHA-256
  `8e2bd03361571785d587300e4297536cc28103f037988cfdba4f399ceb370be0`,
  harness SHA-256
  `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`,
  torture-manifest SHA-256
  `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`,
  and c-testsuite-manifest SHA-256
  `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`.
  The x86 compiler/driver SHA-256 is
  `4c25c88933e0b2172a4355478ff28f5a285275092de5acc389ad195a6a7985ed`;
  ARM's is
  `9687b3d4588a46c061e6895885cc636192a2dd2bcbff6c2d673872d5e10d6159`.
  Exact x86 and ARM stream SHA-256 values are respectively
  `41a63377dfc2220b96b7f130660bcc28ea55644f97275aa788728c990a9c18c8`
  and
  `826cd9d90df5fad75edc3aeada8f203d9351bc864edf982dee6cdc76c7263351`.
  The behavior commit and GitHub's synthetic integration commit have the same
  tree, `e80afe4d69a60c4d822a875bd3552fee4e8eb282`.

  The baseline engine processed the two complete streams in forward and
  reverse order and regenerated both outputs byte-identically. Atomic
  publication promotes the ten intended huge-object cells and the one
  independently observed official-x86 scalability cell with zero PASS
  regression. It retires fingerprint `dcf802f4...` while leaving the
  24-cell compiler-scalability timeout bucket live. The published state is
  27,028 ratchet lines / 27,025 PASS keys, 7,005 classified failures, 42
  observed buckets, 33 applied decisions, nine live repair rows representing
  nine repair tranches, one retained stale host variant, and zero unresolved
  decisions. PASS and triage SHA-256 values are respectively
  `b65edb5706c55cc1af6149305fcc383c8d1418504910157a652eed3a3c8b5038`
  and
  `52b73b1d6a03bbcc8e4fab3c78ab44edbbc28109357387e0087f15dddabccac9`.
  Fresh post-publication standard CI and exact-head native ARM64 must be green
  before merging. The next recommended target-complete compiler gap is
  `s56.5-utf8-wide-literal-decoding` (`ctestsuite/00220.c`, ten cells).
- The August 28 machine transfer to Apple silicon does **not** require a native
  support campaign. On Darwin ARM64, `make build/cgfried` produces a native
  Mach-O compiler reporting `arm64-macos`; a Cgfried-built hello-world links,
  signs, and runs. `make tools` builds both bundled Rust tools natively,
  `tests/macos/run.sh` passes all 16 builds and eight programs with both system
  and bundled linkers, and `scripts/macho_objdiff_lane.sh` reports all ten
  objects byte-identical to Apple `as`. One local developer-test gap is the
  x86 simulator helper in `tests/unit/x64sim.h`: it assumes an x86-host
  10-byte `long double`, while Darwin ARM64 uses eight bytes, so Apple Clang 21
  rejects that helper's `memcpy` under `-Werror`. This is a unit-harness
  portability issue, not a Cgfried Darwin codegen failure; targeted non-x86
  sema/conversion tests pass with that warning demoted. Keep it as a separate
  narrow harness-portability tranche rather than blocking compiler-gap work.
  The merged failure-decomposition tranche fixes the earlier Darwin signal
  fingerprint nondeterminism; its distinct-testcase and cross-target assertions
  pass locally. The full runner gate now proceeds to an older Darwin-only phase
  probe mismatch (`cg-fail.c` has an empty phase instead of `cg`). The triage
  and matrix failure-injection fixtures also miss their expected injected
  failure on Darwin (the staged atomic outputs themselves remain
  byte-identical). Keep those host path/tool assumptions with the x64 simulator
  issue in the separate harness-portability tranche.
- The behavior-head CI [run 33347696647](https://github.com/tenseleyFlow/Cgfried/actions/runs/33347696647)
  passes every check other than its intentionally pre-publication x86 PASS-set
  gate: that gate reports only the five uncommitted `pr41935.c` promotions and
  no regression. Both bootstrap runs pass, as do full test, sanitizer, macOS
  ARM64, QEMU ARM64, toolchain, campaign, formatting, and fuzz lanes. A fresh
  CI run is required after the publication commit before merge.
- CI runs the complete x86 matrix on every PR and the native arm64 matrix on
  the scheduled runner.  Matrix publication and baseline refresh are atomic,
  target-complete, and provenance checked.

No original Sprint 56 campaign-infrastructure work remains. Sprint 61 repairs
and the Sprint 56.5 declarator, packed-bitfield, enum-bitfield,
enum-integer-mode, static-subobject-pointer-difference, and VLA-semantics
tranches retired every bucket they fixed. The aggregate-initializer tranche is
implemented, its target-complete ratchet is published, and PR #30 is merged.
The flexible-array-initializer tranche is implemented, published, and merged
through PR #40. The nested-flexible-array-member tranche is implemented and its
exact-head target-complete ratchet is merged through PR #41. The old-style
designator tranche is implemented and its exact-head target-complete ratchet is
merged through PR #42 as `6ef24ca2`; range designators are merged through PR
#43. GNU `#ident` is implemented and its exact-head target-complete ratchet is
merged through PR #44 as `d52e44d1`; GNU macro pragma stacks are merged through
PR #46 as `eba78f27`. The alignof extension tranche is implemented and its
target-complete ratchet merged through PR #47 as `5277c7fc`. The
extension-type-name tranche is implemented and merged through PR #48 as
`1d6da267`. The generic-qualified-association tranche is implemented,
target-complete, and merged through PR #49 as `d8ccfc04`. The extern-void
tranche is implemented, target-complete, and merged through PR #50 as
`e2439e79`. The GNU `alloca` alias tranche is implemented, target-complete,
and merged through PR #51 as `d7d59fa`. The compound-literal array-completion
tranche and target-complete ratchet are merged through PR #52 as `cfaec8d`.
The failure-decomposition tranche is merged through PR #53. The remaining
compiler debt is enumerated by 23 live `s56.5-*` repair rows representing 19
unique repair tranches. Sprint
54 and Phase 11 subsequently closed on their independent fleet evidence.

---

## 0a. Sprint 54 / Phase 11 closeout — COMPLETE

The implementation, three-date controlled evidence, current release report,
closure audit, and ratchet are complete. Pre-control artifacts remain
provenance-only. The absolute load limit was reopened for one evidence-backed
repair: idle Hasu and Nomad measured roughly 90% aggregate CPU idle while
ordinary desktop housekeeping held their one-minute load near 2.8.

### Implemented and verified

- The ten-row lattice in `doc/perf-gates.md` maps 1:1 to `ci/gates.d/`:
  controlled-host compile time (+30%), RSS (+20%), stripped program/compiler
  size (+15%), kernel instruction count (`max(2%, 2)`), kernel `.text` (+5%),
  MAD-guarded runtime (+10% and beyond four MADs), report-only internals, and
  explicit inactive Sprint 57/58 lanes.
- Shared x86 and native ARM CI measure RSS, sizes, and kernel static metrics;
  timing comparisons remain forbidden on shared runners. Trial gates record
  trips without hiding infrastructure status 3. The seeded boundary proofs,
  13/14-day state transition, escape-hatch policy, summary goldens, scoped
  multi-host release report, and 90-day trend classifier are fixture-pinned.
- Fleet-control-v2 records logical CPU count, one-minute load, and a fixed
  aggregate CPU-idle preflight. It accepts only normalized load
  (`load1 / logical_cpus`) at most 0.20 and CPU idle at least 85%, in addition
  to the platform power rules. Current artifacts require the complete v2
  tuple; partial, empty, malformed, or wrong-host evidence fails with status
  3. Complete v1 evidence remains controlled only under its original stricter
  `load1 <= 0.5` bound, preserving the immutable accepted Kasumi pair.
- Linux fleet controls model effective state, not a misleading raw governor:
  `performance` is accepted directly, while active `intel_pstate` is accepted
  as `power_profile=performance`, `governor=powersave`, and
  `energy_performance_preference=performance`. Artifacts that predate this
  complete power tuple remain provenance-only and cannot silently enter a
  timing gate.
- `make test-perf-gates`, the 29-case control helper, the 58-case compile
  gate, the 35-case runtime gate,
  kernel comparison tests, ShellCheck, the POSIX-shell check, formatting, and
  `git diff --check` all passed after the control-model fixes. Independent
  review approved the implementation and directly reproduced the two repaired
  no-baseline/empty-field failures.
- The first post-v2 shared-CI run exposed one integration regression:
  `bench.sh` tried to classify GitHub's ephemeral runner hostname as a fleet
  host. Recognized `ci`, `shared-ci`, and `arm64-ci` classes now take an
  explicit RSS-only producer path that requires `BENCH_SKIP_TIME=1`, omits
  fleet-only v2 provenance, and never invokes the fleet controller. The gate
  independently refuses those classes in every timing mode, including a
  fixture with forged controlled-looking Kasumi provenance; unknown classes
  still fail closed through the fleet controller.
- `scripts/bench-summary.sh`, `perf-report.sh`, and `bench-trend.sh` emit
  deterministic Markdown with signed deltas, named thresholds, host-scoped
  provenance, prior-report markers, and honest report-only classifications.
  `[bench skip]` and `perf-override: #NNN` remain narrow and audited.

### Controlled deployment evidence

- All three schedulers are installed with pushes enabled. Kasumi and Hasu use
  user-systemd pre/post hooks that enter the performance profile, verify the
  effective governor/driver/EPP tuple, and restore the saved profile even when
  the nightly command fails. Nomad uses LaunchAgent label
  `com.tenseleyflow.cgfried-fleet-perf` and records the Linux-only fields as
  `unavailable`.
- Kasumi's first controlled run is
  `2026-08-11T065745Z-kasumi` / `-kernels` in artifact commit `106efed5`:
  compile load 0.01, runtime load 0.35, performance profile,
  `intel_pstate`/`powersave`, performance EPP, clean tree, no trip. The
  artifacts compared provenance-only against the incompatible legacy
  baselines, then were accepted byte-for-byte in the separate reviewed
  baseline commit `b87a2789`.
- Hasu's first controlled fleet-control-v2 run is
  `2026-08-11T160603Z-hasu` / `-kernels` in artifact commit `1643af0d`:
  20 logical CPUs, compile load 0.89 at 97.54% idle, runtime load 0.98 at
  97.49% idle, performance-profile `intel_pstate`/`powersave` with performance
  EPP, clean tree, and no trip. The exact-copy compile/runtime baselines were
  accepted separately in `787b9cec`.
- Nomad's first controlled fleet-control-v2 run is
  `2026-08-11T161721Z-nomad-1` / `-kernels` in artifact commit `2e152c54`:
  18 logical CPUs, compile load 2.32 at 89.74% idle, runtime load 2.67 at
  85.25% idle, truthful Darwin-unavailable power fields, clean tree, and no
  trip. Two earlier LaunchAgent attempts failed closed with status 3 and wrote
  no artifacts because the generated PATH omitted `/usr/sbin`, hiding
  `sysctl`; `7e7986d3` repaired both scheduler templates and regression-tested
  `/usr/sbin:/sbin` preservation. The exact-copy Nomad baselines were accepted
  separately in `9c50e6d1`.
- The installed schedules added clean, no-trip second-date pairs for Kasumi
  (`2026-08-12T011528Z`, commit `6db164e0`) and Hasu
  (`2026-08-12T013538Z`, commit `8d5898bc`). Both carry the complete
  fleet-control-v2 tuple and preserve their host-specific power contract.
- Nomad's clean, no-trip second-date pair is `2026-08-12T055505Z` in commit
  `d5b99a5f`: compile load 3.34 at 87.66% idle and runtime load 2.87 at
  86.13% idle across 18 logical CPUs, with the truthful Darwin-unavailable
  power tuple.
- Kasumi's clean, no-trip third-date pair is `2026-08-13T011528Z` in commit
  `abea4d5a`, and Hasu's is `2026-08-13T013538Z` in commit `6d8f6fde`.
  Both carry complete fleet-control-v2 provenance plus the Sprint 58
  controlled bootstrap timing receipt. Nomad's clean, no-trip third-date pair
  is `2026-08-13T055503Z` in commit `881659ba`: compile load 3.03 at 87.53%
  idle and runtime load 3.50 at 86.43% idle across 18 logical CPUs, with the
  truthful Darwin-unavailable power tuple. This completes the required three
  distinct UTC dates on every fleet host.
- Controlled musl warmups from compiler revision `3ff951d0` were accepted as
  exact-copy Kasumi/Hasu baselines in `8e422f8e`. Post-baseline runs
  `2026-08-13T221954Z-{kasumi,hasu}-musl-full-build.txt` are v2-controlled,
  clean-tree, `trial-pass`, and pass the +30% wall / +20% RSS gate without
  mutating those baselines. Artifact commits are `7e0482b9` and `991b2bec`.
- `.benchmarks/report-0.0.1.md` was regenerated twice byte-identically from
  the current controlled fleet baselines/latest artifacts plus committed
  CI/static evidence. Its SHA-256 is
  `92f26a1f90b3ea5ae1414c58701ebac43cc4caaf410a51da90b7b630ad2cab9b`;
  every selected fleet input classifies controlled.
- Older Kasumi, Hasu, and Nomad artifacts collected before the new fields
  landed remain useful deployment provenance only. They are not controlled
  timing/runtime evidence and must not be relabelled or used to manufacture a
  three-night history.
- The shipping five-second preflight observed Hasu's 20 logical CPUs at 91.45%
  idle with load 1.01, and Nomad's 18 logical CPUs at 90.20% idle with load
  2.27. Linux derives idle from aggregate `/proc/stat` deltas with I/O wait
  treated as busy; Darwin uses the second `top -l 2 -s 5 -n 0` sample. Both
  hosts pass fleet-control-v2; no user workload needs to be stopped to chase
  the retired absolute threshold.

### Closure result

- The full local repository suite passes with 711 unit tests and 4,288,526
  assertions, all performance/campaign contracts, every differential and
  cross lane available locally, formatting, POSIX-shell checks, and the clean
  fuzz ledgers.
- The exact baseline revision `8e422f8e` passed hosted bootstrap at O0/O2 and
  the complete standard CI matrix. Raising the contiguous ratchet to 57
  exposes no stale source deferrals; both stage1 and musl lanes are active in
  trial state.
- Sprint 54 and Phase 11 are closed. Sprint 59 later closed out of order;
  resume Sprint 58's independent soak above.

### Checkout hygiene

`afs-as` and `afs-ld` may remain dirty from pre-existing submodule work, and
the many untracked `build-*` directories are local artifacts. Do not stage or
delete them as part of Sprint 54. `AGENTS.md`, `CLAUDE.md`, and
`.docs/sprints/` remain ignored local project memory; `.docs/HANDOFF.md` is
tracked.

---

## 0b. HISTORICAL D5 IMPLEMENTATION RECORD — superseded

Everything in this section records the state before Sprint 55 closed. Its
commands, blocker states, and counts are not current instructions; §0a wins.

### Former resume point — D5, `__GNUC__`

**THREE of D5's blockers are DONE and pushed. A FOURTH was found by
widening the probe, and it is the next task.** Sprint 55 is NOT closed.

| blocker | state |
|---|---|
| 1. `__builtin_bswap16/32/64` | **DONE** — `b8631a90` |
| 2. integer `mode(M)` | **DONE** — `faf0bbcd`; took the header probe from 1/19 clean to 18/19 |
| 3. `_Float128` | **DONE** — `de84d2e2`; `math.h` compiles, bit-identical to gcc at every level |
| 4. **GNU named variadic macros** | **NOT STARTED** ← next; fully measured below |
| 5. the `__GNUC__` predefine itself | LAST, after 4 |

### THE ONE THING TO UNDERSTAND FIRST

**Defining `__GNUC__` is a PROMISE, not a predefine.** glibc's
`sys/cdefs.h` gates dozens of declarations on it; today, with it
undefined, glibc *neutralizes* `__attribute__` itself, which is why every
attribute fixture in the tree is freestanding. **The day `__GNUC__` is
defined, every attribute in every system header goes live at once**, and
answering yes and then rejecting one of gcc 8's extensions is worse than
answering no. That is why the predefine goes in LAST.

### BLOCKER 4 — GNU named variadic macros. Everything below is measured.

`#define M(args...)` and `#define M(fmt, rest...)`. Blocks `netdb.h`,
`arpa/inet.h` and `sys/socket.h` — so, sockets.

**HOW IT WAS FOUND, and the lesson:** the handoff previously said "three
blockers, measured." That measurement covered **19** headers. Widening the
same probe to **30** found this one. *The probe's coverage bounds its
conclusion* — widen it again before believing four is the final number.

```sh
C=build-d5/cgfried
for h in stdio stdlib string ctype errno time math limits unistd fcntl \
         sys/types sys/stat sys/mman signal pthread setjmp inttypes stdint \
         locale assert wchar wctype dirent netdb arpa/inet sys/socket \
         sys/wait sys/time termios poll; do
  printf '#include <%s.h>\nint main(void){return 0;}\n' "$h" > one.c
  echo "$h -> $($C -std=gnu17 -D__GNUC__=8 -D__GNUC_MINOR__=3 \
      -D__GNUC_PATCHLEVEL__=0 -fsyntax-only one.c 2>&1 |
      grep -m1 'error:' | cut -c1-100)"
done
```
Currently **27 of 30 clean**; the three failures are all this feature.
Compile each header ALONE and take only its FIRST error — a dozen at once
gives ~40 mostly-cascade errors.

**The measured semantics (gcc 16, `-std=gnu17`):**

- The name **REPLACES** `__VA_ARGS__` rather than adding to it: inside
  `#define M(a...)`, using `__VA_ARGS__` is an ERROR ("can only appear in
  the expansion of a C99 variadic macro"). A parameter literally named
  `__VA_ARGS__` is the same error.
- In a plain `#define M(...)`, the identifier `args` is just an ordinary
  token — the name is per-macro, not global.
- `#define M(a...)` then `#define M(...)` is a **redefinition** warning, so
  the ISO redefinition compare must consider the variadic name.
- The named tail must be LAST: `M(a..., b)` is "expected ')' after '...'".
- Zero arguments works; `#` stringizes the tail (`S(3, 4)` → `"3, 4"`).
- `-pedantic` gives `-Wvariadic-macros`, "ISO C does not permit named
  variadic macros". **That registry id already exists**
  (`WARN_VARIADIC_MACROS`, `WG_PEDANTIC`/`WD_PEDANTIC`/`WL_PEDWARN`), so
  the pedwarn is free.
- Expansions to pin: `M(1,2)` → `f(1,2)`; `N(9,8,7)` with
  `#define N(x, rest...) g(x, rest)` → `g(9, 8,7)` (note the spacing).

**THE CODE SHAPE IS FAVOURABLE — it is one index, not a second mechanism.**
`find_param()` in `src/pp/macro.c:137` returns `m->nparams` for the
variadic slot, and *everything* keys off that: substitution, stringize, and
the `, ## __VA_ARGS__` comma swallow at ~line 631. So:

1. `MacroDef` (`src/pp/pp.h:238`) gains an interned `va_name`, NULL meaning
   `__VA_ARGS__`.
2. The parameter loop (`src/pp/macro.c:~200`, where the refusal lives now)
   accepts `IDENT ...`, sets `is_variadic` and records the name.
3. `find_param` matches `va_name` when set **instead of** `__VA_ARGS__`
   (measured: they are exclusive), and the `__VA_ARGS__`-outside-a-variadic
   error at line 281 needs the same treatment.
4. The redefinition compare must include `va_name`.

**The refusal names Sprint 55 itself**, so `check_deferrals` would flag it
at close regardless: a deferral to the CURRENT sprint must be resolved
before that sprint ends, in one direction or the other.

### D5 — what has NOT been measured yet

- **arm64-macos and FreeBSD have their own lists.** Run the same per-header
  first-error probe there. Apple's `sys/cdefs.h` uses `__attribute__` with
  NO `__GNUC__` gate at all, so its failure mode differs in kind.
- The DoD wants `__GNUC__` **8 / 3 / 0** in gnu modes and **absent** in
  `-std=c*` modes, both fixtured. The split is load-bearing.
- Expect the musl gates to MOVE. Both are exact pins; re-pin deliberately
  with a sentence of justification.

### WHAT IS NOT YET VERIFIED at `de84d2e2` — do this FIRST

The three blockers are implemented and each was proved against gcc
directly. What has NOT been re-run end to end since the final
comment-only reformat:

```sh
make BUILD=build-v-gcc   CC=gcc   test          # rc must be 0; FAIL count lies
make BUILD=build-v-clang CC=clang test
make test-a64-corpus && make test-a64-spill-all  # SEQUENTIALLY, never -j
scripts/musl_warn_dryrun.sh build-v-gcc/cgfried  # exact pinned numbers
make musl-sweep
```

The last full gcc run before that reformat was green with **zero FAIL
lines**; the only gate that fired was `check_bans`, tripped by the literal
`__attribute__` inside two of my own COMMENTS — now reworded, and
`check_bans`/`check_format`/`check_gnu_tiers`/`check_deferrals` are all
clean. So the expectation is green, but it is an expectation, not a run.

**Specifically unproven:** the arm64 lanes have not seen `_Float128` or
`mode` execute. `tests/corpus/x86_64/fp/gnu_float128.c` was written to run
there (its rank assertion branches on `__aarch64__` precisely because the
answer differs), and it cross-compiles clean, but it has not run under
qemu. The clang lane has not been run at all since D5 started.

### HOUSE RULES that a compaction will drop

- **Commit often, in chunks.** Terse imperative subject, under ~250 chars
  unless the body earns more. **Never co-author. No "Generated with" or
  session trailers.** Tests and CI are first-class.
- **`AGENTS.md`, `CLAUDE.md` and `.docs/sprints/` are GITIGNORED and must
  NEVER be committed.** `git add` refuses `.docs/sprints` and helpfully
  suggests `-f` — **do not take that hint.** Sprint-file corrections stay
  local and go in the COMMIT MESSAGE instead. `AGENTS.md` is the reality
  snapshot and syncs to `CLAUDE.md` with `cp AGENTS.md CLAUDE.md`.
- `.docs/audits/*.md` and `.docs/HANDOFF.md` ARE tracked, via explicit
  gitignore negations.
- The shell here is **fish** for the user, and the assistant's `Bash` tool
  runs **zsh**, which does **not word-split an unquoted expansion** — `cgf
  $flags file` passes ONE argument. Use `${=flags}`. This silently made every
  fixture answer "unrecognized command-line option", which then read as "the
  fixture is vacuous".
- Task list: **#125 is D5**, and carries the same measurements as §0a. #123
  (switch label out-of-range check) and #112 (`.bss` for an explicitly
  zero-initialized global) are open pre-existing finds, unrelated to D5.

### THE NUMBERS AS OF THIS HANDOFF — what "unchanged" looks like

Green on `trunk` at `de84d2e2` under gcc, EXCEPT that the full chain has
not been re-run since the last comment-only reformat -- see "WHAT IS NOT
YET VERIFIED" below. If one of these MOVES after a change, that is the
signal; re-pin deliberately and say why.

| gate | value |
|---|---|
| unit tests | 628 tests / 4,263,151 assertions, 0 failures |
| x86_64 fixtures (`tests/corpus` + `tests/programs`) | **672 / 672** |
| arm64 corpus, and under `CGF_SPILL_ALL=1` | **76 / 76** each, both ledgers EMPTY |
| tier table (`check_gnu_tiers.sh`) | **26 implemented / 6 parsed-ignored / 8 refused** |
| musl warning sweep | **1259 / 1361** parsed, 102 deferred, 414 oracle-matched, **zero false positives** |
| musl memory sweep | **1276 / 1361** analyzed, 85 pinned deferrals, **zero `-Wmem` diagnostics** |
| ISA driver (`s36_isa_driver.sh`) | 95 corpus files / 570 object checks |
| fuzz digest | `fe3cdda07126f524` (at `--iters 5000`) |
| sanitized 100k | 0 findings; `tests/fuzz/crashes/` holds only `README.md` |
| gcc / clang `make test` | rc=0 both |

**Expect the musl numbers to MOVE on D5** — that is the point of the
deliverable. Both are exact pins; changing them is a deliberate act with a
sentence of justification, not a silent re-run.

### WHERE THE CODE IS, for D5's three items

The attribute machinery is one table plus one record plus three functions
that must each enumerate every field. Adding an attribute means touching all
of these — and **`gnu_attrs_merge` and `gnu_attrs_any_symbol_property` sit
next to each other on purpose**, because forgetting one means forgetting the
other in the same glance (that omission is how seven attributes were once
silently dropped on function parameters).

| what | where |
|---|---|
| attribute classification (`GA_IMPLEMENTED`/`GA_IGNORE`/`GA_UNSAFE`/`GA_UNKNOWN`) | `src/parse/gnu_attrs.def` |
| the parsed record | `GnuDeclAttrs` in `src/attr.h` |
| merge across declarations | `gnu_attrs_merge`, `src/parse/attr.c` |
| "is this a symbol property?" | `gnu_attrs_any_symbol_property`, same file |
| per-attribute parse handlers + dispatch | `src/parse/attr.c` (`parse_*_attr`, then the `GA_IMPLEMENTED` switch) |
| `__x__` spelling normalizer | `gnu_attr_norm_name`, `src/parse/attr.c` — shared by the classifier and the `format` archetype |
| carried onto the Symbol | `carry_symbol_attrs` + `gnu_attrs_merge(&sym->gnu, &d->gnu)`, `src/sema/decl.c`. **`Symbol.gnu` is a whole `GnuDeclAttrs`**, so a new field rides along free |
| builtin table | `src/builtins.def` — no accept-anything fallback by design |
| builtin lowering | `src/lower/expr.c`, beside `SEMA_BUILTIN_EXPECT` |
| the predefine table | `pp_predefine_all` (grep for the policy comment saying NO `__GNUC__`) |
| binary128 today | `src/util/softfp.c`, `src/lower/f128.c`, `src/rt/` (`__*tf3`) |
| tier table + its gate | `docs/gnu-extensions.md`, `scripts/check_gnu_tiers.sh` |

**Fixture placement decides which bugs a test can see.** `tests/corpus` runs
on x86_64, under `CGF_SPILL_ALL=1`, AND under both arm64 lanes; it is outside
`FE_FUZZ_CORPUS`, so it costs no digest repin. `tests/programs` runs under
none of those and costs a 100k. Executable fixture → corpus; compile-failure
or warning-count fixture → programs.

**Every fixture needs its SILENT half.** Most of these checks could fire on
everything and still pass a firing-only test. D3's five each carry one: an
undeprecated sibling member and enumerator, a call in a condition, a variadic
function with no attribute and no table row, a null in an unlisted position,
a callee that really can fall through.

### Sprint 55 against its own Definition of Done

| DoD | state |
|---|---|
| 1. tier table, every row fixtured, CI-gated | **met** — 23/6/8, `check_gnu_tiers.sh` |
| 2. musl `syscall_arch.h` + `src/internal/` compile clean | **met** — verified directly |
| 3. extended asm O0–Os both targets + early-clobber execute fixture | **met** |
| 4. **16 attributes** with semantics tests + packed differential | **met** — 16 of 16 |
| 5. `__GNUC__=8` in gnu17, absent in c17, both fixtured | **THE ONLY OPEN ONE** ← D5 |
| 6. deferred constructs hard-error naming the sprint | **met** |
| 7. zero new warnings building cgf itself; bootstrap lanes green | **met** |

### BACKGROUND: what D4 and D3 closed, and what they found

Read these only if you need the reasoning behind a decision. §0a above is the
live work.

### D4 — what is DONE

- statement expressions `({ ... })` — `56a94d14`
- `typeof` / `__typeof__` / `__typeof` + `__auto_type` — `2245382c`
- `__builtin_types_compatible_p` / `__builtin_choose_expr` — `4d433873`
- `__thread`, `__extension__` — `tests/corpus/x86_64/int/gnu_thread_extension.c`.
  `__thread` is a pure ALIAS: it produces the same `AST_SC_THREAD_LOCAL` as
  `_Thread_local`, proved by diffing the emitted assembly for both spellings
  (**byte-identical**). `__extension__` is SWALLOWED at the top of
  `parse_unary_expr`; the declaration position has worked since Sprint 9.
  **The fixture needs `// ENV: CGF_AS=0`** — the bundled assembler has
  neither `%fs:` nor `@tpoff` on x86 (TLS-004) and the driver refuses by
  name. That line is INERT on arm64, where the lane sets `CGF_AS_PATH` and
  an explicit path beats a mode, so the fixture still executes through the
  bundled assembler there. Its absence is what made the first full run come
  back 89/90 under both compilers with arm64 clean — an asymmetry that reads
  like an x86 codegen bug and is really assembler routing.
- `[0]` arrays and `__builtin_constant_p` — **already worked**, found by a
  13-line survey before any planning. PROBE THE LIST BEFORE PLANNING IT.

### D4 IS CLOSED. What the last four items cost, and what they found

- **case ranges** `case lo ... hi:` — `fbf381d7`. Ranges do NOT enter the IR
  switch value table (`case 0 ... 1000000` would be a million entries); each
  becomes one bounds test, `d = scrut - lo` then unsigned `d <= hi - lo`.
  The wrap is what makes a negative range work.
- **`a ?: b`** — `2b669d91`, plus the two `-pedantic` pedwarns gcc emits for
  it and for case ranges, plus the `__extension__` suppression they require.
- **`__label__` and empty structs — REFUSED** (`f5ce6442`), with the reason
  MEASURED rather than argued. See below; this is the interesting part.
- **`__builtin_offsetof` array designators and `,##__VA_ARGS__` ALREADY
  WORKED.** Found by a survey before any planning — the same lesson `[0]`
  arrays taught. PROBE THE LIST BEFORE PLANNING IT; it is now 4 for 4.

**THE THREE THINGS WORTH CARRYING FORWARD:**

1. **Implementing case ranges found THREE pre-existing gaps in switch label
   checking, two of them REQUIRED diagnostics** (task #123). `case 1:` twice
   and `default:` twice were both accepted silently, picking the first. They
   land with ranges because overlap between two ranges, between a range and
   a plain label, and between two plain labels are the same question about
   the same intervals — one list answers all three. The out-of-range case
   label (`-Wswitch-outside-range`) is still open.
2. **THE ENUMERATION HAZARD, again, in the lowering.** Three loops had to
   agree on which cases are table entries; I taught the sizing loop about
   ranges and not the filling loop, so the array was sized for the singles
   and written with all of them. The overflow corrupted the arena and IR
   verify reported a branch to block id 3894. One predicate now.
3. **A GATE I ADDED WAS VACUOUS, and fixing it found an older one.**
   `check_gnu_tiers`'s refused-row check grepped src/ for a token, and the
   COMMENT explaining a refusal contains the same words — so mutating the
   message away left the gate green. Tightening it to require the token in a
   STRING LITERAL exposed that the `mode` row had been vacuous since it was
   written: its token `mode(` never appears in that refusal at all. All
   seven rows are mutation-verified now.

**A LANE-CONTENTION TRAP THAT LOOKS LIKE A COMPILER BUG.** The sanitized
100k came back with EIGHT "hang (spawn timeout)" findings, which the harness
wrote into `tests/fuzz/crashes/` where their presence fails the build. Not
one was real: every case compiles in under 15ms on both the plain and the
sanitized compiler, four of the eight are EMPTY FILES, and the seeds run in
consecutive runs (35990-35993). The cause was mine -- I ran the two musl
sweeps concurrently with the fuzz run, which is the rule this file already
states. **A fuzz finding whose only symptom is a TIMEOUT is a claim about
the machine as much as about the compiler**: reproduce it standalone before
believing it, and check whether the input is even non-empty.

**REFUSING IS A RESULT.** Both refusals rest on measurement, and the empty-
struct one is the sharper: gcc gives them size zero, and `struct E arr[3]`
then has `&arr[0] == &arr[1]`. So the extension does not add a size, it
breaks the distinct-address property the shared alias service and the
memory-safety lattice are both built on. Demand was measured first — musl 0,
glibc's C headers 0 (every `/usr/include` hit is C++), Linux uapi 1 inside
`__DECLARE_FLEX_ARRAY`. Its old message named Sprint 55, the sprint that
examined and declined it, so `check_deferrals` would have flagged it at
close: **a deferral to the CURRENT sprint must be resolved before it ends,
in one direction or the other.**

### The historical D4 list, with the semantics that were measured

**Do not re-derive these. They are gcc-measured and some are
counterintuitive.**

- **case ranges** `case 1 ... 3:` — inclusive at BOTH ends. A REVERSED range
  (`3 ... 1`) is a gcc **warning** ("empty range specified"), not an error.
  A single-value range (`1 ... 1`) is fine.
- **`a ?: b`** — the left operand is evaluated **exactly once**, on both the
  true and false paths (measured with a call counter both ways). The refusal
  is at `src/parse/expr.c` in the `PUNCT_QUESTION` case.
- **empty struct / union** — `sizeof` is **0**. ⚠ **READ THE EXISTING
  COMMENT AT `src/sema/decl.c` (`any_named`) BEFORE IMPLEMENTING.** Whoever
  wrote it refused zero sizes deliberately: they break the "distinct objects
  have distinct addresses" property later passes assume. This is the one
  remaining D4 item with a documented hazard — check what layout, alias
  analysis and the memory-safety engine do with a zero-extent object rather
  than assuming gcc's acceptance settles it.
- **`__label__`** — local labels; parser + scope work.
- **`__builtin_offsetof` array designators** — separate, smaller.
- **`,##__VA_ARGS__`** comma-swallow — a Sprint 5 deferral, and the only
  item that is PREPROCESSOR work. The sprint file corrects itself here: gcc
  accepts it in ALL modes, so match gcc.

### D3 IS CLOSED — 16 of 16

`deprecated`, `warn_unused_result` and `format` landed in `87e4bd39`, all
byte-identical to gcc 16. They were chosen together because each is a
DIAGNOSTIC with no codegen consequence and each already had a registry
warning id waiting — the infrastructure was built, only the wiring missing.

- **`deprecated`** fires at the USE and never at the declaration; the
  DEFINITION of a deprecated function is silent. Six positions.
- **`warn_unused_result`**: `(void)must()` STILL WARNS, the opposite of
  `-Wunused-value` where the cast IS the acknowledgement. The check looks
  THROUGH a cast to void; that asymmetry is why it cannot be folded in.
- **`format`** wires Sprint 39's whole checker to user functions, taking
  precedence over BOTH the builtin name table and `-ffreestanding`.

**A CORRECTION I HAD ALREADY WRITTEN DOWN BEFORE MEASURING PROPERLY.** I
claimed in a source comment that gcc does not support `deprecated` on an
ENUMERATOR in C. It does. My probe put the attribute BEFORE the enumerator
name, where gcc rejects it outright ("expected identifier before
`__attribute__`"), and I had grepped only for `warning:` — so the ERROR was
invisible and its absence read as "gcc is silent here". **A filtered slice
of a compiler's output is not a measurement.** The legal position is AFTER
the name, and our parser did not accept attributes there at all.

`nonnull` and `noreturn` followed in `f1af918f`, and each had one fact worth
keeping:

- **bare `nonnull` is not "no positions", it is EVERY POINTER PARAMETER**,
  so the two forms carry separate state — a zero mask would be
  indistinguishable from "all". And `(int *)0` is NOT a null pointer
  constant by 6.3.2.3p3, yet gcc warns for it, so the check strips an
  explicit pointer cast while `conv_is_npc` stays strict for its other
  caller (which decides the TYPE of a conditional and needs the narrow
  rule).
- **`noreturn`'s value is FALSE POSITIVES REMOVED**, not a diagnostic
  added: a function ending in a call that never returns drew "control
  reaches end of non-void function", and a variable set on every surviving
  path drew "may be used uninitialized". It joins C11 `_Noreturn` and the
  library-name list at ONE decision rather than becoming a second
  mechanism.

`malloc`, `pure` and `const` are deliberately NOT implemented: they are
optimization licenses where a user's lie becomes a miscompile, which is a
different risk class from a missed diagnostic. They stay parsed-ignored.

### Then, in order

1. **D5's blocker 4** (GNU named variadic macros), then the `__GNUC__`
   predefine itself, then re-probe the headers on arm64-macos and FreeBSD.
   Only after every blocker does the predefine go in, because until then
   defining it makes hosted compilation WORSE than it is today.
2. **Then Sprint 55 closes**, and its DoD gate 5 with it.
3. **Sprints 52, 53, 54** — compile speed, codegen quality, perf gates.
   All three are performance work, which is why they were safe to defer.
   Sprint 53 also inherits a MEASURED item from D5: a runtime
   `__builtin_bswap64` is 64 x86 instructions where gcc emits `bswap`,
   because there is no IR opcode for it (see the comment at its case in
   `src/lower/expr.c` for why adding one is a 17-file change).

### THE VERIFICATION RITUAL, and the one lever that saves an hour

Before anything lands: gcc `make test`, clang `make test`, BOTH arm64 lanes
(`test-a64-corpus`, `test-a64-spill-all`), and — whenever the fuzz digest is
repinned — a sanitized 100k (`ASAN_OPTIONS=detect_leaks=0`, and check
`nm -D` for `__asan_init` on BOTH binaries first). For anything touching
warnings or analysis, also `scripts/musl_warn_dryrun.sh <cgfried>` and
`make musl-sweep`; both print exact pinned numbers.

The exact commands, since three of them have a gotcha:

```sh
make BUILD=build-v-gcc   CC=gcc   test          # rc must be 0; FAIL count lies
make BUILD=build-v-clang CC=clang test
make test-a64-corpus && make test-a64-spill-all  # SEQUENTIALLY, never -j together
# sanitized tree: NOT `SAN=1` -- that is not a thing. This is:
make BUILD=build-san-d4 \
  EXTRA_CFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all" \
  build-san-d4/cgfried build-san-d4/fuzz_frontend
ASAN_OPTIONS=detect_leaks=0 ./build-san-d4/fuzz_frontend --iters 100000 \
  ./build-san-d4/cgfried tests/fixtures tests/programs
# the digest gate runs --iters 5000, NOT 2000. Repin with:
./build-d4/fuzz_frontend --hash --iters 5000 ./build-d4/cgfried \
  tests/fixtures tests/programs | awk '{print $4}' > ci/fuzz_sequence_digest.txt
```

**`make test` returning rc != 0 with ZERO `FAIL` lines is NORMAL** — it means
a GATE failed, and gates print their own message. `grep -E "^make.*Error"`
the log and read upward. The ones that have caught me: `check_bans` (it greps
the WHOLE file, so the literal `__attribute__` in a COMMENT trips it),
`check_format`, `check_warn_matrix` (every new registry row must also land in
`.docs/warnings-matrix.md` WITH a fixture), and the fuzz digest.

**`tests/corpus` IS FREE; `tests/programs` COSTS ~20 MINUTES.**
`FE_FUZZ_CORPUS` is `tests/fixtures tests/programs`, so ANY add/delete there
moves the mutation digest and obligates the full 100k. The type-query
builtins alone cost THREE of those. An EXECUTABLE fixture belongs in
`tests/corpus` — it runs on x86_64, under `CGF_SPILL_ALL=1`, and under both
arm64 lanes, and costs nothing. Only compile-FAILURE fixtures need
`tests/programs`. **Batch a feature's fixtures into ONE verification pass.**

Run the suites ONE AT A TIME (two at once on this box produce failures that
do not reproduce), and never edit `src/` while a chain is in flight — it
rebuilds from source. **This is not advisory.** Running the two musl sweeps
alongside a 100k produced EIGHT "hang (spawn timeout)" findings, which the
harness wrote into `tests/fuzz/crashes/` where their presence fails the
build. Not one was real: every case compiled in under 15ms on both the plain
and the sanitized compiler, and four of the eight were EMPTY FILES. **A fuzz
finding whose only symptom is a TIMEOUT is a claim about the machine at least
as much as the compiler** — reproduce it standalone before believing it.

Build trees: pass `BUILD=` a RELATIVE path. An absolute one poisons the tree
(the debug lane concatenates BUILD with the cwd, `.d` files stop matching,
and header changes stop triggering rebuilds — the symptom was 55 clang unit
failures reading `no tag S` that vanished on a clean tree).

### WHAT KEEPS BITING, in the order it bit

1. **THE ENUMERATION HAZARD.** A list that must name every case forgets one.
   Six sites built an `ATY_BASE` from a specifier soup and I patched one —
   `typeof(int) b = 1;` resolved to TY_ERROR while `sizeof(typeof(a))`
   worked. Fix is always ONE helper, never N+1 lines: `soup_fill_identity`,
   `ir_arg_carry_provenance`, `gnu_attrs_any_symbol_property`. Same shape
   found `__volatile` missing from two asm-qualifier loops while its sibling
   `__inline` was present — that one gated every musl TU with atomics.
2. **A CHECK THAT CANNOT TELL "VERIFIED" FROM "NEVER RAN."** A mutation
   whose injected bug did not compile reported PASS. `grep -c FAIL` called
   three build/gate failures clean. A loop label printed `-O2` while never
   passing `$O`. **zsh does not word-split** an unquoted expansion, so
   `cgf $flags file` sent one argument and every fixture answered
   "unrecognized option" — read as "silent, therefore vacuous". READ THE
   OUTPUT, NOT A COUNT OF IT. **PROVE THE BINARY IS THE ONE YOU CHANGED** —
   compare its mtime to `date`; that caught two stale-binary reads.
3. **MEASURE gcc BEFORE WRITING CODE.** It has overruled the sprint file
   FOUR times this sprint, most recently on `__builtin_choose_expr` — the
   file said the unselected arm is "untype-checked beyond parse" and gcc
   type-checks it. Implementing as written would accept code gcc rejects,
   and no good-faith fixture would catch it because every natural test puts
   a VALID expression in the dead arm.
4. **A PLAUSIBLE CALL THAT DOES NOTHING.** `type_qualify(t, 0)` returns the
   type UNCHANGED when asked for zero qualifiers — it adds, it does not set.
   `types_compatible_p(const int, int)` answered 0 until `conv_strip_quals`
   replaced it. Likewise a first dead-branch rule tested the statement
   BEFORE a case label when the scanner had already descended past it: it
   compiled, passed everything, and never fired.
5. **AN OBSOLETE REFUSAL ASSERTION PER FEATURE.** SEVEN so far, and they
   come in pairs — a `tests/programs` fixture AND a unit assertion in
   `tests/unit/test_expr.c`, which fails on a DIFFERENT run than the fixture
   does. Most were CONVERTED to pin a boundary that still errors (file-scope
   `({...})`; `typeof` in ISO mode; `a ?: b`'s `-pedantic` pedwarn); one was
   deleted because nothing remained. The gate tells you a boundary MOVED;
   deciding whether one REMAINS is judgment. **Expect two per D5 feature.**
6. **A FILTERED SLICE OF gcc's OUTPUT IS NOT A MEASUREMENT.** I grepped for
   `warning:` while probing `deprecated` on an enumerator, concluded gcc does
   not support it in C, and wrote that into a source comment. gcc had emitted
   an *error* — my probe put the attribute BEFORE the enumerator name, which
   gcc rejects outright — and the filter hid it. Show errors AND warnings,
   and check the exit code. **Probe the LEGAL position before concluding
   about a feature**; attribute placement is load-bearing in C.
7. **A GATE YOU ADD CAN BE VACUOUS, AND FIXING IT MAY FIND AN OLDER ONE.**
   `check_gnu_tiers`'s refused-row check grepped `src/` for a token, and the
   COMMENT explaining a refusal contains the same words — so mutating the
   diagnostic away left it green. Requiring the token in a STRING LITERAL
   exposed that the `mode` row had been vacuous since it was written: its
   token `mode(` never appears in that refusal at all. **Mutate every gate
   you write, in both directions.**
8. **READ THE RULE, DO NOT COPY ITS NEIGHBOUR.** I repinned the fuzz digest
   at `--iters 2000` because that is what the line above it in the Makefile
   used; the gate runs 5000. Two repins agreed with each other and neither
   agreed with `make test`.
9. **`.docs/sprints/` IS GITIGNORED AND MUST NEVER BE COMMITTED.** `git add`
   refuses it and suggests `-f`. Do not take that hint. Sprint-file
   corrections stay local; record them in the commit message instead.

---

## 0b. D2, EXTENDED ASM — DONE. What it cost, and what it found.

**`cgf` compiles inline asm with operands, constraints and clobbers on both
targets.** The interesting part of this section is not the feature: it is that
the operand slice turned up **three bugs in code that was not the operand
slice**, and that the one bug IN it was invisible to every lane but one.

### What works

Extended asm with operands, on BOTH targets, matching gcc at -O0/-O1/-O2/-Os:

- x86_64 constraint matrix `4 102 77 9 10 6 11 7` — `r`, `i`, `m`, `+r`,
  width modifiers `%b0/%w0/%k0/%q0`, symbolic `%[name]`, `%%`, fixed
  registers (`a b c d S D`), tied operands, named clobbers.
- **musl's syscall shape executes**: `"=a"(r)` tied to `"0"(n)` with `D`/`S`/
  `d` inputs and `rcx`/`r11`/`memory` clobbers, making a real `write(2)`.
- arm64 the same matrix, verified against `aarch64-linux-gnu-gcc` under qemu.

### THE BUG IN IT: an asm operand cannot be spilled, and only one lane looks

**`CGF_SPILL_ALL=1` was wrong on BOTH targets.** Every spilled asm operand
reloaded into the same scratch register:

```
	movq	-32(%rbp), %r11      <- input a reloaded into r11
	movq	-40(%rbp), %r11      <- input b reloaded into r11, clobbering a
#APP
	movl %r11d, %r11d            <- all three operands are one register
```

x86 printed `2 8` where `11 7` is right.

**The fix is an ALLOCATOR FEATURE, not a patch at the reload site**, and that
distinction is the whole lesson. An operand constrained `"r"` must BE in a
register; x86 has exactly two scratches to reload through, so three register
operands cannot be satisfied by reloading AT ALL. No cleverness at the reload
site can produce a register that does not exist. `CgInterval.no_spill`
(`src/cg/shared.h`) says so, and `cg_linear_scan` honours it in three places
that all had to agree: `spill_all` skips such an interval, eviction refuses to
pick one as a victim, and the spill-vs-evict heuristic inverts for it —
someone else goes, whatever the relative lengths. Both backends set it from a
`collect_no_spill` pass over their own `ASM` instructions.

If the pool ever genuinely runs out there is a backstop ICE naming inline-asm
register pressure. gcc reports that case as *"'asm' operand has impossible
constraints"* and we cannot, because the allocator holds no diagnostic context
and no span; reaching it needs more simultaneously-live register operands than
the machine has allocatable registers.

**Mutation-verified**: putting the `spill_all` guard back reproduces
`4 102 77 9 10 6 2 8` exactly — the original wrong answer, not merely *a*
failure.

### A CLAIM I HAD DOCUMENTED WAS FALSE. Both copies are now corrected.

I wrote that early clobber is IMPLIED by this allocator, reasoning that
`cg_intervals_build` extends a def and a use at one instruction point to that
same point, so an output can never share a register with an input. **That is
true of ordinary allocation and was FALSE in the spill path**, where operands
were reloaded into a small fixed scratch set and genuinely collided. It is
true again now, but for a reason that had to be BUILT rather than one that
came for free — `no_spill` keeps asm operands out of that path entirely. The
corrected wording is in `tests/corpus/x86_64/int/asm_operands.c` and the
`case IR_ASM:` comment in `src/cg/x86_64/isel.c`.

**The fixture MOVED to `tests/corpus/x86_64/int/` as part of the fix**, and
that is the durable half. `tests/corpus` is run under `CGF_SPILL_ALL=1` on
x86_64 AND under both arm64 lanes, so the fixture now executes in three places
that spill everything. In `tests/programs` it executed in none of them, which
is exactly why a bug on both targets survived a green suite.

### CI CAUGHT A FIFTH BUG THE LOCAL SUITE DID NOT, and it was two bugs

**`memsafe-musl-zero-fp` failed on the first push** with an ICE
(`lower_irtype on non-scalar type kind 17`, which is `TY_ARRAY`) compiling
musl's `src/network/res_msend.c`. **The local gap was mine: there are TWO
musl lanes and I ran only one.** `test-musl-warnings` and the memory sweep
(`musl_mem_warn.sh`, the `musl-sweep` target) compile different TU sets, and
a green warning sweep says nothing about the memory one.

A STACK TRACE found it in one step where reading would have taken many --
`break cgf_ice; run; bt` showed `lower_local_init` calling itself, which is
the compound-literal path. The trigger is musl's

```c
.msg_iov = (struct iovec [2]){
    { .iov_base = (u8[]){ ql>>8, ql }, .iov_len = 2 },
```

and it turned out to be **two pre-existing bugs stacked, both reachable from
ordinary C with no extension of any kind**:

1. **The ICE.** An array-typed operand reached `lower_scalar_convert`. The
   array-to-pointer decay rule existed ONLY at `AST_EXPR_CAST` -- fine for a
   decay sema materialized as a cast node, useless for one that arrives
   without it, which an initializer element does. The rule now lives in
   `lower_scalar_convert` itself, so every caller inherits it instead of the
   two that happened to be known.
2. **A SILENT WRONG ANSWER THE ICE WAS HIDING.** With the crash fixed the
   fixture printed 183, then 167 -- varying per run, i.e. uninitialized
   stack. `(u8[]){a,b}` **never had its bound deduced from its
   initializer**: `sizeof((int[]){1,2})` reported "incomplete type", and a
   NESTED one allocated too small and stored only its first element.
   `array_size_from_init` had existed for declarations since Sprint 9 and the
   compound-literal path never called it; `sema_array_complete_from_init` is
   now the one rule with two callers.

**Third time in this project that an honest ICE sat on top of a silent
miscompile** (after the VLA reckoning and `_Alignas`). Fixing the crash is
where the search STARTS.

The payoff is large: the musl memory sweep went **733 -> 1084 of 1361 TUs
analyzed**, deferrals 628 -> 277, still zero diagnostics; the warning sweep
gained one TU and three sign-compare warnings, all oracle-matched.

**AND THE PROJECT'S OWN RULE CAUGHT ME.** I wrote the regression fixture's
`CHECK` line from prediction -- `4` where gcc prints `4660`. "Every corpus
expectation gcc-verified before pinning" exists for exactly that, and without
running gcc first I would have pinned a wrong expectation into the corpus.

### AND A SIXTH, FOUND BY PROBING THE FIFTH'S EDGES

Checking what else the bound-deduction fix might touch turned up that **a
file-scope compound literal could not be a static initializer at all**, in
any form -- gcc accepts every one:

```c
static struct S val   = (struct S){1, 2};     /* aggregate value  */
static struct S *addr = &(struct S){3, 4};    /* worked already   */
static int *sized     = (int[3]){5, 6, 7};    /* array, decays    */
static int *unsized   = (int[]){8, 9};
```

Pre-existing, independent of the deduction work (it reproduces on the
pre-change compiler and for the SIZED form too), and **two bugs wearing one
symptom**, which is why fixing the diagnosed half would have left the
complaint standing:

- the ARRAY forms decay to an address, and `constexpr`'s `eval` had NO
  compound-literal case at all, so only the explicit `&(T){...}` route ever
  reached the address-constant code;
- the AGGREGATE form is an IMAGE rather than an address, and the
  static-initializer check in `declare_one` validated it as a SCALAR
  constant -- asking `eval` for a value the literal does not have.

**The audit is the part worth keeping.** I first wrote this up as "a rule on
the explicit path, missing on the implicit-decay path", generalizing from the
`lower_scalar_convert` bug. That was WRONG and measuring disproved it: an
array VARIABLE and a string literal both come through implicit decay
correctly at file scope. The gap was one missing case, not a class, and a
grep finds only four decay-aware sites in the tree. **A pattern inferred from
two instances is a hypothesis; check before telling anyone to go hunting.**

The carve-out is deliberately narrow and pinned by negatives: `= f()`,
`= other_struct`, an automatic-storage literal, an incompatible literal type,
and a pointer to an automatic array all still reject, matching gcc's verdict
on every one.

### THE THREE BUGS IT FOUND ELSEWHERE — none of them in the asm code

**An asm ESCAPES the pointers it is handed, and the memory analysis did not
know it.** `char *p = malloc(16); asm("" :: "r"(p));` was diagnosed
**-Wmem-leak** — a false positive on the shape every libc's `arch/` directory
uses, in a checker whose whole contract is proof-only. A template is opaque
exactly as an unknown call is, and both directions count: an input can be
published, and an output operand is an ADDRESS the template writes through.

**TWO places enumerate instructions here and I fixed the wrong one first.**
`alias.c`'s `mark_escapes` was missing `IR_ASM` and is the one that LOOKS
responsible; adding it changed nothing, because the leak check reads the
lifetime lattice in `memsafe/lifetime.c`, whose own `process_inst` switch had
no `IR_ASM` case either. Both rows are in now, and the alias one carries a
comment saying no fixture reaches it — it stays because `alias_escapes` has
other clients and escaping MORE is the safe direction, not because anything
proves it necessary. **I had already written the fix up as the alias row
before running the suite**; the suite is what corrected it.

**The memory-BARRIER rows do not cover this either, and that distinction is
worth keeping.** `dce`, `dse`, `gvn`, `licm`, `dep` and `vectorize` all gained
an `IR_ASM` row while the operand slice was being written; every one of them
orders accesses. Ownership is a different question, and a pass can be
perfectly ordered while still believing it owns the block. Six correct barrier
rows did not add up to one escape row.

*(The other pass that must name `IR_ASM` and does not need to: the INLINER
copies `in->callee` verbatim, which for an asm is an index into the MODULE's
asm table, so it stays valid across inlining. Checked rather than assumed —
the same field means a function index on a call.)*

**Three warning-engine false positives**, all pre-existing, all made
REACHABLE by extended asm parsing 350 more musl TUs, each measured against
**gcc 8 in the `gcc:8` container AND gcc 16** before a line was written
(`src/sema/decl.c`, `src/sema/warn_expr.c`) — and each now carrying a fixture,
because the musl gate is an aggregate and an aggregate cannot say WHICH rule
regressed:
  1. an ALIAS TARGET did not count as a use, so musl's
     `weak_alias(dummy, __stdout_used)` warned "defined but not used" for
     both variables and functions. Same fact IPO learned separately.
  2. a FILE-SCOPE `volatile` warned as unused; gcc exempts it (the object is
     a hardware register and its existence is the point) but still warns for
     a LOCAL volatile — measured both.
  3. `-Wsign-compare` fired on provably non-negative operands. gcc suppresses
     when the signed side came from an unsigned type NARROWER THAN INT
     (musl's `val[c] >= base`, seven in one file) or is a 0/1-valued logical
     or relational result (`remain > !!f->buf_size`).
- **`tests/warn/corpus-baseline.txt` repinned** to the new musl numbers:
  1066 parsed / 295 deferred / 270 oracle-matched / **zero false positives**.
- **One entry added to `tests/warn/corpus-genuine-divergences.txt`**:
  `lookup_name.c:137 maybe-uninitialized`. `family` really IS uninitialized on
  that switch's default path and our note says so; gcc's silence is the miss.
  Triaged rather than suppressed — weakening a true warning to make a gate
  pass is how a warning engine stops being worth running.
  Seven fixtures cover them: two under `sign-compare/`, two under
  `unused-variable/` (INCLUDING the fire case for a LOCAL volatile — the
  exemption's negative half, so widening it cannot pass silently), one under
  `unused-function/`, and `wmem/leak/nofire-asm-escape.c` for the alias row.
- **New fixtures** `tests/corpus/x86_64/int/asm_operands.c` (executes under
  three spill-all lanes; carries BOTH targets' templates),
  `tests/programs/gnu/asm_mem_output.c`, `asm_two_reg_outputs_refused.c`;
  `asm_operands_refused.c` DELETED because the form now works.
- **`scripts/s36_isa_driver.sh` repinned 83→84 corpus files and 498→504
  object checks** — it asserts both counts exactly, in both directions, which
  is what made the corpus move announce itself instead of silently widening
  the ISA gate's coverage claim.
- **`ci/fuzz_sequence_digest.txt` repinned to `403ee999ec3fb17b`** with the
  sanitized 100k actually run (`__asan_init` confirmed present in BOTH
  binaries first). Note the digest moved TWICE: the corpus changed once when
  fixtures were added to `tests/programs` and again when `asm_operands.c` left
  it for `tests/corpus`.

### Boundaries deliberately drawn, with the measurement behind them

- **One REGISTER output; any number of MEMORY outputs.** A second register
  output would mean widening `CgMirView.inst_def` (one def per instruction)
  across both backends. Counting musl's asm sites for our two targets says
  that is rarely what a second output is: x86_64 has 181 one-output sites
  against 27 two-output, aarch64 172 against 19, and the two-output cases are
  dominated by a MEMORY second output (`"=m"`, `"=Q"` in the atomics) which
  consumes no register. Refused by name, and it is now a row in the tier
  table's REFUSED tier rather than a TODO in a comment.
- `asm goto` and label lists stay refused.

### THE LANE GEOMETRY THAT DECIDED THIS, worth internalizing

Three lanes run `tests/corpus`: x86_64 native, x86_64 under `CGF_SPILL_ALL=1`,
and arm64 under both. `tests/programs` gets none of the spill-all treatment.
So **where a fixture LIVES decides which bugs it can see**, and for anything
touching register allocation that is not a filing preference — a fixture in
`tests/programs` is invisible to the one lane that stresses the allocator.
The rule to carry: if the claim is about registers, the fixture goes in
`tests/corpus` and EXECUTES.

### Build trees, and a rule I broke

`build-s55-asm` was the verification tree and `build-a64dev` the arm64
development tree. **I armed a verification chain against `build-s55-asm` and
then rebuilt that same tree**, which produced `could not spawn compiler` on
two fixtures — not a compiler bug, my own violation of *freeze the tree while
a verification run is in flight*. The clang lane was at 83/83 when it was
killed. Keep dev and verification in separate BUILD dirs.

---

**`cleanup` is DONE** (the eleventh attribute, and the first that is a
LOWERING feature rather than a symbol property). What it taught, because two
of these cost real debugging time:

- **It reuses Sprint 20's VLA scope chain and shares none of its rules.**
  `VlaScope` is `LexScope` now, with two lists. A restore of the outermost
  token subsumes the inner ones; a cleanup call subsumes nothing. `return`
  restores NO VLA token (the epilogue reclaims the frame) and must run EVERY
  cleanup (a call has no epilogue equivalent). Where a scope owes both, the
  CALLS COME FIRST — the pointer handed to a cleanup is into storage the
  restore releases.
- **TWO BUGS IN THE NEW CODE, both the same shape**, both found by executing
  against gcc rather than by reading: a `goto` walk that ran off the top of
  the scope stack and fired an enclosing cleanup TWICE. The first reused
  `label_vla`, which is FILTERED to VLA-bearing compounds and is therefore
  NULL for every label in a function with no VLA. The second recorded only the
  label's innermost compound, which a jump INTO a scope leaves off the goto's
  stack entirely. The answer is the label's whole CHAIN and the innermost
  COMMON compound. **The VLA sibling needs neither fix because 6.8.6.1p1
  forbids the jump that would break it** — an adjacent feature's shortcut can
  be correct only because a language rule protects it, so check for that rule
  before copying the shortcut.
- **A MUTATION CHECK THAT DID NOT COMPILE REPORTED PASS.** The injected bug
  left an unused variable, `-Werror` failed the build, make kept the previous
  binary, and `>/dev/null 2>&1` hid all of it — so the check ran the
  UNMUTATED compiler and passed, which reads exactly like "this fixture does
  not catch that bug". **Never suppress the build output of a mutation
  check.** Same family as `rsync -a` preserving an old mtime so make skips the
  rebuild, which happened in the same session.
- **A pre-existing silent drop, for SEVEN attributes.** The function-parameter
  path warned only if `packed || weak || visibility` — the three that existed
  when it was written — so `aligned`, `alias`, `used`, `section`,
  `constructor`, `destructor` and the asm label had been dropped there without
  a word. `gnu_attrs_any_symbol_property` now enumerates every field and sits
  beside `gnu_attrs_merge`, which carries the same obligation. Bug #116's
  shape exactly: **any list that must name every attribute is a silent-drop
  waiting to happen — put it next to the other one.**

This file is the *transferable* part of what was learned building all of it:
the traps that cost real debugging time, the invariants that look like style
but are load-bearing, and the ritual the work follows. It is not a substitute
for the sprint files — it is the thing that stops you re-learning what already
hurt.

---

## ARCHIVE BOUNDARY — everything below is historical

The remainder of this file preserves pre-Sprint-54 implementation history and
lessons. Its peer-level headings, including “Current position”, “THE NEXT
WORK”, “UNDER WAY”, and old `ci/closed_sprints.txt` values, describe the state
when those sections were written. They are not current instructions. For all
resume decisions and current sprint/ratchet state, §0a above is authoritative.

## 0. Where the record actually lives (READ FIRST)

Three tiers, and **two of them are gitignored**:

| What | Where | In git? |
|---|---|---|
| Reality snapshot: what is implemented, sprint by sprint | `AGENTS.md` (copy: `CLAUDE.md`) | **NO** |
| Per-sprint plan + appended implementation notes | `.docs/sprints/**/*.md` | **NO** |
| Debt ledgers (XFAIL, afs-ld ELF gaps) | `.docs/audits/*.md` | yes |
| This file | `.docs/HANDOFF.md` | yes (negated in `.gitignore`) |

**If you are on a fresh clone, `AGENTS.md` and the sprint files are not
there.** They are the most valuable documents in the project. Get them
from the previous machine before starting; without them you have the
code and this file and nothing else.

`AGENTS.md` is the honest snapshot — one section per sprint, written
*after* implementation, recording what actually happened including the
corrections to the sprint file's own text. Keep updating it (and `cp
AGENTS.md CLAUDE.md`). **Never commit either.**

---

## 1. Historical position at the end of Sprint 51

- **Sprints 0–51 CLOSED; Phases 1–9 closed. Sprint 55 is UNDER WAY** — see
  §1b-1's *WHERE THE WORK IS* for the resume point. Phase 10 (second backend
  and targets) is under way on top of the completed preprocessor, frontend,
  sema, IR, x86_64 backend, driver, optimizer, warnings, and memory-safety
  phases. Sprint 55 was taken OUT OF NUMERICAL ORDER because 28 deferrals
  pointed at it and it blocks hosted compilation on macOS and FreeBSD plus
  the musl campaign (Sprint 57) that precedes the bootstrap (Sprint 58).
- **`--target=` and `--sysroot=` exist now** (Sprint 51 D1) over the closed
  five-target set, so the compiler's architecture is no longer the target.
  `cgf_target_selected()` is the target, `cgf_target_host()` is the host, and
  `scripts/check_target_seam.sh` gates the split. Do not reintroduce a host
  sniff in target-dependent code.
- **`_Thread_local` works** on both Linux targets (local-exec). It did not,
  silently, for fifty sprints — see §1b.
- **Sprint 51 is CLOSED** (all seven deliverables) and **Sprint 55 is under
  way** — see §1b-1. `ci/closed_sprints.txt` is 51; raising it forced an
  audit and found only the `-g` gate the DWARF work had already removed.
- **Inline asm: BASIC works, operands refused by name.** An asm template is
  TARGET-SPECIFIC ASSEMBLY, so a fixture in `tests/corpus` — which the arm64
  lanes re-run under qemu — must select on `__x86_64__`/`__aarch64__` or it
  cannot pass there. Mine did not, and both lanes said `unknown mnemonic
  addl`.
- **ELEVEN GNU attributes are implemented**: `weak`, `visibility`, `packed`,
  `aligned`, `alias`, `used`, `__asm__("name")` labels, `section`,
  `constructor`, `destructor`, `cleanup`. §1b-1 has what each one taught, and
  *WHERE THE WORK IS* has what to do next and why.
- **Two bugs that had SHIPPED were found by adding the tenth**, and the second
  is the one to remember. `section` was printed in the IR text and parsed
  nowhere, so `-emit-ir` ICEd on any program using it. And **every symbol
  attribute was dropped on a definition with a prior plain declaration** —
  `weak` emitted GLOBAL where gcc emits WEAK, which is musl's `weak_alias`
  shape and exactly what the code comment claimed to handle. Both were
  invisible because every fixture writes the attribute and the definition
  together, and `section`'s only tests are `ASM_CHECK`s that never touch the
  IR text. **When a new feature misbehaves in a shape an old one shares, test
  the old one in that shape before believing your own explanation** — this one
  first read as gcc's known priority-dropping bug and was a broader bug of
  ours underneath it.
- **arm64-linux emits DWARF and `.eh_frame`**; `addr2line` resolves a linked
  executable. `src/cg/debug.c` is now the ONE line-table and CU-DIE emitter
  for every target, which also makes task #93's variable DIEs a write-once
  job rather than per-backend.
- **The GNU attribute surface is classified** by what ignoring each one
  costs; `weak` and `visibility` are implemented and agree with gcc's symbol
  table on both targets. **`packed` and `aligned` are both implemented**
  (`.docs/audits/packed-layout.md`, `.docs/audits/aligned-layout.md` — each
  records what the measuring found that the plan had not anticipated).
- **`_Alignas` on an OBJECT works.** It did not, on any target, for the whole
  project: ISO C11 6.7.5 validated and then discarded. Members had worked
  since Sprint 14, which is what hid it.
- **`.set` works on BOTH afs-as paths** — PR #28 for x86 (`fb50d2f`), PR #29
  for arm64 and Mach-O (`a6690e2`). AS-SET-002 is CLOSED, the driver's by-name
  refusal is gone, and `attr_alias.c` lives in `tests/corpus` so aliases
  EXECUTE on arm64 through the bundled assembler.
- **A ledgered recipe is a hypothesis, not a plan.** AS-SET-002's was wrong in
  its first step (the deferral it prescribed already existed; the real gap was
  one variant in `may_resolve_with_labels`) and right in its trap (the
  emission cursor). Re-measure before trusting a recipe you wrote earlier —
  and REBUILD first: the first reproduction ran a stale
  `afs-as/target/release/afs-as` and reported an error the source no longer
  produces.
- **Known-wrong-but-shipping is now ZERO.** Everything open is a NAMED
  refusal or a deliberate deferral — see §1b-1's *WHERE THE WORK IS*.
- **Controlling expressions are constrained** (was task #108, and it was
  bigger than its title). All twelve scalar positions — `if`/`while`/`do`/
  `for`, `?:`, `&&`, `||`, `!` — were unchecked for BOTH void and aggregate
  operands, and both failures were silent: a void operand became
  `icmp ne i32 undef, 0` (branch on an undef), an aggregate became
  `icmp ne ptr @s, 0` (an address, never null, so `if (1)`). `switch` takes
  the stricter integer rule; a pointer or float there used to reach lowering
  and ICE in the IR verifier.
- **THE EXEMPTION IS THE HARD HALF of any new constraint.** `if (arr)` and
  `if (fn)` are legal — an array or function converts to a pointer — but a
  STATEMENT condition is typed WITHOUT decaying, so they arrive undecayed and
  the first draft rejected correct C. Lowering was already right about them
  (that address is never null, so the condition is always true), which is why
  the `_ok` fixture EXECUTES rather than only compiling.
- **Never grep for a word that appears in both success and failure.** I
  checked whether `switch` was already covered with `grep -c error`, saw a
  non-zero count, and excluded it — the count was the ICE's own
  "internal compiler error" line. The 100k fuzz found what that missed.
- **Const globals reach read-only memory**, as of the `.rodata` work. One
  shared rule (`cg_global_segment`) both backends call: const with no
  relocation is `.rodata`, const with one is `.rodata` without PIC and
  `.data.rel.ro` with it, because under PIC the loader must WRITE that word.
  Mach-O spells the pair `__TEXT,__const` / `__DATA,__const`. **macOS is
  always position-independent** whatever the flags say
  (`cgf_target_always_pic`) — getting that wrong puts a loader-written
  pointer in `__TEXT`, which is read-only AND code-signed. Constness is the
  object's own qualifier looked through ARRAY-ELEMENT qualification (6.7.3p9);
  a const member of a non-const record does not count, matching gcc.
- **The memory summary tables now select on SHAPE, not on the name alone.**
  Both matched a callee with `strcmp` and nothing else, so `strcpy(buf, "")`
  with no `#include` — whose implicit declaration returns int — attached the
  row's pointer facts to an integer result and ICEd in the alias validator.
  Three ICEs, two tables, pre-existing since at least Sprint 47; found by the
  100k fuzz lane gating unrelated work (seed 64271). Sprint 39's format table
  had always done this correctly; gcc calls the mismatch
  `-Wbuiltin-declaration-mismatch`. **Any new table keyed on a libc name owes
  the same check**, and its fixture owes BOTH directions — dropping a row is
  the safe answer, so a too-strict gate disables the analysis silently.
- **The runner's scratch is per-invocation now** (was task #110). It
  defaults to a directory beside the runner binary, so two BUILD trees are
  isolated without asking, and `CGF_TEST_WORK` overrides it — needed because
  the two arm64 lanes share one `$(BUILD)/cgf-test`, which the binary's own
  location cannot separate. `make -j test-a64-corpus test-a64-spill-all` now
  gives 63/63 twice; it used to fail nearly the whole corpus with "the
  assembler rejected cgfried-generated assembly … line 0". check_bans keeps
  the literal out. **Third instance of this shape** after ppfuzz's 128 phantom
  findings; `tests/unit/` still has a benign one (task #113).
- **FREEZE THE TREE while a verification run is in flight.** Editing or
  rebuilding under a running `make test` produces results from a tree that
  never existed. It cost two discarded runs in one session — both mine, not
  the harness's — and the tell is a lane failing in a way that does not
  reproduce.
- **VLAs work on both targets**, as of the deferral reckoning (§1b-1). They
  did not: arm64 ICEd on every one and x86 silently miscompiled any VLA in a
  function that also passed arguments on the stack. Multidimensional VLAs
  and pointers-to-VLA were wrong too.
- The arm64 e2e corpus is **63/63**, and **63/63 again under
  `CGF_SPILL_ALL=1`**. Keep both green; the spill lane exists because exactly
  one fixture at one level once caught a stale NZCV producer index. They may
  now be run in PARALLEL — each gets its own scratch (task #110).
- Verified at the end of the Sprint 51 session, sequentially and locally
  (never two suites at once — §1b-2): gcc and clang `make test` both rc=0,
  **625 unit tests / 4,263,106 assertions**, 514 and 477 fixture profiles,
  arm64 corpus and spill-all 55/55 each, ABI differential 304/304 per Linux
  target in both directions. Re-verified after the VLA campaign: same unit
  counts, 516 and 477 fixture profiles, arm64 corpus and spill-all **57/57**
  each, musl warning lane unchanged at 716/1361 parsed / 645 deferred / 186
  oracle-matched / zero false positives. **Latest full local verification**
  (after `section`, the summary-table shape gate, `.rodata`, AS-SET-002, the
  scalar/switch constraints and the runner scratch): gcc and clang
  `make test` both rc=0 at **626 unit tests / 4,263,123 assertions**, arm64
  corpus and spill-all **63/63** each AND 63/63 with both lanes run in
  PARALLEL, memsafe 90/90 + interproc 50/50 + foundation 14, safe-mode 56/56,
  e2ediff 10/10, a64 objdiff 20 identical / 0 pinned, x86 objdiff 38/38,
  c-testsuite 215/220 with 0 new, 100k sanitized fuzz 0 findings, safe-dogfood
  102 TUs / zero exemptions. All 14 CI jobs green on `trunk`.
- **After `cleanup`** (both lanes rerun end to end, sequentially): gcc and
  clang `make test` both rc=0, **626 unit tests / 4,263,124 assertions**,
  565 program fixtures, 477/477 warning, 90/90 memsafe + 50/50 interproc,
  56/56 safe-mode, objdiff 38/38, e2ediff 10/10, c-testsuite 215/220 with
  0 new, musl unchanged at 716 parsed / 645 deferred / 186 oracle-matched /
  zero false positives, **arm64 corpus and spill-all 66/66 each with both
  ledgers still empty**, and a sanitized 100k frontend fuzz at 0 findings
  (digest repinned to `076a1a46ac3703b4` for the 8 new corpus files; both
  binaries checked for `__asan_init` before the run was believed).
- `cgf hello.c -o hello && ./hello` works on **x86_64-linux AND
  arm64-linux**. On arm64 the compiler emits its own assembly, assembles it
  with the bundled afs-as into ELF objects byte-identical to
  `aarch64-linux-gnu-as`, and links. The e2e corpus is 43/51 there, with the
  one gap ledgered by cause — the same split on real hardware and under
  qemu, so it is a backend gap, not an emulator artifact.
- **Sprint 49 is CLOSED — all seven DoD gates met as written. Sprint 50
  (arm64-macos) is IN PROGRESS; see §1b.** See the DoD audit table at the end
  of `.docs/sprints/10-backend-arm64/s49-arm64-linux.md` before assuming any
  arm64 property holds.
- `cgf hello.c -o hello && ./hello` works. Multi-TU works. Hosted
  programs against system glibc work on Arch *and* Debian/Ubuntu.
- `-g` emits DWARF v4 line tables and every object carries `.eh_frame`;
  gdb break/next/four-frame backtraces work at `-O0` and `-O2`, including
  GDB 15's shorter x86 prologue scanner via an entry definition row plus
  `prologue_end` on the first executable row.
- Phase 7 is closed: every `-O` level reaches a real pass manager. O1 runs
  mem2reg → sparse conditional constant propagation → exact simplify →
  block-local CSE → DCE → general CFG cleanup. O2+ adds GVN, DSE and bounded
  jump threading, then internal-only IPO and a bottom-up SCC inliner. O2 adds
  canonical natural-loop analysis, LICM and affine-IV strength reduction; O3
  adds exact bounded full unroll. O2 now adds edge-sensitive BCE; O3 adds
  factor-four constant partial unroll, loop unswitching, and conservative
  adjacent-loop fusion behind a shared region cloner and affine dependence
  service. O3/Ofast now add a constant-trip, statically proven SSE2 loop
  vectorizer; Ofast additionally licenses FP reductions through one documented
  fast-math bundle. Scalar, loop and unroll groups are separate fixpoints so CFG
  cleanup cannot oscillate with canonicalization. The
  50-program corpus remains behaviorally equal across O0/O1/O2/O3/Os/Ofast.
- Sprint 37 opens Phase 8 with a 157-row warning registry, GCC-specificity
  option policy, mandatory diagnostic suffixes, location-sensitive GCC
  diagnostic pragmas, macro/system provenance, strict runner assertions, and
  a complete 222-row GCC 8 parity matrix. Existing frontend warnings are
  migrated onto the same policy engine. Warning-option classification and
  pragma-name validation are centralized in `src/warn/warn.c` and protected by
  `scripts/check_warn_seams.sh`; the manual is `docs/warnings.md`.
- Sprint 38 implements the AST/sema warning set: unused/shadow/conversion,
  prototype/K&R/VLA, switch/return, expression-shape, range and indentation
  diagnostics. PP comment metadata drives the exact GCC 8 fallthrough levels;
  conversion proofs reuse target widths and `softfp`. The suite has 166
  frontend fixtures and 197 warning fixtures overall. A real GCC 8 container
  lane reports 179 exact matches, 18 documented divergences and zero
  unannotated differences. The strict same-mode musl pass compiles 706/1,361
  sources, explicitly defers 655, finds 181 oracle-backed warnings and zero
  false positives.
- Sprint 39 adds the dedicated `src/warn/format.{c,h}` checker for printf,
  scanf, strftime and strfmon grammars. Builtin recognition is target-,
  linkage- and rough-signature-aware, including every fixed parameter; all
  conditional literal alternatives are checked; default promotions,
  positional operands, output-pointer qualifiers, target wchar types,
  GNU/FreeBSD rows and glibc `__isoc99_*` redirects are pinned. POSIX
  `strfmon` fill/flag ordering, decorated percent rejection and all GCC 8 y2k
  conversions are regression-tested. The 64-row type matrix generates 128 fire/nofire
  fixtures, and the complete format tree is 203/203. The real GCC 8 lane now
  covers 400 warning fixtures: 379 exact, 20 annotated, one narrowly normalized
  Cgfried-only unbounded-scanf extension and zero unannotated differences.
  musl remains zero-false-positive, and a new TinyCC lane is also
  zero-false-positive over every source the current frontend accepts.
- Sprint 40 closes Phase 8 with cloned-IR flow analysis for definite and maybe
  uninitialized reads, self-init, unreachable regions, non-void falloff and
  infinite recursion. Lowering records source provenance that optimized IR
  cannot reconstruct; mem2reg supplies pre-rewrite definite-assignment facts;
  diagnostics remain byte-stable across O0/O1/O2/O3/Os. The flow corpus is
  77/77 across 385 level runs, the complete warning tree is 477/477, and the
  real GCC 8 lane has zero unannotated differences. A bounded CFG workspace
  reduced the 1,200-use review stress case from 23.013s to 0.455s.
- Sprint 41 opens Phase 9 with the shared memory-analysis substrate. The alias
  service now owns stable allocation sites, byte-offset hulls through memory,
  stored-pointer contents, and transitive reachability for both optimizer and
  memsafe clients. `src/memsafe/` adds the five-state lifetime lattice,
  persistent source-qualified traces, the eleven allocation-family rows, and
  bounded path splitting at 8 states / 256 splits / 4 predicates. The driver
  runs it on a dedicated read-only post-opt module only under
  `CGF_MEMSAFE_DUMP=1`; default behavior and generated assembly remain
  byte-identical, and `-Wmem` is still unknown.
- Sprint 42 ships the first user-visible memory-safety layer. `-Wmem` is
  default-on and reports proof-only intraprocedural use-after-free,
  double-free, leak, constant/affine OOB, uninitialized heap read, and
  free-nonheap findings; `-Wmem-realloc-zero` is opt-in and
  `-Wmem-strict` holds pass-to-unknown UAF. Realloc success/failure paths are
  correlated, every diagnostic has an ordered proof trace, and exact warning
  policy/pragma behavior is fixture-pinned. The semantic corpus is 89/89 with
  11 exact trace sequences. The musl gate analyzes 733/1,361 pinned TUs,
  explicitly baseline-defers 628 pre-Sprint-55 GNU-syntax TUs, emits zero
  memory warnings, and completes in about 13 seconds.
- Sprint 43 makes those checks interprocedural through deterministic bottom-up
  summaries over the shared callgraph. Five checked `cgf_*` ownership
  attributes, a 55-row libc/`FILE *` summary table, write ranges and
  multi-parameter return aliases feed call-site state without inlining. The
  default-on annotation-mismatch lint keeps declared contracts honest. The
  new corpus is 50/50 with 13 exact traces; musl remains zero-diagnostic.
  `<cgfried/memsafe.h>` is portable under host GCC/Clang, and `make install`
  now copies the whole include tree to the path used by installed compiler
  discovery, resolving the reported installed-`cgf` `stddef.h` failure.
- Sprint 44 ships `-fcgf-safe`: one exact-emission memsafe traversal
  statically discharges proven accesses and terminal-splices opaque checks for
  the residue, including the backend-generated 24-byte `va_start` write. Nine
  GNU ld wrappers add 32-byte headers, canaries, poison and a 1,024-block/8-MiB
  quarantine. Private registry lookup preserves the foreign-pointer
  no-false-abort law; diagnostics are stdio-free and deterministic. Runtime
  coverage is 27/27 across seven O0/O2 failures, correct and mixed programs,
  static link, threads, trap mode and real-C discharge accounting; all three
  interim benches are below the 2.5x time/2x RSS budget. Fresh GCC, Clang and
  complete ASan+UBSan suites pass. Valgrind 3.25.1 reports zero errors/leaks
  over the seven instrumentation units; the runtime executable has no definite
  or possible leaks, with only its bounded quarantine still reachable.
- Sprint 45 ships GCC-compatible parseable fixits and copy-only application to
  `*.cgf-fixed`, retaining original source bytes so CRLF, missing final newline,
  UTF-8 byte columns, physical aliases and symlinks are handled deterministically.
  Conflicting/touching edits are withheld and advisory edits are never applied
  by `=all`. Four conservative transform families cover early-return leaks,
  bounded copy APIs, missing allocation checks and `sizeof` mismatches; every
  family has explicit no-fire cases and no path suggests `strncpy`. Inserted
  identifiers are resolved conservatively at their lexical point: active
  `snprintf`/`free` macros and local shadows suppress edits, while annotation
  macros must match the shipped replacement-token shapes. A `free` prototype
  appearing only after the return cannot authorize an earlier suggestion.
  `-ftrivial-auto-var-init=zero|pattern` lowers after warning analysis, including
  dynamic VLA initialization, while must-fact annotation inference provides the
  mechanical annotate-then-ratchet workflow. The dedicated lane proves 12 exact
  parseable records, original-byte preservation, GCC sanity, O0/O2 equivalence
  and the annotation round trip.
- Sprint 46 ships the documented `-fsafe` per-TU contract. It composes
  instrumentation, default-tier memory errors and zero auto-init; rejects six
  unmodelled construct families with deployment alternatives; permits only the
  exact pointer-derived `uintptr_t` grammar; and emits a versioned ELF safety
  note that is validated before safe links. `make safe-dogfood` rebuilds all
  91 compiler TUs, verifies every marker and smoke-tests the result with zero
  exemptions. Dogfood found and fixed the call-site allocator
  ceiling, the `%rdx`/`%rcx` fixed-register conflict, fabricated-pointer map
  values and automatic-address constant folding. The contract truthfully
  defers stack/global spatial instrumentation; VLA acceptance is not described
  as a runtime-bound guarantee.
- Sprint 47 opens the ARM64 backend with verified pre-RA MIR, exact
  instruction selection, ABI-neutral call metadata, shared target-independent
  liveness/linear scan, SP/XZR form checking, memory pairing, and monotone
  branch relaxation. The contract is intentionally pre-RA: Sprint 48 owns
  AAPCS64 and physical allocation, Sprint 49 owns assembly/object/execution,
  and Sprint 51 owns public cross-target routing. Ten `.cgfir` modules pin 93
  coverage labels as exact deterministic full snapshots; they are labels, not
  93 independent fixtures. The assembly oracle compares afs-as Mach-O and GNU
  as ELF `.text` bytes for a fixed fragment plus all 5,334 logical immediates.
  Constant bulk-memory expansion is capped at 64 KiB; larger/dynamic forms
  fail immediately naming Sprint 49.
- **Next action: Sprint 48** —
  `.docs/sprints/10-backend-arm64/s48-arm64-regalloc-abi.md`, adding ARM64
  register allocation and AAPCS64 lowering.
- **Pending parallel-test follow-up:** `tests/fuzz/ppfuzz.c` still writes every
  run to `build/fuzz-work/case.c`. Two concurrent full suites can replace that
  file between the Cgfried and gcc oracle runs and report false differentials.
  Sprint 41 validation reproduced this with concurrent Clang/sanitizer runs
  and proved both isolated 2,000-case runs clean. Give each process/build a
  private work directory before relying on parallel local `make test` runs.

Metrics to compare against after your changes (all must hold or improve):

```
unit: 598 tests, 4262592 assertions, 0 failures
cgf-test: total=505 pass=505 fail=0 xfail=0 xpass=0 skip=0 config=0
memsafe warning fixtures: 89/89; exact ordered trace sequences: 11
interprocedural memsafe fixtures: 50/50; exact ordered trace sequences: 13
focused memsafe units: 52 tests / 764 assertions
safe runtime: 27 checks, 0 skips; three interim benches within 2.5x/2x
autofix transforms: source-copy, four transforms, auto-init and annotation ratchet green
musl -Wmem gate: 733 analyzed, 628 pinned deferrals, 0 memory diagnostics, <90s
format warning fixtures: 203/203; flow warning fixtures: 77/77; all warning fixtures: 477/477
format matrix: 64 semantic rows / 128 fire+nofire fixtures
GCC 8 warning differential: 409 exact + 36 normalized CGF-only + 32 annotated, 0 unannotated
safe mode: 56/56 fixtures; 8 accept + 8 reject uintptr cases; mixed-link green
safe dogfood: 91/91 object notes; 0 symbol exemptions; smoke green
musl warning dry-run: 716 parsed, 645 deferred, 186 genuine, 0 false positives
TinyCC warning dry-run: 12/30 parsed, 18 deferred, 0 format warnings, 0 false positives
warning matrix: 222/222 raw GCC 8 C rows accounted for
OPT_EQ corpus: 51/51 at O0/O1/O2/O3/Os/Ofast; verifier-after-each also green
ctestsuite_diff: 220 files, 215 agree, 5 known-deferred, 0 new, 0 xpass
header_diff: 148 macro/type lines byte-identical to gcc
rt_diff: 2317 result lines identical to libgcc
driver_matrix: 39/39 rows agree with gcc
objdiff: 38/38 · e2ediff: 10/10 · afsld lane: 12 fixtures
debug_info lane: 81 checks with tools/gdb; 6 addr2line rows
pp_dm_check: 182 predefines checked; __CGFRIED__=1; __GNUC__ absent
memsafe foundation: 14 deterministic fixtures; 17 focused units / 264 assertions
frontend fuzz digest: e39a0b1f9c71243f; 2,000 sanitized smoke iterations, 0 findings
ARM64 MIR: 10 modules / 93 exact coverage labels; 1 GiB bulk expansion rejected boundedly
ARM64 assembler differential: 1 fixed fragment + 5,334 logical immediates
ARM64 Valgrind: 10/10 modules, 0 errors, 0 leaks
```

Local Sprint 47 validation note (2026-08-03): fresh GCC, Clang and complete
ASan+UBSan suites pass with 598 unit tests / 4,262,592 assertions and 505/505
program fixtures. The ARM64 lane passes 10 byte-exact deterministic MIR
modules / 93 coverage labels and rejects the 1 GiB bulk-memory stress case
before expansion. The assembler differential passes its fixed fragment plus
all 5,334 production-accepted logical immediates; local GNU-as absence is one
exact expected skip, while the CI toolchain job requires afs-as and GNU as
with zero skips. The dataflow-aware division-UB lint accepts all 67 real
fixtures and rejects its planted self-tests. With the glibc loader debuginfo
cache described in §3.5, Valgrind 3.25.1 reports zero errors and zero leaks
over all 10 successful ARM64 MIR modules. Independent re-review covered 61
files, found zero remaining issues at every severity, and approved the sprint.

Local Sprint 38 validation note (2026-08-01): fresh GCC and Clang full suites
pass with 480 unit tests / 94,590 assertions and 496/496 program fixtures. The
complete ASan+UBSan suite passes with leak detection disabled for the host
ptrace policy. The real GCC 8 container reports 179 exact + 18 documented / 0
unannotated warning differences; the musl lane reports 706 parsed, 655
deferred, 181 oracle-backed warnings and zero false positives. Frontend fuzzing
reproduces digest `428755e13c99b029` with zero findings. Valgrind 3.25.1 still
could not start inside the command sandbox because its private loader lacked
the mandatory `memcmp` redirection symbol. A host-scope retry during Sprint 40
proved that Valgrind itself works on this machine; this historical note records
the absence of a valid Sprint 38 run, not a host incompatibility.

The closing review also pinned nested brace-elision with a persistent
current-object cursor, rejects an unbraced scalar initializer for an aggregate,
and makes oversized initializer-image integer writes fail before any invalid
shift. The independent re-review found no remaining blockers.

Local Sprint 39 validation note (2026-08-02): fresh GCC, Clang and complete
ASan+UBSan suites pass with 483 unit tests / 95,206 assertions, 496/496 program
fixtures and 400/400 warning fixtures. The real `gcc:8` container uses a
compiler built inside that container (a host build requires GLIBC_2.33/2.34 and
cannot execute there) and reports 379 exact, 20 annotated, one explicitly
normalized Cgfried-only unbounded-scanf warning, and zero unannotated
differences. The format matrix is 64/64 rows and 128/128 generated fixtures;
musl and TinyCC both report zero format false positives. Valgrind 3.25.1 again
failed before loading the touched format path inside the command sandbox. The
later Sprint 40 host-scope retry established that the sandbox loader, not the
machine, caused that failure; no valid Valgrind result was collected for the
Sprint 39 path at the time.

Local Sprint 40 validation note (2026-08-02): fresh GCC 16.1, Clang 22.1 and
complete ASan+UBSan suites pass with 491 unit tests / 95,291 assertions,
504/504 program fixtures, 477/477 warning fixtures and 77 flow fixtures across
385 byte-stable optimization-level runs. The real `gcc:8` container reports
409 exact warning sets, 36 narrowly normalized Cgfried-only warnings, 32
documented divergences and zero unannotated mismatches. musl parses 709/1,361
sources, explicitly defers 652, observes 181 oracle-matched warnings and has
zero false positives; c-testsuite remains 215/220 with five known deferrals.
The complete sanitizer run uses `ASAN_OPTIONS=detect_leaks=0` for the host
ptrace policy; a separate 100,000-iteration sanitized frontend run has zero
findings. Seven permanent malformed-source fixtures intentionally repin the
frontend-fuzz digest to `597efab493bdc971`. Host-scope Valgrind 3.25.1
Memcheck runs over every changed Sprint 40 unit path report zero errors and
pass. A full unit-binary run is also memory-clean, though the unrelated
process-spawn unit changes behavior under Valgrind instrumentation and makes
that aggregate invocation exit nonzero.

The post-CI fuzz hardening rejects invalid parameter storage classes and void
parameters, malformed `va_list` cursors, floating/pointer comparisons and
invalid compound assignments before lowering. Pointer/integer conditional
recovery now materializes its cast, while old-style no-prototype functions
carry an explicit IR contract that the verifier accepts and inline/IPO decline
to transform. These guards convert all 17 minimized frontend-fuzz ICE seeds
into ordinary source diagnostics or valid warning-only recovery.

Local Sprint 41 validation note (2026-08-02): fresh GCC and Clang full suites
each pass 515 unit tests / 95,609 assertions; the focused alias set passes 26
tests / 157 assertions. The fourteen foundation fixtures have exact
source-qualified event chains on at least ten programs, and their double runs
are byte-identical. They cover all three budget boundaries,
verifier-after-each, a 300-branch `<2s` gate, and
an assembly comparison proving the private dump stage is emission-inert. The
complete GCC suite passes with 504/504 program fixtures, 477/477 warning
fixtures, all three fuzz smokes, differentials, bans, seams, and formatting.
The complete ASan+UBSan suite passes with `ASAN_OPTIONS=detect_leaks=0` for the
host ptrace policy. Host-scope Valgrind 3.25.1 reports zero errors and zero
leaks for the 17-test memsafe set, the 26-test shared-alias set, and an
end-to-end gated analysis run. Independent final review found no remaining
issue.

Local Sprint 42 validation note (2026-08-02): fresh GCC and Clang full suites
and the complete ASan+UBSan suite pass with 534 unit tests / 95,771
assertions, 504/504 program fixtures, 477/477 warning fixtures, and 89/89
memory-warning fixtures plus 11 exact proof traces. The pinned musl memory
sweep analyzes 732/1,361 TUs, defers 629 exact identities, emits zero `-Wmem`
diagnostics, and finishes in 13 seconds; the warning sweep now parses 711 TUs
and records 183 oracle-matched warnings with zero false positives. The
20-case GCC `-fanalyzer` comparison is complete. A host-scope ASan+UBSan
frontend fuzz run completes 100,000 fixed-seed iterations with zero findings;
the deterministic smoke digest is `d6abeca631b6cd2e`. Host-scope Valgrind 3.25.1
first found 244 conditional reads from uninitialized `MsFact.extent` state;
zero-initializing facts and canonicalizing unknown extents fixed the defect.
The final 30-test memsafe unit run and an end-to-end aggregate-provenance run
both report zero Memcheck errors and zero leaks. Independent final review
approved the implementation with no remaining findings.

Local Sprint 43 validation note (2026-08-02): fresh GCC, Clang and complete
ASan+UBSan suites pass with 551 unit tests / 96,086 assertions, 504/504
program fixtures, 477/477 warning fixtures, 89/89 intraprocedural memory
fixtures, and 50/50 interprocedural fixtures with 13 exact ordered traces. The
sanitizer run uses `ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer cannot
initialize under this host's ptrace policy; AddressSanitizer and Undefined
BehaviorSanitizer remain enabled for the complete suite. The
55-row builtin summary table and all five ownership annotations have focused
unit and corpus coverage. The pinned musl memory sweep still analyzes
732/1,361 TUs, defers 629 exact identities and emits zero `-Wmem`
diagnostics; the general warning sweep reports 183 oracle-backed warnings and
zero false positives. Summary dumps are byte-identical across repeated runs.
Valgrind 3.25.1 passes the 45-test / 692-assertion memsafe set with 1,695
allocations and frees, zero bytes live, zero leaks and zero errors; the
two-hop mixed-return compiler regression is independently clean. The
installed-layout follow-up is closed: the install manifest now copies the
complete include tree, and the ownership portability header compiles cleanly
under both host GCC and Clang.

Local Sprint 45 validation note (2026-08-03): fresh GCC, Clang and complete
ASan+UBSan suites pass with 582 unit tests / 96,373 assertions, 505/505
program fixtures, 477/477 warning fixtures, 89/89 intraprocedural memory
fixtures and 50/50 interprocedural fixtures. The dedicated autofix lane proves
the copy-only/source-byte laws, exact format records, all transform positives
and negatives, auto-init layering and annotation round-trip. The pinned musl
memory sweep remains 732/1,361 analyzed, 629 deferred and zero diagnostics;
the new permanent program fixture legitimately repins the frontend-fuzz digest
to `e39a0b1f9c71243f`. The sanitizer suite uses
`ASAN_OPTIONS=detect_leaks=0` only because LeakSanitizer cannot initialize
under this host's ptrace policy; ASan and UBSan exercise the complete suite.
Valgrind 3.25.1 is usable on this machine: the diagnostic, autofix, trivial
auto-init, summary and lifetime groups total 57 passing tests with every heap
block freed and zero errors, and an end-to-end auto-init compiler run is also
clean. Earlier Valgrind startup failures were sandbox-loader-specific evidence,
not a machine incompatibility.

Local Sprint 46 validation note (2026-08-03): fresh GCC, Clang and complete
ASan+UBSan suites pass with 583 unit tests / 96,392 assertions, 505/505
program fixtures and 56/56 safe-mode fixtures. Safe dogfood rebuilds all 91
compiler translation units, verifies every safety note with zero exemptions and
passes its smoke test. The bounded union-layout proof accepts matching large
and nested layouts, rejects crossed layouts and ranges above `INT64_MAX`, and
rejects a repeated S0-S28 type graph within 64 MiB and three seconds. A fresh
Valgrind 3.25.1 run over the nested-large regression reports 556 allocations
and frees, zero bytes live and zero errors. Independent final review approved
the implementation with no remaining correctness or performance blockers.
CI review moved musl `src/network/getaddrinfo.c` from deferred to analyzed after
the automatic-address fold repair; the exact gate is now 733 analyzed / 628
deferred with zero memory diagnostics.

---

## 1b. HOW SPRINTS 50 AND 51 GOT HERE (both CLOSED; background)

**Sprint 50 (arm64-macos) is CLOSED**: three DoD gates met as written, three
partial with the reason named and ticketed. The gate-by-gate audit is at the
end of `.docs/sprints/10-backend-arm64/s50-arm64-macos.md`.

**Sprint 51 is CLOSED, all seven deliverables.** D1 (host/target split,
`--target=`, `--sysroot=`), D2 (PIC/PIE per architecture, `-shared`), D3
(TLS), D4 (musl and FreeBSD bring-up), D5, D6 (the ABI differential) and D7
(arm64 DWARF line tables + `.eh_frame` CFI). What follows is the record of
how it got there; the LIVE work is Sprint 55 — see §1b-1.

### Sprint 50 (arm64-macos), still true and still needing its lanes run

- The Mach-O dialect, the whole seven-row Apple divergence table, SDK
  discovery, the ld64 link recipe, and afs-ld as the second link lane.
- **The Mach-O object differential: 10 objects identical to Apple's
  assembler on section bytes, relocations AND symbols, nothing pinned.**
- `codesign --verify` on both linkers' products — each ad-hoc signs.
- `tests/macos/run.sh` (7 programs x 2 linkers, signatures checked) and
  `scripts/macho_objdiff_lane.sh`, both run by a `macos-15` CI job that
  fails if either lane SKIPS.

**Run both macOS lanes after any emitter change.** They skip loudly off
arm64 Darwin, so on Linux you get nothing — and the ABI differential's
arm64-macos mode is a third lane that only exists there (§7b has the sync
recipe).

### The open tickets, and what changed

- **#100 hosted macOS compilation** needs Sprint 55. Apple's `sys/cdefs.h`
  uses `__attribute__` UNCONDITIONALLY — it does not guard on `__GNUC__` the
  way glibc does, so the trick that makes hosted Linux work does not apply —
  and `__DARWIN_ALIAS` needs `__asm` labels. Neither can be faked: dropping
  every attribute is wrong the moment one is `packed`, `aligned` or
  `noreturn`. **Sprint 51 widened this to 2 of 5 targets**: x86_64-freebsd
  is blocked the same way. The arm64-macos and FreeBSD corpora are
  freestanding-only until Sprint 55, which is a large part of why §1b-1
  takes 55 out of order.
- **#101 thread-local storage — CLOSED for local-exec on both Linux
  targets** this session (§1b). TLS-003 (Mach-O), TLS-004 (afs-as
  relocations) and TLS-005 (initial-exec/general-dynamic) remain in
  `.docs/audits/tls-debt.md`. Still a **hard prerequisite for Sprint 58**:
  the runtime uses `_Thread_local` and the bootstrap compiles the runtime
  with cgf, so TLS-004 must land before the bootstrap can use the bundled
  assembler.
- **#93 variable DIEs** (`.docs/audits/debug-info-debt.md`) and **#105 arm64
  DWARF** are the same area; #105 is the blocker (§1b-1 STEP 3).
- **#104 ABI-004** is new — Apple anonymous aggregates, diagnosed and
  ledgered, see above.

### What the ABI differential is, and why it earns its keep

`tests/tools/abigen.c` + `scripts/abi_differential_lane.sh`. A seeded
generator emits a function signature as a TEXT DESCRIPTOR, then a matched
caller/callee pair. The lane compiles one half with cgf and the other with
the reference compiler, links them, and runs — **both directions, every
time**. Three targets: x86_64 (gcc), arm64-linux (aarch64-linux-gnu-gcc under
qemu), arm64-macos (clang, on nomad-1).

Run it:

```sh
make test-abi-diff                       # both Linux targets
CGF_ABI_DIFF_TARGET=arm64-linux CGF_ABI_DIFF_COUNT=300 \
  sh scripts/abi_differential_lane.sh build/cgfried
```

The descriptor form is what makes minimization tractable: shrinking is text
surgery (drop an argument line, unwrap a composite one level) and never has
to re-derive the generator's random state. Minimized reproducers live in
`tests/abi_differential/repro/` and the lane replays **all eleven** before it
generates anything.

**THE DIAGNOSTIC WORTH INTERNALIZING.** When a signature disagrees, look at
WHICH DIRECTION fails:

- one direction  -> a placement bug on that side;
- **both directions -> a shared assumption.** Our caller and our callee agree
  with each other and neither agrees with the reference. `ours x ours` passes,
  so no same-compiler test can ever see it.

That tell has now appeared four times in three sprints (ABI-001's HFA return,
Sprint 50's row 3, ABI-002, ABI-003). Check it first.

### What it found (all closed, all in `.docs/audits/abi-debt.md`)

- **ABI-002** — an aggregate that cannot be placed ENTIRELY in registers goes
  entirely to memory; we committed eightbytes one at a time and split them.
  AAPCS64 additionally PINS the exhausted bank at 8. Placement depends on the
  type AND everything before it, so classification alone cannot decide it:
  `abi_budget_init`/`abi_arg_place` is now one shared service that both
  argument walks call, because those two walks disagreeing IS the failure
  mode.
- **ABI-003** — six varargs defects, found the moment the generator learned
  variadic tails. All six in the CALLEE. Three are one fact: **a register
  save area is laid out BY REGISTER, not by object**, so multi-eightbyte SSE
  aggregates and HFA leaves must be GATHERED out of 16-byte slots rather than
  read contiguously, and a mixed SSE+INTEGER pair needs both banks at once
  (a single "is this the FP path" boolean could not say so, and pushed every
  mixed pair to the overflow area). Two long-standing Sprint 48/49 deferrals
  fell out with them.
- **ABI-004 — OPEN.** arm64-macos is 24/304 and every minimal is a COMPOSITE
  anonymous argument. Apple holds anonymous arguments in the varargs area BY
  VALUE and contiguous; the classifier still shapes an anonymous aggregate
  like a named one. **Caller-only** — `lower_va_arg_apple` already reads
  contiguously. Marking it `ABI_ARG_STACK` at the call site covers up to 32
  bytes via the `ceil(size/8)` leaf re-plan; anything larger needs the arm64
  marshaller to honour `IROPF_ONSTACK` on a byval POINTER, which it does not
  yet. A checked-in fixture (`sysv-va-overflow-slot8`) is its standing
  witness: 10 of the 11 pass on macOS and that one does not.

### D3, thread-local storage — landed this session

`_Thread_local` had parsed and typed correctly since Sprint 16 and NEVER
LOWERED. Until Sprint 50 it became an ordinary global and every thread shared
one copy — four threads incrementing one a thousand times each printed 1000
where gcc printed 0, with no diagnostic anywhere.

- `IrGlobal.is_tls` carries it, round-trips as ` tls`. It is a property of the
  OBJECT, not of any reference, so backends ask the module (`ir_sym_is_tls`)
  rather than threading it through operands.
- x86_64: `.tdata`/`.tbss` with the T flag, `@tls_object`, STT_TLS symbols
  matching gcc. The address is BUILT, not folded into each access —
  `movq %fs:0` then `leaq sym@tpoff` (R_X86_64_TPOFF32) — so one code path
  serves loads, stores and address-of, and an addend rides the lea's
  displacement where a relocation cannot carry it. `fold_addr` refuses a
  thread-local for the same reason it refuses a GOT symbol.
- arm64: `mrs tpidr_el0` + `:tprel_hi12:` / `:tprel_lo12_nc:`.
- **AN ORDERING RULE, arm64-only.** gas rejects a TLS relocation naming a
  symbol it has not yet seen DEFINED in a TLS section ("Accessing `x` as
  thread-local object"). Functions precede data in our output, so
  thread-locals are emitted FIRST — what gcc does. A `.type ... @tls_object`
  declaration up front is NOT enough. x86 gas accepts either order, so this
  presented as a codegen bug until the emitted text was read.
- Two boundaries are clean errors, not guesses: an EXTERN thread-local emits
  no global so nothing downstream can tell it is thread-local (answering "it
  is not" is the original silent miscompile) — it needs initial-exec,
  TLS-005; and the BUNDLED assembler has no `%fs:`/`@tpoff`/TPOFF32
  (TLS-004), so the driver says so and points at `CGF_AS=0` rather than
  letting afs-as reject correct assembly and calling it "a cgf emission bug".
- **The test that matters runs four threads** and requires main to still see
  0 — `tests/programs/tls/tls_threads_are_separate.c`, `OPT_EQ: all`, run
  natively on x86_64 and under qemu on arm64. Section spellings prove nothing
  about semantics.

---

## 1b-1. Historical Sprint 55 work order

**Sprint 55 is the live one.** Skip to *WHERE THE WORK IS* below for the
resume point; the STEPS above it are the closed Sprint 51 record.

The user's standing position, stated explicitly: *"I'm generally not
comfortable leaving deferrals sitting around for long periods."* The plan
below was agreed with that in mind. Do it in this order.

### STEPS 1 and 2 — DONE. What they found is the reason to keep the habit.

Commits `9ac0686`, `6858686`, `0593cea`, `d48ead5`, `0bf917e`.

The plan called STEP 1 a renaming job. **It was audited by REACHABILITY —
compile a program that should hit each message — never by reading**, and
that is the entire lesson: eight of the nine were dead defensive text, and
the ninth was a missing C feature that then implicated four more defects.

| what | where | how it read before |
|---|---|---|
| arm64 ICEd on **every VLA** | `cg/arm64/regalloc.c` | "lands in Sprint 49", two sprints closed |
| x86 **silently miscompiled** a VLA in any function that also passed args on the stack | `cg/x86_64/regalloc.c` | no diagnostic at all |
| **2-D VLAs** wrong on both targets at every level | `lower/expr.c` | no diagnostic at all |
| **pointer to VLA** (`char (*p)[n]`) computed its stride in a non-dominating block | `lower/stmt.c` | IR verify check 1 |
| two more passes **dropped call-argument provenance** | `opt/inline.c`, `opt/loop_tree.c` | IR verify check 9 |

Details worth keeping:

- **The honest ICE was hiding the silent wrong answer.** x86's dynamic
  alloca handed back the new `rsp` as the object's base, while outgoing
  stack arguments are stored at `[rsp + k]` against that same `rsp` — so an
  argument list overwrote the first `out_args` bytes of the VLA. Both
  backends now reserve the 16-rounded outgoing area BELOW the object.
- **A VLA element's static layout size is 0**, so the dedicated subscript
  path in `lower/expr.c` scaled every row index of `int m[r][c]` by a
  literal zero and every row aliased row 0. `sizeof(m)` and `sizeof(m[0])`
  stayed correct throughout — they reach the runtime size by another route —
  so nothing that checked sizes could see it. `ptr_index` (the `p + n` path)
  had always been right; only the shortcut was not.
- **C17 6.7.6.2p4**: the size expression of a VLA type is evaluated at the
  DECLARATION, whether or not an object is created. `char (*p)[width]`
  declares a pointer, so nothing walking array chains saw a VLA.
- Two gates found things I did not: **the musl warning lane** caught the
  pointer-to-VLA regression as a one-TU drop in its parsed count, and **the
  arm64 MIR verifier** rejected `sub sp, sp, xN` outright — encoding 31 is
  XZR in the shifted-register form and the emitter has no extended form —
  instead of letting it assemble into nonsense.

**How all of it survived, and this generalizes**: the arm64 e2e corpus is
the x86 corpus re-run under qemu, and **no program in it used a VLA**. The
VLA fixtures stop at IR/MIR level, where the arm64 backend never sees them.
A corpus inherited from another target covers only what that target's
authors happened to write. Two executed programs now cover the shapes
(`tests/corpus/x86_64/int/vla_{calls,shapes}.c`); the arm64 corpus went
55 -> 57 and spill-all likewise, both ledgers still empty.

**The preventive**: `scripts/check_deferrals.sh` is in `make test`. No
message in `src/` may defer to a sprint at or below `ci/closed_sprints.txt`.
The roadmap is not in the repo, so that file is the in-tree source of truth
and raising it when a sprint closes forces the audit. Anti-vacuity checked:
set to 51 it catches the driver's live `-g on arm64` deferral.

Reproduce the audit any time with:

```sh
grep -rn 'lands\? in Sprint [0-9]*' src/      # then check ci/closed_sprints.txt
sh scripts/check_deferrals.sh
```

### STEPS 3 and 4 — DONE. **SPRINT 51 IS CLOSED**, all seven deliverables.

`246fe90`, `7977dca`, `ec32b34`, `6a28c43`, plus `359e9d9` for ABI-004.

**ci/closed_sprints.txt is now 51.** Raising it is the ratchet working:
it FORCES an audit of anything still naming 51, and the only such deferral
was the `-g` gate, which the DWARF work removed. Nothing needed renumbering.

**D7 (arm64 DWARF).** The seam was smaller than this file predicted --
`A64Inst` already carried `loc` -- so `src/cg/debug.{c,h}` takes an ordered
`CgDebugRow` sequence and both backends feed it. Proven inert on landing:
15 assembly outputs with `-g`/`-g3`/`-O2 -g` byte-identical to the previous
compiler, plus the 81-check x86 lane.

**CFI genuinely did not share, and the reason is worth keeping.** x86's FDE
is sixteen FIXED bytes because the prologue always moves the CFA by 16.
AArch64 allocates the whole frame in its first instruction, so the FDE
carries the frame size and no two functions share a program -- which is why
`frame_emit_prologue` records its own shape rather than the encoder
re-deriving which branch ran. Three CIE fields differ from x86 and EACH
would corrupt an unwind silently if copied: code alignment 4 (not 1),
return-address register x30 (not the synthetic 16), initial CFA naming SP as
register 31. `scripts/a64_debug_lane.sh` checks all three, verified by
mutation.

That lane reuses `tests/debug/dwarf_lines.c`, the x86 lane's fixture,
deliberately: a line table is a claim about SOURCE, so both targets must
resolve the same markers to the same lines. That IS D7's cross-target
agreement, and reusing the fixture made it free. Cross tools only -- nothing
runs -- so it lives in `make test` on an x86 host.

**ABI-004 closed at 315/315** with clang on nomad-1, from 24/304
disagreeing. Read the ledger entry: it was WRONG TWICE and both corrections
came from measuring clang, not from reading the note. See
`.docs/audits/abi-debt.md`.

**A cheaper future**, recorded because it changes a plan already written
down: `src/cg/debug.c` is now the ONE line-table and CU-DIE emitter for
every target, so DBG-001..004 (the variable DIEs behind the linker
diagnostic in task #93) get written ONCE rather than per backend.
`.docs/audits/debug-info-debt.md` has been corrected to say so.

### Historical state: Sprint 55 was under way

Taken out of numerical order on purpose: 28 deferrals pointed at it, it
blocks HOSTED compilation on macOS and FreeBSD, and Sprints 56/57 need it.

**THE ORGANIZING IDEA, and it decides everything else.** One question:
*what happens if we ignore this attribute?*

- cost is a diagnostic or a missed optimization -> accept, warn under
  `-Wattributes` (gcc's own flag; its default is flipped to ON to match)
- changes LAYOUT, LINKAGE or BEHAVIOUR -> hard error until implemented
- never heard of it -> accept and warn, exactly as gcc does, because a
  compiler that rejects a name it has never heard of cannot read next
  year's headers

`src/parse/gnu_attrs.def` is that table and `docs/gnu-extensions.md` is its
prose, gated by `scripts/check_gnu_tiers.sh`.

#### ELEVEN implemented, and what each one taught

`weak`, `visibility`, `packed`, `aligned`, `alias`, `used`, `__asm__("name")`
labels, `section`, `constructor`, `destructor`, `cleanup`.

- **`packed`**: drops the RECORD's alignment as well as its members'. Force
  the offsets alone and every offset a reader checks is right while `sizeof`
  keeps its tail padding. Injecting exactly that takes the layout differential
  400/400 -> 277/400, failing on `_Alignof` and never on an offset.
- **`aligned`**: record/member positions only raise, so member `aligned(1)` is
  not a spelling of `packed`; object, typedef, and declarator-type positions
  are exact and may reduce alignment without changing size or compatibility.
- **`alias`**: two bugs only a real LINK showed. IPO deleted a static function
  reachable only through its alias (a `.set` is not a relocation, so the
  callgraph never saw it), and `.weak_definition` is Mach-O's spelling that
  ELF rejects.
- **`used`**: reaches the same IPO root set an alias target does, from the
  other direction.
- **asm labels**: rename the SYMBOL; the C identifier stays for source and
  diagnostics, which is why `lower_link_name` is a separate accessor. One of
  the TWO blockers for hosted macOS -- `__DARWIN_ALIAS` uses it.
- **`section`**: a named section forces PROGBITS, so an UNINITIALIZED object
  there gets real bytes rather than a `.bss` reservation -- otherwise it lands
  outside the section the author named.
- **`constructor`/`destructor`**: 65535 is the DEFAULT, not a maximum -- gcc
  emits the same plain `.init_array` for the bare form and for an explicit
  65535. A LOWER priority runs FIRST, and the destructor order is the exact
  mirror. Adding them found that EVERY symbol attribute was dropped on a
  definition with a prior plain declaration (bug #116).
- **`cleanup`**: the only LOWERING one. Its scope walk is not the VLA walk
  even though it shares the chain -- see the header for the two bugs that
  taught it, and for why the VLA shortcut is safe only because 6.8.6.1p1
  forbids the jump that would break it.

#### SPRINT 55 SCOREBOARD (checked against the sprint file, not recalled)

Five deliverables:

| | Deliverable | State |
|---|---|---|
| **D1** | Acceptance-tier table | **Done** — 12 implemented / 7 parsed-ignored / 6 refused, gated by `check_gnu_tiers.sh` |
| **D2** | Extended asm | **Done** — basic AND operand forms on both targets, §0b |
| **D3** | Full `__attribute__` set | **11 of a targeted 16** |
| **D4** | Expression/statement extensions | **Not started** |
| **D5** | `__GNUC__` policy | **Not started** — verified undefined in BOTH `-std=gnu17` and `-std=c17` |

D4 is the largest remaining chunk and is a LIST rather than one feature:
statement expressions `({...})`, `typeof` / `__auto_type`,
`__builtin_types_compatible_p` / `choose_expr` / `constant_p`,
`,##__VA_ARGS__` comma-swallow (a Sprint 5 deferral), `__builtin_offsetof`
array designators, case ranges `1 ... 5`, `a ?: b`, `[0]` arrays, empty
structs, `__label__`, `__thread`, `__extension__`.

Of the seven DoD gates: #1, #3 and #6 are met — asm passes O0–Os on both
targets and under every spill-all lane. #4 is partial (11 of 16 attributes,
packed differential green at 2000/2000); #2, #5 and #7 are open.

#### WHERE THE WORK IS, AND WHAT TO DO NEXT

**Everything that was silently wrong is fixed. Start from a clean slate.**

Closed in the session that wrote this: `section` (the 8th attribute), the
memory-summary shape gate, `.rodata`, AS-SET-002 (afs-as PR #29, so aliases
EXECUTE on arm64), the scalar/switch controlling-expression constraints, and
the runner's per-invocation scratch. **Known-wrong-but-shipping is ZERO** --
every open item is either a named refusal or a deliberate deferral.

**NEXT, in order:**

1. **D4 -- UNDER WAY.** Statement expressions are DONE; §0c has the state of
   the rest of the list, and **task #122 is the single biggest lever left in
   the sprint**.
2. **D5 `__GNUC__` -- LAST, and read the section below before starting it.**
3. **Then Sprints 52, 53, 54** -- compile speed, codegen quality, perf gates.

---

## 0c. Historical D4 progress record

**A SURVEY BEFORE IMPLEMENTING FOUND TWO ITEMS ALREADY WORKING.** `[0]`
arrays and `__builtin_constant_p` need nothing; the sprint file lists them
because it was written before they landed incidentally. Probe the list before
planning it -- 13 one-line compiles answered it.

**DONE: statement expressions `({ ... })`.** Value is the last EXPRESSION
STATEMENT; a trailing declaration or a trailing `if` makes the whole thing
void; empty `({ })` is legal; file scope is refused as gcc refuses it. A
`cleanup` variable inside runs at the statement expression's OWN `}` -- after
the value is materialized, before the enclosing expression continues, pinned
as `CTT` in `tests/corpus/x86_64/int/stmt_expr.c`. All measured against gcc
first; the plausible alternative ordering passes every test that does not
observe ordering.

Landing it retired THREE obsolete refusal assertions, each caught by a gate
that is exactly enforced in both directions -- a `tests/programs` fixture, a
unit-test line (CONVERTED rather than deleted, since the file-scope case is
still an error), and two c-testsuite ledger rows, taking that differential
from **215 to 217 agreeing**. Note `00214.c`'s ledger reason blamed
`__builtin_expect` "lands in Sprint 28", long closed: **`check_deferrals.sh`
scans `src/`, not test ledgers**, so deferral rot hides there.

**NEXT AND LARGEST: task #122.** `asm` accepts `volatile` and `__volatile__`
but not `__volatile` -- the middle GNU spelling, no trailing underscores.
`KW_ALT_VOLATILE2` ALREADY EXISTS in `keywords.def`; it is simply missing from
the two qualifier loops in `src/parse/stmt.c`, while its exact sibling
`KW_ALT_INLINE2` is present in one of them. The enumeration hazard again.

musl's `arch/x86_64/atomic_arch.h` uses it, so it blocks every TU that
includes atomics: fixing it takes the memory sweep from **1084 to 1276 of
1361** analyzed, deferrals 277 -> 85.

**It is deliberately NOT committed**, and the reason generalizes: the
newly-parsed TUs surface NINE warning false positives across six checkers,
and the musl warn lane's ORACLE (host gcc 16) now FAILS on three files where
the gcc 8 parity baseline merely warns -- gcc 14 made an implicit function
declaration an error, and `-Dweak=` strips the declarations musl relies on.
Both patches are preserved and the task carries the evidence. Do the oracle
severity fix first, then the nine.

**D2 IS DONE** -- basic and extended asm both, on both targets. §0b is its
record, including the three bugs the operand slice turned up in code that was
not the operand slice.

**STILL OPEN, sorted by what kind of thing they are:**

*Named refusals -- LEAVE THEM.* SEC-MACHO-001 (`section` on Mach-O needs a
SEGMENT,SECTION pair), PACKED-001, and the six still-refused attributes all
fail loudly and say what is missing. That is the tier table working. Closing
them is feature work, not gap-closing, and PACKED-001 in particular should
stay open: nothing consumes an over-claimed load alignment today, and the
evidence for that is recorded rather than assumed. The former over-aligned
automatic-object refusal was retired during Sprint 53 closeout on both
backends.

*Real but not urgent.*
- **Task #93 -- `-g` emits no variable DIEs**, so a debugger cannot name a
  data symbol's source line. A genuine `-g` gap and a substantial standalone
  DWARF piece; `src/cg/debug.c` being the ONE shared emitter now makes it a
  write-once job rather than per-backend.
- **Task #112 -- an explicitly zero-initialized global goes to `.data`**
  where gcc uses `.bss`. Cosmetic (file size), both writable, values correct.
  If you take it: a CONST all-zero object must keep real `.rodata` bytes,
  never `.bss`, because `.bss` is writable.
- **Task #113 -- unit tests write helper files to a fixed scratch path.**
  The benign remnant of #110: fixed filenames, identical content, `EEXIST`
  tolerated. Worth doing only if a third instance appears or either test
  starts writing content that varies per run.
- **Task #102 -- afs-as lacks x86_64 `@GOTPCREL`/`@PLT` operand syntax**, so
  `-fPIC` on x86 needs `CGF_AS=0`. Upstream work.

*The musl campaign (Sprint 57) and the bootstrap (Sprint 58)* are the real
destination, and `_Thread_local` had to work before either -- it does now.

#### HABITS THAT PAID, REPEATEDLY

- **Measure gcc BEFORE writing code.** It overruled the sprint's own tiering
  three times; preparing `aligned` uncovered `_Alignas` on an object doing
  nothing at all; and measuring PIC levels for `.rodata` caught that macOS is
  unconditionally position-independent, where `__TEXT` is read-only AND
  code-signed.
- **Check the ARTIFACT, not the instruction.** `readelf -sW` vs the emitted
  directive; the ADDRESS at run time vs `_Alignof`, which answers from the
  TYPE and is right even when placement is wrong; a SIGSEGV on write vs a
  `.section .rodata` line; linking and RUNNING vs reading assembly.
- **NEVER grep for a word that appears in both success and failure.** I
  checked whether `switch` was already constrained with `grep -c error`, saw a
  non-zero count, and excluded it -- the count was the ICE's own
  "internal compiler error" line, and the code then carried a comment
  asserting something false.
- **Mutate every new gate before trusting it.** Several gates have been
  vacuous on first run, including `ASM_CHECK-NOT`, which walked into
  F-S22-MIRCHECK: a new directive kind must ALSO be listed in `directive.c`'s
  `add_dir` or it parses, validates, and asserts nothing.
- **THE EXEMPTION IS THE HARD HALF of any new constraint.** Both constraints
  added recently nearly rejected correct C -- `(void)f()` for the cast rule,
  `if (arr)` and `if (fn)` for the scalar rule. Write the `_ok` fixture
  first, and make it EXECUTE when the claim is a runtime one.
- **A local green suite is not the fuzz job.** `make test` runs a 2,000-
  iteration smoke; CI runs 100,000 under sanitizers. That gap has now caught
  two real ICEs (seeds 76632 and 47924). Run the full 100k locally before
  pushing anything that touches `tests/programs` -- and note that re-pinning
  the digest CHANGES THE MUTATION SEQUENCE, so a corpus edit is never inert.
- **A ledgered recipe is a hypothesis.** AS-SET-002's was wrong in its first
  step and right in its trap. Re-measure before trusting one you wrote.
- **FREEZE THE TREE while a verification run is in flight**, and never use
  `git checkout <file>` to undo a scratch mutation in a file with uncommitted
  work -- it reverts everything. Both cost real time in one session.
- **A LOOP LABEL IS NOT EVIDENCE.** Verifying the arm64 asm fix I wrote a
  `for O in -O0 -O1 -O2 -Os` loop that printed the level in its output and
  did not pass `$O` to the compile. Four identical lines, four labels, ONE
  configuration actually tested. Same family as reading a count instead of
  the output: the report was generated by the harness, not by the thing under
  test.
- **AN AGGREGATE GATE CANNOT NAME WHAT REGRESSED.** The musl zero-false-
  positive lane is the best tool in this repo for FINDING warning bugs and a
  poor one for holding them fixed -- it says "1066 parsed, zero false
  positives", never which rule. Every fix it motivates gets its own fixture,
  and an exemption gets the FIRE case too (a local `volatile` must still
  warn), or widening the exemption later passes silently.
- **A TEST THAT NEVER RUNS LOOKS EXACTLY LIKE A TEST THAT PASSES.** The
  sources declared 628 unit tests and `--list` reported 627:
  `gen_unit_registry.sh` matches `^void test_...(TestCtx *` on ONE LINE, and
  clang-format had wrapped one declaration, so it was never registered and had
  never run since the day it was written. **Second occurrence** -- Sprint 36
  hit the identical trap and its note claims the count "is asserted by the
  full suite", which was not true of any check that existed. A fix that
  renames one test stops that instance, not the next one;
  `scripts/check_unit_registry.sh` (in `make test`) compares DECLARED against
  REGISTERED, because comparing the registry to itself proves nothing.
- **`check_bans` HAS NOW BEEN WIDENED TWICE FOR THE SAME REASON.**
  `tests/corpus/` was missing until the first packed program landed there;
  `tests/warn/` and `tests/memsafe/` until the first fixture asserting a
  warning ABOUT an attribute did. A fixture directory holding compiled C
  belongs on that exemption list the day it is CREATED. Mutate after widening
  -- an exemption that switches the gate off is silent.
- **ZERO `FAIL` LINES IS NOT A PASS.** Three consecutive `make test` stops
  this session -- a `-Werror` build break, `check_bans`, `check_format` --
  produced no `FAIL` line at all, because none of them was a failing test.
  `grep -c FAIL` would have called all three clean. Read the exit code and
  the tail.
- **THE SHELL HERE IS zsh, WHICH DOES NOT WORD-SPLIT.** A loop doing
  `flags=$(sed -n '1s|// FLAGS: ||p' "$f"); cgf $flags "$f"` passes the whole
  string as ONE argument, so every fixture came back "unrecognized
  command-line option" -- and since the check counted `warning:` lines, that
  read as "0 warnings, the fixture is vacuous". It was the harness. Use
  `${=flags}` in zsh, or pass the flags explicitly. Third counting mistake of
  one session, after a loop label that did not reach the compiler and a
  `grep -c FAIL` on a build break.

#### D5 IS A PROMISE, AND ONE FACT GOVERNS EVERY FIXTURE UNTIL THEN

While `__GNUC__` is undefined, glibc's `sys/cdefs.h` does
`#define __attribute__(xyz)`. **No hosted fixture can test any attribute** --
the preprocessor deletes it and the fixture passes no matter what the compiler
does. The first packed comparison "disagreed with gcc on every row" for exactly
this. EVERY attribute fixture is freestanding.

The reverse is the D5 risk: the day `__GNUC__` is defined, every attribute in
every system header goes live at once. The implemented table is the obligation
list.


## 1b-2. Process traps from the Sprint 50/51 sessions

**READ THE CI LOG BEFORE BLAMING THE INFRASTRUCTURE.** Four consecutive
runs (`c26dc62`, `07f18cc`, `dcc27d0`, `4d55d69`) were recorded here as "the
Actions outage created no runs". Runs WERE created and WERE failing, on a
bug of ours, for days. `gh run view <id> --log` returns nothing in this
environment; use:

```sh
gh run view <id> --json jobs --jq '.jobs[] | select(.conclusion!="success") | .name'
gh api repos/tenseleyFlow/Cgfried/actions/jobs/<job-id>/logs --allow-escape-sequences
```

**THE ASSEMBLER ROUTING TRAP — it has now bitten FIVE times**, the fourth
being those four red runs: `scripts/musl_cross_lane.sh` invoked `cgf`
directly and never routed its own assembler. cgf's default
assembler is the BUNDLED afs-as. Any lane that invokes `cgf` directly must
say `CGF_AS=0` (host target) or `CGF_AS_PATH=<cross as>` (cross target) when
afs-as is not built — and a Rust-free CI job never builds one.

- Sprint 49's native arm64 lane lost three rounds to it.
- **It is what made CI red for seven consecutive runs.**
  `tests/cross/determinism.sh` compiles and LINKS in its crt-probe check, died
  at "assembler not found" before the probe ran, and then reported *"the crt
  probe ignored --sysroot"* — because it only greps for the path it wanted to
  see. The check's bug, not the driver's: **a check that greps for success
  must distinguish "looked and found the wrong thing" from "never got that
  far."** Both the routing and the message are fixed.
- The ABI lane would have lost its first CI run the same way; its routing is
  now INSIDE the lane rather than left to the caller, verified by hiding
  afs-as and running both targets.
- **The FIFTH is the interesting one, because it wore a different face.**
  The first `__thread` corpus fixture came back 89/90 under BOTH host
  compilers with **arm64 clean at 74/74** — which reads like an x86 codegen
  bug, since a feature working on the harder target and failing on the
  easier one is the wrong way round. It was routing: the bundled assembler
  has no `%fs:`/`@tpoff` on x86 (TLS-004) and the driver refuses BY NAME,
  while arm64's `:tprel_hi12:` has been in afs-as since Sprint 51. The
  fixture needs `// ENV: CGF_AS=0`, which is inert on arm64 because the lane
  sets `CGF_AS_PATH` and an explicit path beats a mode. **A per-target
  split in a result is evidence about the TOOLCHAIN at least as often as
  about the compiler** — read the stderr before reading the codegen.

**Reproduce CI's Rust-free condition instead of guessing:**

```sh
mv afs-as/target/release/afs-as /tmp/  &&  mv afs-ld/target/release/afs-ld /tmp/
make test          # this is what CI actually runs
mv /tmp/afs-as ... # put them back
```

Hide BOTH or the `debug-notools` skip ledger will not match and you will chase
a phantom.

**Two full `make test` runs concurrently on this box produce failures that do
not reproduce sequentially** ("could not run produced binary", timing gates).
Verify ONE AT A TIME. Concurrency also produced a spectacular false positive:
ppfuzz hardcoded `build/fuzz-work/case.c` regardless of `BUILD`, so two trees
overwrote each other's input between the write and the two spawns and the
differential reported **128 diffs that reproduce nowhere**. Fixed (the scratch
dir now sits beside the binary), but the lesson stands.

**The fuzz digest hashes CORPUS CONTENTS.** Adding, removing or EDITING a
fixture moves it. Re-pin `ci/fuzz_sequence_digest.txt` LAST, after the
fixtures are final — re-pinning mid-edit just means doing it twice.

**`rsync --exclude 'build*'` matches any path component**, so it silently
omitted `src/ir/build.c` and the remote link failed on three missing symbols.
Anchor the exclusions (`/build`, `/build-*`).

**`pkill -f` / `pgrep -f <pattern>` match shells you did not mean.** Three
separate incidents. `pkill -f 'BUILD=build-s51-v'` killed the session's own
command, twice, because the pattern was in that shell's command line. Then a
`while pgrep -f 'BUILD=build-s52c'; do sleep; done` wait loop hung forever:
a LEFTOVER wrapper shell from an earlier command still carried that string,
so the condition could never go false. Waiting on `pgrep -f` is waiting on
"no process anywhere mentions this text", which is not the same as "the job
finished". Use a PID, a sentinel file, or the harness's own completion
notification.

---

## 1c. Concerns and judgement calls worth inheriting

Things a reader cannot reconstruct from the diff, written down because the
next person will otherwise re-derive them or, worse, quietly undo them.

**The Sprint 49 DoD is 7 of 7 — but it took three counts to get the number
right.** Two earlier revisions of that line disagreed with their own table.
The habit that fixed it: count the `**met**` rows, do not trust the prose. The audit table at the end of
`.docs/sprints/10-backend-arm64/s49-arm64-linux.md` says which is which. Do
not let "Sprint 49 complete" in a changelog become "arm64 is done" in your
head — the corpus is 50/51 and the remaining failure is real.

**The corpus ledger is a floor, not a ceiling.** 50 of 51 e2e programs pass
on arm64. That is a good deal short of "the arm64 backend works", and the
corpus is 51 small programs written to exercise x86. Sprint 57's musl
campaign will be a far harsher test. Treat the current arm64 backend as
"boots and runs simple programs", not as production.

**Do not start Sprint 50 (arm64-macos) before the backend gaps close.**
Sprint 50 is a new object format AND a new ABI on top of the SAME isel and
regalloc. With an aggregate-return crash or an NZCV structural bug still
live, every Sprint 50 failure would be ambiguous — Mach-O, Apple ABI, or the
shared backend? The aggregate-return case was the sharpest example: Sprint 50
is precisely about ABI differences in varargs and aggregates. That is why the
gap work was sequenced first, and the reasoning still holds for whatever
remains.

**`lower/f128.c` runs after the optimizer deliberately.** `simplify.c` folds
f128 through the same softfp core (Sprint 31). Moving the pass earlier — which
looks tidier, since it is nominally "lowering" — would hide constant f128
arithmetic behind opaque calls and silently lose that folding. The header
comment says so; do not "fix" the placement.

**The in-place `rewrite_as_call()` trick has a precondition.** It works only
because a call's operands ARE its arguments and its type IS its result, so an
f128 add and a call to `__addtf3` have identical shape. The moment a rewrite
needs a different operand count or an extra instruction — which comparisons
do — that trick does not apply and real insertion is required. Do not try to
stretch it.

**I corrected two user-facing reports this session; both times the reported
symptom was real and the reported CAUSE was not.** The installed-compiler
bug was reported as by-name-vs-full-path and was actually
installed-vs-dev-tree (the full path to the INSTALLED binary fails
identically). Reproduce before believing a causal story, including your own.

**Two of my own commits fixed causes I had inferred rather than read.** See
§3.6. The crt-multiarch work was genuinely needed, so it was not wasted, but
it was not the diagnosis I claimed it was. If you catch yourself writing "the
cause is X" without having seen X in a log or a debugger, stop and go get the
evidence — it is nearly always cheaper than the round trip you are about to
spend.

**Unverifiable CI steps are worth deferring.** The native arm64 lane took
three rounds partly because I shipped job steps I could not test locally. Two
Sprint 49 gates (char-sign and the atomics hammer on native hardware) are
still open specifically because I chose not to add a fourth unverified step
at the end of a long session. That was the right call; make it again if you
are in the same position.

## 2. The ritual (not optional)

1. **Read the sprint file first, in full.** It names its own pitfalls
   deliberately. Do not skim it.
2. Implement. Check the Definition-of-Done items — they are numeric;
   check them numerically, not by vibe.
3. **Run every lane before pushing**: `make test`, `make test-san`,
   `make BUILD=build-clang CC=clang all`, and valgrind over the paths
   you touched.
4. **When the sprint file and reality diverge, fix the file** in the
   same change, and append implementation notes to it.
5. Update `AGENTS.md`, `cp AGENTS.md CLAUDE.md`.
6. **Commit in chunks** (never one monolith), terse imperative
   messages under ~250 chars unless elaboration earns its space.
   **Never co-author. Never add "Generated with" trailers.**
7. Push, then watch CI in the background.
8. **Never leave a sprint partially finished.**

The user runs a **staggered CI policy**: continue into the next sprint
while CI verdicts land; if a run fails, *step back* and fix the prior
sprint before continuing. Honor it — do not stall waiting, and do not
plough on ignoring red.

---

## 3. Environment traps — the expensive class

Every one of these cost a CI round-trip or a container session. They
share a shape: **the code was correct on this machine and wrong
somewhere else.**

### 3.1 CI's `/bin/sh` is dash (hit THREE times)

Bashisms die there and nowhere else. Sprint 15: `echo` interpreting
backslash escapes. Sprint 28: `<(process substitution)`. Gated now by
`scripts/check_posix_sh.sh` (in `make test`), which parses every
harness script under dash and greps *code* (not comments) for behavior
bashisms.

**A parse check is not enough — RUN new lanes under dash:**
`dash scripts/your_lane.sh build/cgfried`.

### 3.2 CI's `test` / `test-san` jobs have no Rust (F-S25-RUSTFREE)

The bundled assembler `afs-as` is not built there. Compiler tests must
stay Rust-free (the Sprint 2 law). `make test` therefore prefixes
assemble/link lanes with `CGF_AS=0` when afs-as is absent — loudly,
never silently (`AS_LANE` in the Makefile).

**Reproduce locally by hiding the binary:**
`mv afs-as/target/release/afs-as{,.hidden}` — this trick has caught two
distinct bugs. Put it back afterwards.

### 3.3 Arch (dev) vs Ubuntu (CI) glibc differ in ways that matter

- **Header layout**: Debian/Ubuntu put glibc's `bits/` headers under
  `/usr/include/<triple>` only. Without that dir in the search list,
  `#include <stdio.h>` cannot resolve *at all*. Arch keeps everything
  in `/usr/include`, which is exactly why it hid.
- **`libc.a` internals**: Ubuntu's `setlocale.o` takes a TLS
  initial-exec path Arch's does not; its `.eh_frame` ships a `"zRS"`
  CIE. Both broke afs-ld static links on CI only.
- **`fwrite` is annotated nonnull** on CI's glibc, so writing an empty
  buffer is UB there and silent here (F-S26-FWRITE0). This is the
  repo's recurring **zero-length-UB family** — `memcmp(NULL,0)` was an
  earlier member. Guard every zero-length libc call.
- **binutils version**: `ld` 2.44 scans index-less archives; 2.42
  refuses. `gas` 2.42 and 2.44 pad NOP fill differently
  (F-S24-NOPVERSION). Never assert a version-dependent outcome —
  assert *agreement* between the two drivers instead.

**Root-owned build artifacts**: a podman run that mounts the repo
WRITABLE leaves root-owned files under `build/`, and the next native
build dies with "Permission denied" opening a `.d` file. Clear them with
`find build -not -user "$(id -un)" -delete` — the parent dirs are
user-owned so unlink is permitted. Mount `:ro` to avoid it.

**The tool for all of this is podman**, and it is cheap:

```sh
podman run --rm -v "$PWD":/w:ro docker.io/library/ubuntu:24.04 sh -c '
  apt-get update -qq >/dev/null && apt-get install -y -qq build-essential >/dev/null
  mkdir -p /t/bin && cp -r /w/include /t/include && cp /w/build/cgfried /t/bin/
  cd /t && printf "#include <stdio.h>\nint main(void){puts(\"ok\");return 0;}\n" > h.c
  CGF_AS=0 ./bin/cgfried h.c -o h && ./h'
```

**Verify anything touching headers, static linking, or libc under
podman before pushing.** It turns a 25-minute CI round-trip into 90
seconds.

### 3.4 A hosted compile is a different test from a freestanding one

Everything through Sprint 27 exercised freestanding paths. The moment
Sprint 28 compiled a program that includes `<stdio.h>` on a foreign
distro, two latent bugs surfaced — and fixing them **unblocked 58
c-testsuite programs** that had been pinned as deferred for unrelated
reasons. When something feels stuck at a suspiciously round number,
suspect the environment, not the feature.

### 3.5 Valgrind may need loader debug symbols, not a different machine

On this Arch glibc 2.44 host, Valgrind 3.25.1 can fail before the target
program starts with a mandatory `ld-linux` `memcmp` redirection error. That is
not compiler evidence and it is not a Valgrind incompatibility. Fetch the
loader's build-id debuginfo into a writable cache, then reuse that cache:

```sh
DEBUGINFOD_CACHE_PATH=/tmp/cgfried-debuginfod \
  debuginfod-find debuginfo <loader-build-id>
DEBUGINFOD_CACHE_PATH=/tmp/cgfried-debuginfod \
  valgrind --error-exitcode=99 --leak-check=full your-command
```

For the 2026-08-03 loader the build id was
`d1e0e87f381ead4885f87135128da0c20166f55f`. Always use `set -e` in a
multi-fixture Memcheck loop; otherwise a startup failure can be followed by a
misleading final success message.

---

### 3.6 READ THE LOG — both fetch commands fail QUIETLY (Sprint 49)

This cost three CI round-trips on one job, and two of the three "fixes"
were for causes that had been INFERRED rather than read.

- `gh run view --log` and `gh run view --job <id> --log` **print nothing
  here**. No error, no exit code — just empty output, which reads exactly
  like "the log is empty".
- `gh api repos/O/R/actions/jobs/<id>/logs` refuses with *"the response
  contains terminal escape sequences"* unless you pass
  **`--allow-escape-sequences`**.

The invocation that works:

```sh
id=$(gh run list --branch trunk --limit 1 --json databaseId -q '.[0].databaseId')
jid=$(gh api "repos/O/R/actions/runs/$id/jobs" --jq '.jobs[]|select(.name=="JOB")|.id')
gh api --allow-escape-sequences "repos/O/R/actions/jobs/$jid/logs" \
  | sed 's/\x1b\[[0-9;]*m//g' | tail -30
```

The failure mode is not "I could not get the log", it is "getting the log
was harder than guessing, so I guessed." A plausible cause stated as a
diagnosis is worse than saying you do not know: it produces a commit that
looks like a fix. If you cannot read the log, say so and make the CI step
print what you need on failure.

### 3.7 A table mixing string literals with runtime-built entries (Sprint 49)

The crt probe's rows were four string literals. Making one target-derived
(`/usr/lib/<multiarch>`) meant building it into a buffer — and
`cgf_probe_crt_dir` RETURNS whichever row matched, so a caller-stack buffer
dangled. Invisible on Arch, where the match is the literal `/usr/lib64`;
broken on every Debian-layout host, which is all of CI.

The shape generalizes: **when a lookup table mixes static and constructed
entries, a lifetime bug hides behind whichever row a given host happens to
match.** The guard is a test that runs where the constructed row wins —
`test_toolchain_crt_probe` is host-sensitive on purpose, and its comment
says so. Do not "simplify" it into a pure-function test.

Related: the Debian multiarch tuple is **not** the target name. Debian
spells arm64 `aarch64-linux-gnu`; the closed target set calls it
`arm64-linux`. `cgf_target_multiarch()` exists for exactly that, with a
unit test asserting the two differ.

### 3.3b The qemu cross lane compiled against HOST headers (Sprint 49)

The driver builds its system include list for the TARGET
(`/usr/include/aarch64-linux-gnu`, `/usr/include`), but on an x86 host those
are the HOST's headers. **Arch has no multiarch directories at all**, so
`/usr/include` holds everything and a cross compile there silently succeeds
against x86 glibc headers. Ubuntu keeps glibc's `bits/` under
`/usr/include/x86_64-linux-gnu` and has no aarch64 directory, so the same
compile fails outright.

No corpus fixture noticed for a whole sprint, because **none of them includes
a SYSTEM header** — they hand-declare `int printf(const char *, ...)`, and the
only `#include` in the whole corpus is `<stdarg.h>`, which is ours.
`tests/corpus/char_sign` is the first that does, and it went red on CI while
passing locally.

`scripts/a64_corpus_lane.sh` now passes `-isystem $SYSROOT/include` in the
cross branch. Note what this means: **the lane had been wrong the entire
time** and only ever looked right because Arch's layout let host headers
stand in. If you add a fixture that includes a system header and it passes
locally, check which `stdio.h` it actually opened (`-v` prints the search
list) before believing it.

### 3.7c "Verified locally" means nothing if you did not COMMIT it

The afs-as submodule bump went red on a lane I had run and watched pass
minutes earlier. The edit that made it pass (`UNENCODABLE=""`) was in the
working tree and never staged — the commit took `afs-as` and `HANDOFF.md`
only. So the local run proved a tree that trunk did not have.

`git status` before pushing costs a second and would have caught it. The
underlying error was treating the local run as the proof and the commit as
bookkeeping; they are one step, and the artifact CI tests is the commit.

What saved it was the ratchet failing in the direction that looks harmless:
**"neon is pinned unencodable but assembled"**. A lane that only checked for
NEW failures would have let trunk carry a stale pin, silently claiming a
fixture was unsupported when it had started working. Enforce debt lists in
both directions, always.

### 3.7b A CI step that redirects to a log can THROW IT AWAY (Sprint 49)

```yaml
run: |
  make some-lane > out.log 2>&1; s=$?   # <-- WRONG
  cat out.log
  [ $s -eq 0 ]
```

GitHub runs `run:` blocks under `bash -e`. When `make` fails, `-e` fires on
that first line and the step exits **before `cat out.log`** — so the entire
diagnostic goes into a file nobody ever prints, and the log shows nothing but
`Process completed with exit code 2`. The flaw is invisible while the step
passes, which is every run until the one you need it.

Use a condition context, which `-e` does not trigger on:

```yaml
run: |
  s=0
  make some-lane > out.log 2>&1 || s=$?
  cat out.log
  [ "$s" -eq 0 ]
```

Also: **GNU make exits 2 for ANY failed target**, not with the recipe's
status. `exit code 2` from a make step tells you nothing about the cause;
do not read it as a signal.

### 3.8 `RT_TARGET` is evaluated at PARSE time (Sprint 49 follow-up)

`RT_TARGET := $(shell $(BUILD)/cgfried -dumpmachine || echo ...)` runs when
make reads the Makefile — which on a fresh checkout is **before the compiler
it probes exists**. So the fallback is not an edge case; it is what every
clean build uses. It was hardcoded `x86_64-linux-gnu`, which on the native
arm64 runner filed the arm64 runtime under `x86_64-linux-gnu/` where the
driver never looks, and every long-double program failed to link. It is now
derived from `uname`, because for a native build the host IS the target. A
cross build must still say `make RT_TARGET=arm64-linux` — the corpus lane
does.

Two smaller traps found fixing it:

- **An unescaped `)` inside `$(shell ...)` closes the function call.** A
  shell `case` with `Linux/arm64)` truncated the expansion and `RT_TARGET`
  came out EMPTY — silently, producing `build//libcgf_rt.a`. Use make
  conditionals, or check `make -p | grep '^RT_TARGET'` before believing it.
- The corpus lane's "no libcgf_rt.a beside $CGF" guard is what turned this
  from two mysterious fixture failures into one sentence naming the cause.
  Guards that name a build gap earn their keep.

## 4. Architectural laws (violating these is a silent miscompile)

These are written in the code as comments; they are repeated here
because each one was learned the hard way.

- **THE OPERAND-ORDERING LAW** (Sprint 21, violated 3× since): in isel,
  operands must *materialize before* their consumer emits. Every
  violation looked like a wrong answer far from the cause
  (F-S25-ICMPIMM64 was the third).
- **Dominance is not block-layout order** (F-S30-FWDSSA). mem2reg can expose
  a value whose dominating definition is visited later by isel around a
  backedge. Operand materialization reserves the value's vreg first; the later
  definition bridges into that stable identity. Never restore the assumption
  that a layout-order use already has a selected definition.
- **ARM64 MIR is ABI-neutral through Sprint 47.** Calls preserve exact callee,
  argument annotation, result, variadic, and noreturn metadata, but no physical
  AAPCS64 register may be assigned before Sprint 48.
- **ARM64 NZCV dependencies are explicit indices.** Any insertion, deletion,
  pairing, or relaxation before a flags consumer must repair `flags_src`; a
  nearby flags-setting instruction is not an implicit substitute.
- **Register 31 is an instruction-form property.** SP and XZR share encoding
  bits but are distinct MIR identities. Validate every position, including
  memory base/index and both legal GP sides of FP/GP `fmov`; the XZR-source
  forms `fmov dN,xzr` / `fmov sN,wzr` are the +0.0 idioms.
- **Two-edge conditional MIR occupies eight emitted bytes.** The taken edge
  uses the narrow conditional range; the explicit false edge is an ordinary B
  with an independent imm26/local-target check.
- **Optimizer scratch is phase-local.** Printer/verifier/dominance/pass
  analysis state must use short-lived arenas; verify-after-each fixpoints must
  not grow the module arena per invocation. Persistent mem2reg undef records
  are the exception, and they record consuming `Span`s plus declaration/name
  provenance for Sprint 40.
- **No host FPU in the constant engine.** All float math goes through
  `src/util/softfp.c`. `scripts/check_no_host_fpu.sh` enforces it.
  Rounding happens in exactly one place (`round_pack`) so no value is
  ever rounded twice. The current `SoftFloat` does not retain NaN payload or
  signaling state, so the optimizer must not fold an operation or conversion
  whose result is NaN: those bits are observable through IR bitcasts.
- **`undef` is per-read freedom, not a reusable unknown value.** Simplify and
  CSE must not merge or reflexively cancel expressions whose operands can be
  undef or whose operation can produce undef (div/rem/shift). SCCP treats an
  undef branch as overdefined and keeps every successor executable.
- **SCCP stays sparse and verifier-clean.** Value changes visit real use lists,
  executable edges are indexed directly, and block parameters meet executable
  incoming edges only. Rewriting a constant terminator must immediately prune
  its now-orphaned blocks; CFG cleanup scratch is exact-sized, never capped at
  a guessed function size. When `switch` becomes `br`, clear `case_val` so the
  printer/parser structural law still holds.
- **Local CSE is not load CSE.** Sprint 31's table is block-scoped and pure-op
  only. Loads remain distinct across every store until Sprint 32's shared alias
  service can prove otherwise.
- **One alias service, two clients.** `src/opt/alias.{c,h}` owns points-to,
  offset and escape facts for both optimization and Sprint 41 memory safety.
  Queries are pure and any IR mutation invalidates the context; rebuild it.
  Character/union effective types suppress only type-based NoAlias — proven
  distinct objects and disjoint byte ranges remain structurally NoAlias.
- **GVN inherits the undef law.** May-undef is transitive through operands,
  and an undef-tainted stored value cannot be forwarded to a load. Treating
  either as a reusable congruence class silently correlates independent reads.
- **Jump-thread caps include terminators.** Both the 12-instruction clone cap
  and 1.15x growth accounting charge the replacement branch. Generated clone
  names are source-independent and collision-checked so long labels cannot
  make printed IR unparsable.
- **One interprocedural callgraph, two clients.** `src/opt/ipo.c` owns direct
  and unknown callees, address-taken facts, and deterministic Tarjan SCCs for
  both IPO and inlining. Never fork the analysis. A recursive callee is never
  inlined even from outside its SCC: cloning its recursive edge creates a
  fresh eligible site and otherwise expands without bound.
- **Memory summaries are may-effects unless a checked annotation says must.**
  An inferred conditional free joins toward unknown; it never manufactures a
  proven freed state. Recursive/indirect top summaries and exhausted
  block-parameter correlation budgets degrade toward silence. Infer first,
  diagnose annotation mismatches second, then apply the annotation contract.
  Attribute metadata must be cloned and ABI-parameter indices remapped by
  every IR transform that copies a function. A top summary may retain local
  inference for mismatch diagnostics, but callers may consume its return
  ownership only when `cgf_returns_owned` explicitly supplies that contract.
- **Pinned effects have explicit pass policies.** Ordinary passes preserve
  the exact volatile/seq_cst sequence; the inliner may add only metadata-equal
  clones while leaving originals ordered; IPO may delete whole functions but
  must preserve every surviving function's pinned sequence. New mutating
  passes must choose a policy deliberately.
- **`-g` currently disables inlining.** Sprint 29 promises concrete function
  breakpoints and backtrace frames at O0 and O2, while the DWARF backend has no
  abstract-origin/inlined-subroutine records yet. `inl_debug_info` is the
  explicit boundary; removing it without richer DWARF silently destroys the
  debugger contract.
- **Loop analysis is invalid after any IR mutation.** `loop_tree_build` is
  pure scratch state. Canonicalization performs one boundary edit, renumbers,
  then rebuilds dominators and the loop tree before touching another loop;
  arena-backed block-array growth makes retained `IrBlock *` especially
  dangerous.
- **LCSSA is transitive through outside joins.** A loop-defined live-out must
  cross an exit parameter and continue through outside block parameters until
  every outside use names an outside definition. Fixing only direct exit uses
  leaves later joins pointing back into the loop.
- **LICM execution means exits and every backedge source.** Dominating every
  exit is insufficient when an inner cycle can bypass the candidate forever.
  Calls, atomics, cmpxchg, `va_start`, `stacksave`, and `stackrestore` are
  memory-state barriers for both load hoisting and store sinking.
- **Signed no-wrap is explicit IR provenance.** `IRF_NSW` comes only from
  signed source arithmetic when `-fwrapv` is off. Opcode-changing rewrites
  clear it; unsigned and `-fwrapv` arithmetic never acquire it.
- **Loop IDs are transaction-stable, not eternal.** `LoopInduction` and
  `TripInfo` keep block/value IDs only across a planned mutation. Any
  renumber/compaction invalidates them; rebuild dominance and the loop tree.
  `loop_clone_region` preallocates blocks/values, remaps block params, results,
  internal edges and LCSSA exits, and preserves flags/locations/pinned
  metadata without retaining raw block pointers across array growth.
- **Sprint 35 partial unroll is constant-trip only.** Exact trips 9–12 use a
  serial remainder peel plus a factor-four loop, preserving FP order and
  pinned-operation metadata. Runtime `TripInfo` currently recognizes syntax
  but does not prove modular termination, so runtime-bound unroll remains the
  explicit `unroll_runtime_unsupported` bail.
- **Unswitch clones static effects but preserves dynamic effects.** Exactly one
  specialized loop executes per invocation, so cloning volatile operations is
  legal when the invariant condition DAG is speculation-safe. `IRF_NSW`
  arithmetic is not hoisted without a non-overflow proof. The shipped cap is
  deliberately stronger than the roadmap: at most one unswitch per function
  and at most 2× function growth.
- **Dependence unknown always forbids restructuring.** Exact affine distances
  use the sign `iteration_b - iteration_a`; distinct pointer expressions prove
  independence only through non-unknown, disjoint points-to object sets. Byte
  offsets/types alone cannot prove different bases. Fusion additionally
  requires exact constant trips because syntactically equal runtime guards do
  not prove both loops terminate. A second-loop external operand must dominate
  the first loop's preheader, and any direct second-loop live-out forbids the
  rewrite.
- **BCE facts are edge-sensitive and proof-only.** Ranges are keyed by value
  and incoming edge, step overshoot is retained, and `-fwrapv`/subword modular
  crossings bail. `IRF_BOUNDS_CHECK` is the Sprint 44 provenance bridge: only
  marked or ordinary user comparisons proven constant may be folded.
- **Fission/interchange are deferred, not stubbed.** There is no aggressive
  loop flag or no-op CI lane. Their whole-iteration reordering needs a broader
  dependence proof and the later torture/self-host evidence base.
- **Sprint 36 vectorization is exact-constant-prefix only.** A nonmultiple trip
  executes `trip % VF` scalar iterations first, then the vector loop. Runtime
  trips, runtime alias versioning, if-conversion, SLP and min/max reductions are
  hard bails/deferrals. The first successful source-loop rewrite canonicalizes
  and rebuilds LCSSA before looking for reductions; otherwise the reduction's
  live-out is not in the form the vectorizer proves.
- **Do not splat loop-varying values.** Only invariants may become `vsplat`.
  The induction variable needs a lane sequence, and a scalar recurrence read by
  its own update is a prefix scan, not a reassociable reduction. Both mistakes
  survived ordinary positive cases and now have dedicated negative fixtures.
- **Vector IR has a deliberately closed ABI.** Exactly six 128-bit types exist;
  calls, parameters and returns reject them. Vector loads/stores reuse ordinary
  memory opcodes, supported arithmetic is an explicit verifier matrix, and
  vector spill/edge homes are 16 bytes. An odd callee-save count reserves an
  eight-byte frame gap so those homes, align-16 allocas and vararg save areas
  remain aligned.
- **Fast math is one argv-ordered bundle, not independent half-policies.**
  `-Ofast` selects O3 plus the bundle; `-ffast-math`/`-fno-fast-math` toggle it;
  a later `-O` resets to that level's default. Component spellings warn and are
  inert. `__STDC_IEC_559__` stays undefined in every mode because dynamic fenv
  semantics are not implemented.
- **The ISA ceiling is a closed allow table.** Unknown objdump mnemonics fail;
  checking only a denylist cannot prove SSE2. The 50-file corpus at six levels
  must produce exactly 300 audited objects. afs-as PR #21 added the nine packed
  instructions newly emitted here, with gas-parity immediate ranges.
- **No host `sizeof` / conditional compilation in `src/sema/`.**
  `char` is unsigned on arm64-linux; a host assumption there
  miscompiles every cross build with no diagnostic
  (`scripts/check_sema_target.sh`).
- **`src/target.c` is the SOLE target-fact site.** Headers are written
  against predefined macros so one header set serves every target.
- **One register allocator at every opt level** (Sprint 22).
  `check_bans.sh` greps `regalloc.c` for `opt_level`.
- **`link_inputs` is NEVER reordered** (Sprint 27) — archive extraction
  is position-dependent and a drop-in `cc` must reproduce gcc's
  order-sensitive failures. Gated in `check_bans.sh`.
- **The bootstrap rule**: `src/rt/` must never use 128-bit `/` or `%`,
  because Sprint 58 compiles it with cgf and those lower to calls to
  the very functions defined there.
- **Determinism defenses exist because Sprint 58 needs a byte-identical
  bootstrap**: insertion-ordered strmap, stable mergesort, numeric
  `.byte` emission, `ar rcsD`. Do not "simplify" them away.
- **`__GNUC__` stays undefined until Sprint 55** — glibc headers
  neutralize `__attribute__` themselves because of it. Load-bearing.
- **Warning checkers consume existing semantic truth.** Conversion checkers
  inspect materialized implicit casts and target widths; float proofs use
  `softfp`. Statement/switch/fallthrough checks run only after ordinary sema.
  Do not re-derive conversions or introduce checker-local option state.
- **Unused bookkeeping follows the root object, not just the leaf lvalue.**
  `s.member` and true array-decay subscripts write the declared aggregate or
  array; `p[i]` reads the pointer and writes an untracked pointed-to object.
  Preserve this distinction when adding new lvalue forms.
- **Warning oracles run in the same compile mode.** GCC 8 emits some warnings
  only during `-S`, so those fixtures say `-S`; the harness never silently
  broadens only the oracle. Strict-C89 oracle copies blank runner metadata
  line-preservingly. The musl oracle must exit successfully before any of its
  warnings can certify a CGF result.
- **Format checking runs after semantic conversions.** Compare the materialized
  promoted argument type; do not independently replay default promotions.
  Scanf is the intentional inverse: its stored-object type is unpromoted and
  pointer-qualified at every level. Conditional format expressions may carry
  multiple literal alternatives, and every distinct alternative must be
  checked.
- **Format extensions follow the target libc contract, not one “GNU” bit.**
  printf `%m` and apostrophe grouping are Linux features (gnu + musl); `I`,
  glibc redirects, syslog and asprintf are GNU-libc rows; BSD err/warn rows are
  FreeBSD-only. Scanf `m` allocation is POSIX and is accepted on every current
  hosted target. Target wchar signedness comes from `TargetSpec`.
- **Differential normalization is an assertion, not a filter.** The sole
  Sprint 39 exception is fixture-marked `-Wformat-unbounded-scanf`: the harness
  first requires exactly one extra Cgfried diagnostic with that ID, removes
  only it, then compares the remaining set. A general “ignore Cgfried extras”
  path would make the oracle vacuous.
- **Default `-Wmem` findings require proof.** Fire only on a singleton
  allocation site or a must-nonheap target; imprecise aliasing, lost path
  correlation, and unknown byte ranges degrade toward silence. Unknown calls
  escape ownership in the default tier; their possible UAF is
  `-Wmem-strict`. Realloc is pending until a null/non-null result branch
  resolves success versus failure.
- **The musl memory budget covers lowered TUs, not parse failures.** Unsupported
  Sprint 55 GNU syntax stops before analysis IR. The CI gate therefore pins
  the upstream commit, the 733 analyzed / 628 deferred split, and SHA-256
  digests of both normalized identity sets. Do not
  describe a deferred TU as warning-clean; newly parseable files must move the
  checked baseline and enter the zero-diagnostic set.
- **No silent stubs.** A placeholder that returns a plausible value is
  worse than one that aborts. Every deferral names its sprint in the
  diagnostic; `src/rt/fp128.c` aborts rather than computing wrong math.

---

## 5. Harness traps (a test that passes vacuously is worse than none)

- **F-S22-MIRCHECK**: a directive was missing from the runner's
  dispatch table, so *nine goldens passed vacuously* for a whole
  sprint. When adding a directive, add it to `add_dir` **explicitly**
  and write a meta-fixture that proves it can FAIL.
- **OPT_EQ must have an anti-vacuity fixture.** It runs every listed level
  through the normal compile/run contract and compares runtime stdout+exit;
  `tests/runner/meta/opt_eq_fail.c` deliberately differs by level and must
  report both names.
- **Never write a listing into the directory being listed** — the
  redirect races the command (green here, red on CI).
- **A harness's notion of "target" was a fact about ITSELF** (Sprint 49).
  `cgf-test` took its target from `cgf_target_host()` — its own binary's
  architecture. Correct-looking for four sprints, because runner and
  compiler-under-test always matched. The moment an x86 runner drove an
  arm64 compiler, every `ASM_CHECK(x86_64-linux-gnu):` in the shared corpus
  applied to arm64 assembly and SEVEN fixtures failed for that reason alone.
  `CGF_TEST_TARGET` overrides it; `CGF_TEST_RUN` prefixes the command that
  executes a produced binary. Ask of any harness property: *is this about
  the harness, or about the thing under test?*
- **Shared fixtures get a LEDGER, not edits.** The e2e corpus is 51
  gcc-verified expectations used by both targets. arm64 debt lives in
  `ci/expected_a64_corpus_failures.txt` — one line per (fixture, opt level)
  with its cause — enforced EXACTLY in both directions, so a new failure and
  a repaired one both fail the lane. Same shape as the `UNENCODABLE` pin in
  `scripts/a64_objdiff_lane.sh`. Never edit 51 shared files to record one
  target's gap.
- **POSIX `sh` has no locals.** `is_pinned() { for name in ...; }` clobbered
  its caller's `$name`, so every fixture after the first was tested under the
  last pinned fixture's identity. The lane reported 13 failures that were one
  bug. Give helper loops distinctive variable names.
- **Assert the artifact exists before comparing behavior.** Two
  missing binaries compare `127 == 127` and pass while proving nothing.
- **Expectations must be gcc-verified before pinning.** Four staged
  expectations from Sprints 18–23 were simply wrong (hand-computed
  sums; the ulp at 2^63 is 2048 not 1024; `va_list` is an *array* type
  so callees advance the caller's cursor). The rule was instituted in
  Sprint 25: probe gcc, then pin.
- **Derive paths from `BUILD`, never from a glob** — `build*/…` handed
  the sanitizer lane the wrong tree's artifact.
- **XPASS is a failure.** If a ledger entry starts agreeing, delete it.
- The **fuzz digest** (`ci/fuzz_sequence_digest.txt`) pins the mutation
  sequence. It changes legitimately when the corpus grows — re-pin
  then, and only then.

---

## 6. Gates (`make test` runs all of these)

| Gate | Protects |
|---|---|
| `check_bans.sh` | qsort/attributes/strtok/rand, single `getenv` site, one allocator, no `link_inputs` reorder, emit.c workarounds must cite a findings ID |
| `check_posix_sh.sh` | harness scripts parse under dash |
| `check_format.sh` | clang-format 22 (pinned; `CGF_FORMAT_REQUIRED=1` in CI) |
| `check_no_host_fpu.sh` | no float/double in the constant engine |
| `check_sema_target.sh` | no host assumptions in sema |
| `check_pp_seams.sh` | no stale `LANDS_IN_SPRINT` markers in `src/pp/` |
| `check_verify_coverage.sh` | every IR verifier check has a firing fixture |
| `check_fuzz_crashes.sh` | a crash in `tests/fuzz/crashes/` fails the build until fixed |
| `check_skips.sh <profile>` | the exact HARNESS_SKIP set per profile |
| `check_ub_division.sh` | ARM64/x86 differential fixtures cannot depend on divide-by-zero or signed-min/-1 UB; simple local propagation is tracked |
| `a64_mir_lane.sh` | ARM64 pre-RA MIR is verified, deterministic, and byte-exact against full snapshots |
| `a64_asm_diff.sh` | afs-as Mach-O and GNU-as ELF encode identical `.text`, including all 5,334 logical masks and production-packed fields |

Differential lanes (each is an *oracle*, not a golden): `header_diff`,
`rt_diff`, `driver_matrix`, `objdiff_lane`, `afsld_lane`,
`debug_info_lane`, `e2e_gcc_diff`, `ctestsuite_diff`, `layout_diff`, `fp_diff`,
`init_diff`, `inline_diff`, `lex_diff`, `parse_diff`, `spill_all_lane`,
`opt_driver`, `s33_ipo_driver`, `s34_loop_driver`.
Sprint 38 adds `warn_diff` (real GCC 8 in CI) and `musl_warn_dryrun`; both use
the same compile mode on both sides, and the latter rejects oracle failures.
Sprint 39 expands `warn_diff` to the full 400-fixture tree, adds a generated
64-row/128-fixture format matrix gate, and adds `tinycc_warn_dryrun` with the
same strict oracle-subset contract as musl.
Sprint 42 adds `test-mem-warnings` (11 exact proof-trace sequences), the
89-file semantic `tests/memsafe/wmem` corpus, optional 20-case
`test-mem-fanalyzer`, and the pinned `musl-sweep` zero-diagnostic CI lane.
Sprint 43 adds `test-mem-interproc` (50 fixtures plus 13 exact traces),
`header_portability.sh` under both host compilers, exact `__CGFRIED__`
predefine checking, and the same pinned musl zero-diagnostic budget with
summaries enabled.
Sprint 44 adds `test-mem-runtime` and `bench-safe` for deterministic runtime
failures, mixed/static linking and the overhead budget. Sprint 45 adds
`test-mem-autofix`, covering parseable/application semantics, all transform
families, auto-init and the annotation ratchet. Sprint 46 adds
`test-safe-mode`, `safe-dogfood`, the exact ELF-note mixed-link lane,
`check_safe_mode_doc.sh`, and the shrink-only symbol allowlist gate. The
normative contract is `doc/safe-mode.md`; `.docs/sprints/09-memory-safety/
s46-findings.md` records the VLA/stack correction and dogfood defects.
Sprint 47 adds `test-a64-mir`, `test-a64-asm-diff`, and
`check-ub-division`. Local runs may use Clang's integrated AArch64 assembler
with an exact skip; the CI toolchain job builds afs-as, installs GNU binutils,
and requires both oracles with zero skips.

**Design differentials so the oracle can't be faked.** The best ones in
this repo: `layout_diff` hands gcc `_Static_assert`s built from *our*
numbers (gcc accepting the file *is* the proof); `rt_diff` links one
probe against both runtimes (proving correctness and symbol
compatibility at once, since the link wouldn't resolve otherwise).

---

## 7. Submodule ritual (afs-as, afs-ld)

Both are separate repos under `FortranGoingOnForty/`. **Fix gaps
upstream; never work around them locally.** Six PRs merged so far
(afs-as #18/#19/#20/#21, afs-ld #17/#18).

Three steps, and stopping after step one is the classic mistake:

1. branch + commit *inside* the submodule, push, open PR, watch its CI
   (`gh pr checks N --watch`), merge;
2. `git checkout trunk && git pull && cargo build --release` in the
   submodule;
3. **back in Cgfried, `git add afs-ld && git commit`** to bump the pin.

Their quality gates: `cargo test`, `cargo clippy --all-targets -- -D
warnings`, `cargo fmt --check`. Rust is for **tools only** — the
compiler and its tests never require it.

Open upstream debt: `.docs/audits/afsld-elf-debt.md` (LD-ELF-001..005,
with two already fixed and recorded).

---

## 7b. Cross-target verification (arm64, Sprints 47-51)

The host constraint is GONE (2026-08-04): `aarch64-linux-gnu-gcc` and
`qemu-user-static` are installed. arm64 can be built AND executed here.

**`--target=` exists as of Sprint 51**, so an arm64 object no longer requires
cross-BUILDING the compiler — `cgf --target=arm64-linux -c` works from the
native binary, which is how the ABI differential drives it. The corpus lane
still cross-builds, because it wants an arm64 compiler running under qemu
(two levels of emulation) rather than a cross-compiler:

```sh
make CC=aarch64-linux-gnu-gcc BUILD=build-a64 build-a64/cgfried
sh scripts/a64_corpus_lane.sh      # does this for you, then runs the corpus
```

Under qemu the compiler's own `execve` of `as`/`ld` reaches the HOST, so
they must be routed by absolute path (`CGF_AS_PATH`, `CGF_LD_PATH`,
`CGF_CRT_DIR`) or you silently get x86 objects. `scripts/a64_corpus_lane.sh`
builds that wrapper; copy it rather than re-deriving it. **`CGF_AS=0` is NOT
the answer for a cross target** — it means "system as", which is the host's.

`scripts/qemu-run.sh` is the ONE place that knows how to run a foreign
binary. It is a passthrough on an arm64 host, which is what lets a single
lane serve both. It exits **125** when it cannot run at all — distinct from
any corpus exit code, so "could not run" never reads as "ran and failed".

`clang --target=aarch64-linux-gnu -O1 -S` remains the ABI oracle, and since
Sprint 51 the **ABI differential** (§1b) is the stronger one: it links our
half against gcc's and RUNS it, both directions.

**The arm64 corpus is the x86 corpus re-run.** That is efficient and it is
also a blind spot — it only covers what the x86 authors happened to write.
It is why arm64 VLAs stayed broken through two sprints and x86 VLAs were
silently wrong at the same time (§1b-1). When you fix a target-specific gap,
add a program that exercises it to the EXECUTED corpus, not just an IR-level
fixture -- that is exactly what the VLA fixtures had failed to do.

CI has three arm64 lanes: `test-arm64-native` (real `ubuntu-24.04-arm`
hardware — leaner image, so install what you need and remember the default
assembler is the BUNDLED afs-as, i.e. pass `CGF_AS=0` in a Rust-free job),
`test-arm64-qemu` (which also runs the ABI differential at 250 signatures per
arch), and the object differential inside `toolchain`.

**nomad-1 (Darwin arm64) has the full Apple toolchain AND cargo.** Remote
commands need `sh -lc` — it defaults to fish, and a non-interactive `sh -c`
does not get Homebrew's PATH. That single fact was misread as "cargo is
missing" for an entire sprint. Sync with anchored excludes:

```sh
rsync -a --delete --exclude '/build' --exclude '/build-*' --exclude '/.git' \
  --exclude '/afs-as/target' --exclude '/afs-ld/target' ./ nomad-1:/tmp/cgfried-s51/
ssh nomad-1 'sh -lc "cd /tmp/cgfried-s51 && make -j8 build/cgfried build/abigen"'
```

`make all` FAILS on macOS — `src/rt/fp128.c` needs `mode(TF)`, which Apple
clang lacks on arm64. Build the specific targets you need.

## 8. Deferrals you will trip over

Every deferral names its sprint in the diagnostic:

```sh
grep -rn 'lands in Sprint' src/ | sed 's/.*Sprint \([0-9]*\).*/\1/' | sort -n | uniq -c
```

Counts drift; re-grep rather than trusting a number written here. As of the
Sprint 51 session the shape is: **Sprint 55** (~18 — GNU `__attribute__`,
`typeof`, `__builtin_types_compatible_p`, `__builtin_choose_expr`; the
largest cluster by far and the reason §1b-1 STEP 4 takes 55 out of order),
**Sprint 23** (~13), **Sprint 19** (~9), **Sprint 57** (cross-TU sema).
`_Complex` is out of v0.1.0 entirely.

**A deferral naming a CLOSED sprint is a lie in the ledger**, and nine of
them existed at the end of Sprint 51. They were audited by REACHABILITY —
compile a program that should hit each one — and the outcome is in §1b-1.
Eight were dead defensive messages; the ninth was arm64 VLAs, a live
user-visible ICE on a shipped target, and pulling on it found four more
defects including a SILENT x86 miscompile. Audit by reachability, never by
reading: a message that looks alarming may be unreachable, and a message
that looks routine may be the only thing standing between a user and
standard C. `scripts/check_deferrals.sh` now keeps the numbers honest, but
it cannot tell reachable from dead — only you can.

`-g` on arm64-linux hard-errors naming Sprint 51 — deliberate, not an
oversight: emitting arm64 objects that silently lack a line table would break
the `-g` contract without saying so. See §1b-1 STEP 3.

---

## 9. Quick verification cheat-sheet

```sh
make                      # build (also builds libcgf_rt.a)
make test                 # everything: units, fixtures, all lanes, all gates
make test-san             # ASan+UBSan, own build tree — CATCHES WHAT test MISSES
make BUILD=build-clang CC=clang all
make tools                # cargo-build afs-as/afs-ld (Rust; tools only)

# after touching headers / static links / libc — before pushing:
#   run the podman snippet in §3.3

# single lanes while iterating:
sh scripts/header_diff.sh build/cgfried
sh scripts/rt_diff.sh build/cgfried
sh scripts/driver_matrix.sh build/cgfried
sh scripts/debug_info_lane.sh build/cgfried
sh scripts/opt_driver.sh build/cgfried
sh scripts/s35_loop_driver.sh build/cgfried build/cgf-test
sh scripts/s36_vector_driver.sh build/cgfried build/cgf-test
sh scripts/s36_isa_driver.sh build/cgfried
CGF_DIFF_GCC8=gcc-8 sh scripts/warn_diff.sh build/cgfried
sh scripts/musl_warn_dryrun.sh build/cgfried
sh scripts/tinycc_warn_dryrun.sh build/cgfried
make BUILD=build test-mem-warnings
make BUILD=build test-mem-interproc
make BUILD=build test-mem-runtime
make BUILD=build test-mem-autofix
make BUILD=build test-safe-mode
make BUILD=build safe-dogfood
make BUILD=build test-a64-mir
make BUILD=build test-a64-asm-diff   # both afs-as + GNU as in CI toolchain job
make BUILD=build test-a64-corpus     # 55 e2e fixtures under qemu
make BUILD=build test-a64-spill-all  # the same, every interval spilled
make BUILD=build test-abi-diff       # ABI differential, both Linux targets
make BUILD=build check-ub-division
make BUILD=build bench-safe
make BUILD=build test-mem-fanalyzer       # optional GCC 10+ comparator
make BUILD=build musl-sweep               # pinned 733/1361 analyzed, <90s
make BUILD=build check-format-matrix
CGF_TEST_CC=build/cgfried build/cgf-test --profile linux-x86_64 tests/programs

# the ABI differential at soak scale (a disagreement is an ABI bug):
CGF_ABI_DIFF_TARGET=arm64-linux CGF_ABI_DIFF_COUNT=300 \
  sh scripts/abi_differential_lane.sh build/cgfried
# on nomad-1, for arm64-macos (clang is the reference; gcc has no darwin/arm64):
ssh nomad-1 'sh -lc "cd /tmp/cgfried-s51 && CGF_ABI_DIFF_TARGET=arm64-macos \
  CGF_ABIGEN=build/abigen sh scripts/abi_differential_lane.sh build/cgfried"'

# reproduce CI's Rust-free condition BEFORE blaming CI (§1b-2):
mv afs-as/target/release/afs-as /tmp/ && mv afs-ld/target/release/afs-ld /tmp/
make test        # hide BOTH or the debug-notools skip ledger will not match

# CI:
gh run list --limit 3
gh run watch <id> --exit-status
gh api repos/tenseleyFlow/Cgfried/actions/jobs/<job-id>/logs \
  --allow-escape-sequences        # `gh run view --log` returns NOTHING here
gh workflow run ci                # push events raised during an Actions
                                  # outage are LOST; re-trigger by hand
curl -s https://www.githubstatus.com/api/v2/components.json | \
  python3 -c "import sys,json;print([c['status'] for c in \
  json.load(sys.stdin)['components'] if c['name']=='Actions'][0])"
```

Useful env knobs (all read in `toolchain.c`, the single `getenv` site):
`CGF_AS=0` (system gas), `CGF_LD=1` (bundled afs-ld, `-static` lane),
`CGF_CRT_DIR`, `CGF_INCLUDE_DIR`, `CGF_SPILL_ALL=1`,
`CGF_DUMP_BAD_IR=path`, `CGF_VERIFY_AFTER_EACH=1`,
`CGF_OPT_BAIL_LOG=1`. `-ftime-report` is a driver flag, not an env knob.
Sprint 35 adds independent bisection toggles:
`CGF_OPT_DISABLE_UNSWITCH=1`, `CGF_OPT_DISABLE_BCE=1`, and
`CGF_OPT_DISABLE_FUSION=1`. Sprint 36 adds
`CGF_OPT_DISABLE_VECTORIZE=1`.

---

## 10. The user's context

They are relearning C from K&R in `~/scratch/C/ch{1-5}` and want `cgf`
as their daily compiler for it. That is the acceptance test that
actually matters to them — hosted programs, real headers, readable
diagnostics, and now source-level gdb stepping/backtraces on cgf-built
binaries.

They care about: honest reporting (say what failed and show the
output), no silent shortcuts, tests and CI as first-class, and
commits that would be pleasant to bisect.
