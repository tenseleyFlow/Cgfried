# F02 lexer/parser — COMPLETE

- Review date: 2026-08-20
- Baseline: `1c639e060ab38bf3daf9a4e2f2a431c9ca3041cb`
- Scope: `src/lex/`, `src/parse/`, frontend fixtures
- Confirmation oracles: GCC 16.1.1, Clang 22.1.8, GCC 8.3.0 (Debian
  8.3.0-6), and Clang 7.0.1 (Debian 1:7.0.1-8+deb10u2).

## Regression and checklist evidence

The focused frontend suites and differential checks were green:

- Lexer units: 9 tests, 214 assertions.
- Parser units: 11 tests, 196 assertions.
- Expression-parser units: 11 tests, 98 assertions.
- Recovery units: 9 tests, 341 assertions.
- Lexer, parser, and diagnostic corpora: 21/21, 32/32, and 22/22 passed.
- `scripts/parse_diff.sh`: 30/30 cases agreed with GCC.
- Historical replay: `scripts/lex_diff.sh` classified 29/29 integer constants
  identically under GCC 8.3.0 and Clang 7.0.1, and `scripts/parse_diff.sh`
  agreed on 30/30 accept/reject cases with each compiler.
- All five audit probes were rejected by each historical compiler under
  `-std=c17 -pedantic-errors`, with one primary error apiece for `FE-H-01`,
  `FE-H-02`, `FE-M-03`, `FE-M-04`, and `FE-M-05`. This confirms both High
  acceptance bugs and the intended one-diagnostic recovery baseline without
  relying only on current compiler behavior.
- Frontend fuzzing: 10,000 iterations at seed 6002001 completed with digest
  `ce7a9b0088635a37` and exit status zero.

All requested attack-surface checks are complete:

- Fifteen typedef-ambiguity and prototype-scope probes were checked against
  GCC and Clang. Only `FE-H-02` diverged.
- Nineteen seeded recovery cases were recounted. `FE-M-03`, `FE-M-04`, and
  `FE-M-05` are the three confirmed excess-diagnostic cascades.
- Fifty ordinary diagnostics were spot-checked for caret placement. All 50
  pointed at the intended token, with zero column mismatches.
- Every confirmed finding has a durable `tests/audit-regressions/` fixture and
  an exact baseline-behavior probe in `scripts/check-audit-fixtures.sh`.

Checklist complete: **Yes.** F02 closes with five raw and five deduplicated
findings: zero Critical, two High, three Medium, zero Low, and zero unverified
observations. Remediation remains Sprint 61 work.

## Findings

~~ID: `FE-H-01`~~
~~Title: an unnamed variadic prototype is accepted in ISO C~~
~~Severity: High — C17 requires at least one named parameter before `...`, so~~
~~accepting `int f(...);` is a standards violation.~~
~~Reproducer: `tests/audit-regressions/fe-h-01.c`~~
~~Root cause: `src/parse/decl.c:1019-1029` accepts an ellipsis before parsing~~
~~any parameter and does not require a preceding named declaration. Cgfried~~
~~accepts under `-std=c17 -pedantic-errors`; GCC and Clang reject.~~
~~Affected sprint: 9.~~
Resolution: RESOLVED 2026-08-20 by `b1c29124`. The parameter parser now
requires a declaration before ellipsis in ISO and GNU modes while preserving
valid named and unnamed fixed parameters before `...`.

~~ID: `FE-H-02`~~
~~Title: nested K&R identifier list escapes the definition-only constraint~~
~~Severity: High — an identifier-list function declarator that is not a~~
~~definition violates C17, but Cgfried accepts it inside another declarator.~~
~~Reproducer: `tests/audit-regressions/fe-h-02.c`~~
~~Root cause: `src/parse/decl.c:2007-2012` checks `is_kr_list` only on the outer~~
~~declarator type. It does not walk through the declared object's pointer chain~~
~~to the nested function type. Cgfried accepts the block declaration; GCC and~~
~~Clang each reject it with one diagnostic.~~
~~Affected sprint: 9.~~
Resolution: RESOLVED 2026-08-20 by `b1c29124`. Declarator validation now walks
pointer, return, parameter, member, and type-name positions for nested
identifier lists while retaining the legal outer K&R definition form.

ID: `FE-M-03`
Title: one malformed parameter causes a six-error parser cascade
Severity: Medium — one syntax error produces five follow-on diagnostics,
obscuring the actionable failure and violating the recovery-quality target.
Reproducer: `tests/audit-regressions/fe-m-03.c`
Root cause: after `src/parse/decl.c:1031-1036` reports the missing parameter
declaration, `src/parse/decl.c:1106` immediately expects `)` without
synchronizing to the end of the malformed parameter list. Cgfried emits six
errors; GCC and Clang each emit one and resume at the following declaration.
Affected sprints: 9, 11.

ID: `FE-M-04`
Title: an invalid initializer item causes three cascading errors
Severity: Medium — one invalid initializer element escapes into file-scope
recovery and creates two unrelated declaration diagnostics.
Reproducer: `tests/audit-regressions/fe-m-04.c`
Root cause: `src/parse/decl.c:1600-1617` does not synchronize a poisoned
initializer item to the next comma or closing brace. Cgfried emits the primary
expression error plus errors on `2` and `}`; GCC and Clang each emit one.
Affected sprints: 10, 11.

ID: `FE-M-05`
Title: an invalid _Generic type creates a false duplicate-association error
Severity: Medium — a malformed association produces an unrelated semantic
duplicate-type diagnostic after the parser has already identified the error.
Reproducer: `tests/audit-regressions/fe-m-05.c`
Root cause: `src/parse/decl.c:1654-1659` recovers from the missing type with an
unpoisoned `int` type. `src/sema/expr.c:1332-1343` then compares that synthetic
type with the preceding valid `int` association and emits a false duplicate.
Cgfried emits two errors; GCC and Clang each emit only the syntax error.
Affected sprints: 10, 13.

## Attack-surface dispatch

- Declarator/prototype grammar: `FE-H-01` and `FE-H-02` resolved.
- Typedef-name ambiguity and prototype-scope shadowing: 15 probes complete;
  no other divergence observed.
- Recovery cascade bounds: 19 seeds complete; `FE-M-03` through `FE-M-05`
  confirmed.
- Token/AST span spot-check: 50/50 carets landed on the intended token.

## Unverified observations

None recorded.
