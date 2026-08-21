# F08 driver and toolchain — CLOSED

- Review date: 2026-08-20
- Baseline: `1c639e060ab38bf3daf9a4e2f2a431c9ca3041cb`
- Validation head: `b83e07470e8f4b5234627c86f5b890632ea99171`
- Scope: `src/driver/`, the Sprint-26 flag table, dependency generation,
  link-line construction, and assembler/linker subprocess boundaries
- Confirmation tools: GCC 16.1.1 and GNU binutils 2.47

## Findings

~~ID: `DRV-M-01`~~
~~Title: assembler signal death is reported as ordinary rejection~~
~~Severity: Medium — the driver returns the phase-appropriate numeric status,~~
~~but suppresses the cause of death. User-provided assembly exits 1 with no~~
~~driver diagnostic at all; generated assembly reports an ICE saying that the~~
~~assembler rejected the text; and a translation unit containing inline asm~~
~~reports ordinary rejection. None says that the assembler died by signal.~~
~~Reproducer: `tests/audit-regressions/drv-m-01.c`, with the deterministic fake~~
~~assembler at `tests/audit-regressions/support/drv-m-01/as-signal.sh`.~~
~~Root cause: all three assembler callers inspect only success versus failure.~~
~~`src/driver/driver.c:1156-1207` turns every non-success result into the same~~
~~rejection path, and the user `.s` paths at `src/driver/driver.c:2173-2181` and~~
~~`src/driver/driver.c:2250-2258` map the status without rendering~~
~~`ToolResult.term_signal`. The linker path does distinguish `TOOL_EXITED` from~~
~~`TOOL_SIGNALED` at `src/driver/driver.c:2352-2366`.~~
~~Affected sprints: 2, 24, 26.~~

Resolution: RESOLVED 2026-08-20 by `a91a8c42`. Generated C keeps the required
ICE status but names the terminating signal; inline-assembly C and user `.s`
or `.S` inputs report the signal and retain exit 1. Ordinary assembler
rejection remains distinct, and the deterministic exit/signal matrix covers
all four input paths.

## Sprint-26 flag-surface matrix

The table currently contains 99 exact rows after later sprints extended the
original 71. The Sprint-26 surface was exercised through the pure parser units
and the existing 39-row GCC differential. Results:

| Matrix | Exact spellings exercised | Result |
| --- | --- | --- |
| Joined-or-separate | `-o`, `-I`, `-iquote`, `-isystem`, `-D`, `-U`, `-L`, `-l`, `-x`, `-MF`, `-MT`, `-MQ`, `-B` | both forms parsed; longest-prefix/exact-match assertions passed |
| Separate-only | `-include F`, `-Xlinker ARG`; joined `-includeF` and missing arguments | separate forms accepted; invalid forms rejected |
| Modes/dispatch | `-c`, `-S`, `-E`, `-o`, `-x c`, `-x none`, `.c`, `.i`, `.s`, `.S`, `.o`, `.a`, stdin | all expected input kinds and output conflicts passed |
| Preprocessor ordering | `-DX`, `-UX`, `-DX=2`; `-include pre.h` | order preserved; executable differential returned 42 and 13 respectively |
| Standards | all 18 documented C89/C99/C11/C17 and GNU aliases; invalid `-std=bogus` | 18 accepted and stored correctly; invalid value rejected by both drivers |
| Optimization last-wins | `-O2 -O -Os -O3`; bare `-O`; `-O7`; `-Ofast -O3` and reverse | final states O3, O1, O3-clamped, O3/non-fast, and Ofast/fast respectively |
| Diagnostics policy | `-Wno-bogus`, `-Wbogus`, `-fbogus`, `-bogus`, `-w`, `-Werror`, ordered `-W...` | documented silent/warn/error routing passed; GCC 16's fatal unknown-`-f` behavior remains the intentional documented divergence |
| Response/introspection | direct and nested `@file`, quoting/escaping/depth cap, `-###`, `-v`, version/dump/print probes | parser units passed; configure-shaped probe passed |
| Link pass-through | `-L`, `-l`, `-Wl,`, empty `-Wl,`, `-Xlinker`, `-static`, subtraction flags | exact ordered tagged stream passed and plans matched GCC |

Fresh evidence was `build/unit_tests --filter args`: 30 selected tests, 274
assertions, zero failures. Of those, 26 are direct `test_args_*` cases; the
filter also selects four unrelated test names containing `args`.

The executable differential was run as
`CGF_AS=0 CGF_LD=0 sh scripts/driver_matrix.sh build/cgfried` because the
bundled assembler is not built in this checkout. All 39/39 rows agreed with
GCC and the configure probe passed. The exact row set is
`tests/driver/matrix.txt`: 7 mode/output rows, 11 dependency rows, 5
accept/reject/order rows, 2 response-file rows, 7 plan/standard/dispatch rows,
and 7 archive/static link rows. Expected failures remained failures on both sides; Cgfried's
phase-specific link status is 2 where GCC's driver conventionally returns 1.

