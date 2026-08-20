# Sprint 60 audit index

- Audit opened: 2026-08-15
- Audit closed: 2026-08-20
- Baseline commit: `1c639e060ab38bf3daf9a4e2f2a431c9ca3041cb`
- Baseline bootstrap: run `31865512724`, x86 O0/O2 PASS
- Baseline full CI: run `31865512754`, 20 PASS / 1 expected SKIP / 0 FAIL
- `afs-as` pin: `a6690e28fe0d1a709365aaddcfeebc22bf75e093`
- `afs-ld` pin: `e1a337b6cd363b764b4c754b3ff8b6319e68eba0`
- Oracle versions used so far: GCC 16.1.1, Clang 22.1.8, AArch64 GCC 16.1.0,
  GNU binutils 2.47, GCC 8.3.0 (`gcc-8` Debian 8.3.0-6), and Clang 7.0.1
  (`clang` Debian 1:7.0.1-8+deb10u2). The historical pair was replayed on
  2026-08-20 in an ephemeral Debian 10 buster container from image
  `debian:buster-slim` (`e1a7bb630c8b`, 73,330,096 bytes). The repository was
  mounted read-only and the 61 MiB build/evidence tree was written under
  `/mnt/space/tmp/cgfried-s60-legacy-20260820`; the container used `--rm`.
- Historical-oracle replay status: F01 C17/GNU17 preprocessor differentials,
  F02 integer-literal/parser differentials and five finding probes, and the
  F03 layout/initializer/soft-float and finding probes all completed. No new
  Cgfried divergence was found. Clang 7's two version/harness qualifications
  are recorded on F03 rather than normalized away.

| Front | File | Raw findings | Deduped | C | H | M | L | Unverified obs. |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| F01 preprocessor | `audit-01-preprocessor.md` | 4 | 4 | 0 | 1 | 2 | 1 | 0 |
| F02 lexer/parser | `audit-02-frontend.md` | 5 | 5 | 0 | 2 | 3 | 0 | 0 |
| F03 sema/layout | `audit-03-sema.md` | 7 | 7 | 2 | 5 | 0 | 0 | 2 |
| F04 IR/lowering | `audit-04-ir-lowering.md` | 10 | 10 | 5 | 4 | 0 | 1 | 0 |
| F05 optimizer | `audit-05-optimizer.md` | 4 | 4 | 0 | 4 | 0 | 0 | 3 |
| F06 x86_64 | `audit-06-x86_64.md` | 3 | 3 | 2 | 0 | 1 | 0 | 0 |
| F07 arm64 | `audit-07-arm64.md` | 4 | 4 | 0 | 3 | 1 | 0 | 0 |
| F08 driver/toolchain | `audit-08-driver.md` | 1 | 1 | 0 | 0 | 1 | 0 | 0 |
| F09 memory-safety | `audit-09-memory-safety.md` | 5 | 5 | 3 | 0 | 2 | 0 | 2 |
| F10 runtime/headers | `audit-10-runtime-headers.md` | 1 | 1 | 0 | 1 | 0 | 0 | 1 |
| F11 tests/CI integrity | `audit-11-test-integrity.md` | 3 | 3 | 0 | 0 | 3 | 0 | 1 |
| F12 determinism/reproducibility/performance | `audit-12-determinism.md` | 3 | 3 | 0 | 0 | 3 | 0 | 2 |
| **Total** | | **50** | **50** | **12** | **20** | **16** | **2** | **11** |

Verdict: Sprint 60 audit collection is complete, but we are not ready to
declare Phase 13 complete. All twelve fronts closed at the frozen baseline,
the required F04 -> F05 -> F09 dependency chain was respected, and F12 ran
last. The cross-front root-cause pass found no duplicates: all 50 raw IDs are
distinct debt, comprising 12 Critical, 20 High, 16 Medium, and 2 Low findings;
the 11 segregated observations remain excluded. The durable regression gate
contains one self-describing fixture per finding and reports 50/50 expected
failures with zero XPASS or harness failures. Sprint 61 must eliminate every
Critical and High before Medium/Low work, while Sprint 58's independent soak
continues at 2/30.

## Sprint 61 remediation discoveries

Critical-fix sibling hunts append new findings here without rewriting Sprint
60's frozen 50-finding collection totals. Each discovery enters the live
burndown and the same fixture lifecycle as the original audit debt.

| ID | Discovered by | Severity | Reproducer | Status |
|---|---|---|---|---|
| `SEMA-C-08` | `SEMA-C-02` sibling hunt | Critical | `tests/audit-regressions/sema-c-08.c` | OPEN |
