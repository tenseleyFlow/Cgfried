# XFAIL and implementation debt ledger

Every `// XFAIL(<selector>):` directive in a test source must cite an
`XF-NNNN` id from the table below; the test runner treats an unknown or
missing id as a configuration error.

Historical `XD-*` rows record implementation seams that were never runner
fixture IDs. They remain in place after retirement so the provenance is not
rewritten, but they cannot be cited by `XFAIL(...)` directives.

Rules:

- IDs are never reused.
- Closing an XFAIL flips its status to `closed (YYYY-MM-DD)` — rows are never
  deleted. The ledger is history, not just state.
- An open entry names the sprint that owns fixing it. "Nobody's sprint" is
  not a status.
- XPASS (an XFAIL that unexpectedly passes) is a hard test failure: fixing
  the bug forces the annotation off and the row closed, loudly.

| ID | Owning sprint | Description | Status |
|---|---|---|---|
| XD-S08-FPHOST | Sprint 15 | `lex_fp_interim` in `src/lex/numlit.c` wraps host `strtod`: its rounding is the HOST's, which is wrong for cross-target constant folding (determinism invariant #3) and for `long double`. Tokens deliberately carry the exact spelling so Sprint 15's correctly-rounded softfloat engine can replace exactly this one function. `scripts/check_bans.sh` enforces that no other conversion site exists. TI-M-02 records the delayed ledger reconciliation. | fixed-by-remediation; closed 2026-08-20 at `141ffcad` |

## Sprint 61 release triage

The primary ledger has zero open `XF-NNNN` rows and therefore zero
Critical-tagged XFAILs. The release bar recorded by Phase 12 closeout is
**N = 0**. The isolated meta-suite `XF-0001`, audit-lifecycle fixture comments,
and torture-harness controls are test-harness self-tests rather than production
XFAIL debt.
