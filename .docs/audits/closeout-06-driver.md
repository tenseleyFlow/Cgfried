# Closeout: Phase 06 — Driver and Toolchain

Date:            2026-08-20
Baseline commit: `89b68ead`
Reviewer:        Independent Sprint 61 closeout review

## DoD items (from the phase's sprint files, every numbered item)

- [x] S26.1 Every flag in Deliverables 2–7 parses in all documented forms; the flag table has ≥ 45 entries and zero `strcmp` chains outside it. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S26.2 Differential matrix: 30/30 rows agree with gcc on subcommand structure and produced files; depfiles byte-match after path normalization. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S26.3 `cgf -c a.c b.c && cgf a.o b.o -o p && ./p` works (multi-TU milestone). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S26.4 `-dumpversion`/`-dumpmachine`/`--version`/`-print-*` outputs match this file exactly; a saved autoconf `configure` fragment probing them succeeds. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S26.5 `-M`/`-MM`/`-MD`/`-MMD`/`-MF`/`-MT`/`-MQ`/`-MP` all covered by golden tests, including the `$`/space/`#` quoting rows. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S26.6 Unknown `-fbogus` warns and compiles; unknown `-bogus` errors with a suggestion when close; `-Wno-bogus` is silent. Exit codes per contract. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S26.7 `-lm`-before-object reproduces gcc's failure; link_inputs order verified verbatim in `-###` output. --- — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S27.1 Golden link argv matches this file's canonical sequence on all three distro layouts (chroot fixtures or path-injected probe table in tests). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S27.2 `cannot find crt1.o` diagnostic names every probed path; exit 2. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S27.3 Archive-order worked example behaves identically under cgf and gcc (both orders, both drivers — 4/4 outcomes match). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S27.4 `-static` hello world runs; the NSS-class ld warning appears verbatim when the fixture calls `getaddrinfo`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S27.5 Subtraction table verified by `-###` goldens: 3 flags × presence checks. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S27.6 `CGF_LD=1 -static` corpus lane green: ≥ 10 fixtures link via afs-ld, run with identical stdout/exit vs system-ld products; readelf checks pass. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S27.7 The five "extend afs-ld upstream" items are filed in the debt ledger with stable IDs; the COPY-reloc hint diagnostic has a test. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S27.8 `grep` shows no code path that reorders `link_inputs`. --- — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S28.1 All 9 headers ship; the 54-cell mode grid compiles warning-free. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S28.2 Macro round-trip differential vs gcc: 100% byte-equal on x86_64-linux-gnu; per-target tables spot-checked via cross `-E`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S28.3 Every builtin in the D8 table has sema + lowering + ≥ 1 fixture; deferred builtins hard-error naming Sprint 55. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S28.4 rt boundary corpus green vs libgcc oracle (≥ 200 cases); both mixing-law links run. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S28.5 fp128 stubs hard-error naming Sprint 49; grep confirms no silent body. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S28.6 Header dir contains exactly 9 files; a hosted fixture including `<stdio.h>` still gets glibc's (no shadowing). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S28.7 `build/<target>/libcgf_rt.a` reproducible: two clean builds byte-equal. --- — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S29.1 `readelf --debug-dump` parses `.debug_line/.debug_info/.debug_abbrev/` `.eh_frame` from every corpus object with zero complaints. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S29.2 gdb batch suite: 100% of `// GDB_LINE:` assertions pass at `-O0`; `-O2` suite has zero wrong-line failures (line 0 permitted). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S29.3 `bt` through 3 nested calls names all frames correctly, `-O0` and `-O2`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S29.4 addr2line spot checks green on 5+ addresses per sample. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S29.5 afs-ld links `-g` objects (CIE encoding accepted); products run. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S29.6 Objects without `-g` still carry `.eh_frame`; `-g0` strips debug sections only. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S29.7 The afs-as named-sections upstream item is landed (or the sprint is blocked — no fallback hack); `.loc` upstream item filed in the debt ledger with a stable ID. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S29.8 Two identical `-g` builds are byte-identical (no mtime/path leakage — invariant 3 holds in debug info). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).

## Open audit findings against this phase

None. At remediation baseline `89b68ead`, [the Sprint 61 burndown](burndown.md) records 0 Critical / 0 High / 0 Medium / 0 Low findings, and the audit lifecycle reports 55 PASS / 0 XFAIL / 0 XPASS / 0 FAIL.

## Verdict

Every numbered phase DoD item is evidenced and no audit findings remain.

READY