## Link-order matrix

| Exact link input stream | GCC | Cgfried | Evidence |
| --- | ---: | ---: | --- |
| `zmain.o -L. -lzz -o prog` | 0, program returns 7 | 0, program returns 7 | archive follows its reference |
| `-L. -lzz zmain.o -o prog` | 1 | 2 | both name unresolved `zzfn`; archive position preserved |
| `zmain.o -L. -lempty -lzz -o prog` | 0, returns 7 | 0, returns 7 | empty archive does not disturb order |
| cyclic `main.o -lgroupa -lgroupb` | 1 | 2 | both leave `group_a_helper` unresolved |
| cyclic `main.o --start-group -lgroupa -lgroupb --end-group` | 0, returns 0 | 0, returns 0 | group rescanning works; `-###` preserves the four tokens in order |
| `main.c dupmain.o -o prog` | 1 | 2 | duplicate `main` rejected by both |
| `zmain.o -lnoidx` | host-dependent | same outcome | agreement-only row, since GNU ld versions differ on index-less archives |

The driver's own default runtime group appears later in its canonical link
tail; the explicit user `--start-group ... --end-group` remained at the user
slot and did not merge with or move across that tail.

## Dependency-generation differential

The existing matrix covers `-M`, `-MM`, `-MMD`, `-MD`, `-MF`, `-MP`, repeated
`-MT`, `-MQ` dollar/space quoting, and source/output-derived targets. A second
focused probe used a quoted local header plus an `-isystem` header under
`-nostdinc`; these outputs were byte-identical without normalization:

| Exact mode | GCC output | Cgfried output | Result |
| --- | --- | --- | --- |
| `-M ... dep.c` | `dep.o: dep.c local.h sys/sys.h` | same | PASS; system header included |
| `-MM ... dep.c` | `dep.o: dep.c local.h` | same | PASS; system header omitted |
| `-M -MF M.mk ... dep.c` | file contains `dep.o: dep.c local.h sys/sys.h` | same | PASS; stdout redirected to file |
| `-MD -MF custom.d -c ... -o custom.o` | `custom.o: dep.c local.h sys/sys.h` | same | PASS; explicit depfile and object target |
| `-MD ... dep.c -o prog` | `prog.d` contains `prog: dep.c local.h sys/sys.h` | same | PASS; link-mode target derives from `-o` |

All ten compiler invocations in this focused matrix exited 0, and all five
dependency-file byte comparisons exited 0.

## Subprocess exit-code matrix

The fake tool emitted one line to each stream before failing, so every row
also checked that partial output survives. The assembler adapter intentionally
prefixes and routes both captured streams to compiler stderr; the linker
inherits stdout/stderr.

| Tool/input/failure | Exit | Partial output | Driver diagnostic |
| --- | ---: | --- | --- |
| assembler, generated C, exit 7 | 4 | both lines preserved as `[as] ...` | ICE: generated assembly rejected |
| assembler, generated C, `SIGTERM` | 4 | both lines preserved as `[as] ...` | ICE names signal 15 |
| assembler, user `.s`, exit 7 | 1 | both lines preserved as `[as] ...` | child text only |
| assembler, user `.s` / `.S`, `SIGTERM` | 1 | both lines preserved as `[as] ...` | driver diagnostic names signal 15 |
| assembler, nonexistent path | 3 | none expected | guidance names `CGF_AS_PATH` |
| linker, exit 7 | 2 | stdout and stderr preserved | `linker command failed with exit code 7` |
| linker, `SIGTERM` | 2 | stdout and stderr preserved | `linker command died with signal 15` |
| linker, nonexistent path | 3 | none expected | guidance names `CGF_LD_PATH` |

`build/unit_tests --filter toolchain` independently passed 8 tests / 43
assertions, covering resolution precedence, empty variables, actual child
exit/signal/spawn results, and the numeric exit mapping. The full unit suite
also passed 713 tests / 4,292,653 assertions with zero failures.

## Closure

**CLOSED for Sprint 60 finding collection.** Every F08 checklist item has an
explicit matrix above: the Sprint-26 flag surface, accept/reject/last-wins
behavior, position-sensitive archives and groups, assembler/linker exits and
partial output, and dependency generation against GCC. Sprint 61 resolved
`DRV-M-01` at `a91a8c42`; its clean detached validation reported 38 PASS / 17
XFAIL / 0 XPASS / 0 FAIL across all 55 audit checks.

The bundled `afs-as`/`afs-ld` executables were absent, so this audit used the
system GNU tools plus deterministic fake subprocesses. That is a declared
tool-availability boundary, not a silent parity claim for the bundled tools.

## Unverified observations

None recorded.
