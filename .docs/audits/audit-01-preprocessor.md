# F01 preprocessor — COMPLETE

- Review date: 2026-08-20
- Baseline: `1c639e060ab38bf3daf9a4e2f2a431c9ca3041cb`
- Scope: `src/pp/`, preprocessor fixtures
- Confirmation oracles: GCC 16.1.1, Clang 22.1.8, GCC 8.3.0 (Debian
  8.3.0-6), and Clang 7.0.1 (Debian 1:7.0.1-8+deb10u2).

## Regression and checklist evidence

- The preprocessor differential completed 74 C17 and 74 GNU17 oracle
  comparisons per two-compiler pair (37 fixtures against each compiler).
  The locked GCC 8.3.0/Clang 7.0.1 replay reported 74 clean comparisons,
  zero diffs, and the two expected XFAIL fixtures in each mode, matching the
  current GCC/Clang evidence.
- The focused blue-paint, rescan, paste, and placemarker matrix was clean in
  both C17 and GNU17 modes.
- The location matrix covered `#line`, nested expansions, spelling versus
  invocation locations, raw arguments, pre-expanded arguments, and pasted
  tokens. It confirmed `PP-M-02`, `PP-L-03`, and `PP-M-04`; no additional
  divergence was observed.
- Every confirmed finding has a durable `tests/audit-regressions/` fixture and
  an exact baseline-behavior probe in `scripts/check-audit-fixtures.sh`.

Checklist complete: **Yes.** F01 closes with four raw and four deduplicated
findings: zero Critical, one High, two Medium, one Low, and zero unverified
observations. Remediation remains Sprint 61 work.

## Findings

ID: `PP-H-01`
Title: mixed-delimiter `#include_next` reuses an incompatible search index
Severity: High — valid GNU preprocessing used by system headers can fail or
select the wrong header, a conformance violation with build-blocking impact.
Reproducer: `tests/audit-regressions/pp-h-01.c`
Root cause: `src/pp/directive.c:504-519` builds quote and angle chains with
different index spaces, but `src/pp/directive.c:611-619` resumes from the
current frame's positional `found_dir` without preserving which chain shaped
that index. Quote-to-angle fatals instead of finding the second header; the
reverse direction can repeat the first. GCC and Clang emit both markers once.
Affected sprints: 4, 6.

ID: `PP-M-02`
Title: dynamic builtins lose their containing macro backtrace
Severity: Medium — the primary diagnostic remains correct, but the required
macro provenance is silently absent.
Reproducer: `tests/audit-regressions/pp-m-02.c`
Root cause: `src/pp/macro.c:420-424` destructively walks `loc` to the
outermost invocation before `src/pp/macro.c:449-467` synthesizes the builtin
token, discarding the expansion chain needed by `pp_diag_at`.
Affected sprints: 5, 7.

ID: `PP-L-03`
Title: full macro backtraces stop after 256 frames
Severity: Low — this is a diagnostics completeness defect on unusually deep
but valid macro chains, not a semantic compiler failure.
Reproducer: `tests/audit-regressions/pp-l-03.c`
Root cause: `src/pp/loc.c:208-229` says full mode disables the eight-frame
cap but collects frames in fixed 256-entry arrays and silently breaks at the
array bound. The reproducer emits exactly 256 expansion notes and omits the
outermost invocation under `CGF_DIAG_FULL_BACKTRACE=1`.
Affected sprint: 7.

ID: `PP-M-04`
Title: pre-expanded arguments lose their inner macro backtrace
Severity: Medium — the primary diagnostic is correct, but one layer of macro
provenance is omitted precisely when an argument is expanded before
substitution.
Reproducer: `tests/audit-regressions/pp-m-04.c`
Root cause: `src/pp/macro.c:724-730` wraps an already-expanded argument token
in the outer macro's expansion location. That makes the inner expansion the
new location's spelling edge while the diagnostic backtrace follows expansion
parents, so `ARG_BAD` disappears and only `PASS` remains. The fixture diagnoses
the invalid octal token but reports zero `ARG_BAD` frames and one `PASS` frame.
Affected sprints: 5, 7.

## Attack-surface dispatch

- Preprocessor oracle corpus: 74 C17 and 74 GNU17 comparisons complete for
  both the current and historical two-compiler pairs; `PP-H-01` remains the
  only confirmed semantic divergence.
- Blue-paint/rescan and paste/placemarker rules: focused matrix complete and
  clean in both modes.
- Location-chain integrity: complete across `#line`, nested, raw,
  pre-expanded, and pasted paths; `PP-M-02`, `PP-L-03`, and `PP-M-04`
  confirmed.

## Unverified observations

None recorded.
