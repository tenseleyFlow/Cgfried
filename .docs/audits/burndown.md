# Sprint 61 audit burndown

Seeded from the final Sprint 60 deduplicated ledger at baseline
`1c639e060ab38bf3daf9a4e2f2a431c9ca3041cb`. Add one row per remediation
merge; counts describe findings still open after that merge.

| Date | Commit | Remediation | Critical | High | Medium | Low | Total |
|---|---|---|---:|---:|---:|---:|---:|
| 2026-08-20 | `28361c9` | Sprint 60 closeout seed | 12 | 20 | 16 | 2 | 50 |
| 2026-08-20 | `555fc32a` | `SEMA-C-08` discovered by the `SEMA-C-02` sibling hunt | 13 | 20 | 16 | 2 | 51 |
| 2026-08-20 | `0eca9832` | `SEMA-C-01` constant signed-overflow repair | 12 | 20 | 16 | 2 | 50 |
| 2026-08-20 | `d4d674ec` | `SEMA-C-02` AAPCS64 zero-width alignment repair | 11 | 20 | 16 | 2 | 49 |
| 2026-08-20 | `892435be` | `IR-C-03` atomic pointer RMW repair | 10 | 20 | 16 | 2 | 48 |
| 2026-08-20 | `45a9d0dc` | `X64-C-02` 64-bit frame-accounting repair | 9 | 20 | 16 | 2 | 47 |
| 2026-08-20 | `7a003d68` | `MS-C-01` immutable safe-diagnostic-floor repair | 8 | 20 | 16 | 2 | 46 |
| 2026-08-20 | `bdd523c0` | `SEMA-C-08` unnamed AAPCS64 bitfield-alignment repair | 7 | 20 | 16 | 2 | 45 |
| 2026-08-20 | `c7d43927` | `IR-C-11` discovered by the `X64-C-01` sibling hunt | 8 | 20 | 16 | 2 | 46 |
| 2026-08-20 | `1b61459a` | `IR-C-04` backward-goto VLA lifetime repair | 7 | 20 | 16 | 2 | 45 |
| 2026-08-20 | `eb528221` | `IR-C-01` SysV pure-f80 aggregate-return repair | 6 | 20 | 16 | 2 | 44 |
| 2026-08-20 | `eb364980` | `IR-C-11` scalar-atomic alignment-contract repair | 5 | 20 | 16 | 2 | 43 |
| 2026-08-20 | `45f0282c` | `X64-C-01` floating-atomic lowering and link repair | 4 | 20 | 16 | 2 | 42 |
