# DWARF depth — debt ledger (opened after Sprint 49)

Sprint 29 shipped **line-level** DWARF v4 and always-on unwind CFI, and it
met its DoD as written: `-g` gives working breakpoints, stepping, and
four-frame backtraces at `-O0` and `-O2`, verified by
`scripts/debug_info_lane.sh` (81 checks against real gdb).

What it did NOT ship — and never claimed to — is the DIE tree that describes
*data*. Measured on `int g; static int s = 3; int f(int a){ int loc = ...; }`:

| producer | `.debug_info` contents |
|---|---|
| gcc 16 | `DW_TAG_compile_unit`, `DW_TAG_subprogram` (f), `DW_TAG_formal_parameter` (a), `DW_TAG_variable` (g, s, loc), `DW_TAG_base_type` (int) |
| cgfried | `DW_TAG_compile_unit` **and nothing else** |

Our CU carries producer / language / name / comp_dir / low_pc / high_pc /
stmt_list and has no children. Everything that works today works off the
**line table** and `.eh_frame`, not off DIEs.

Rule: these are scope limits, not defects. Each keeps its stable ID until the
rung that closes it lands.

| ID | Gap | Observable effect |
|---|---|---|
| DBG-001 | No `DW_TAG_variable` for globals or statics | The LINKER cannot attribute a data symbol to its declaration. Reported by a user: a multiple-definition error reads `dcl.o:(.bss+0x0)` where gcc says `dcl.o:/path/dcl.h:9`. Same error, much worse diagnosis. |
| DBG-002 | No `DW_TAG_subprogram` | gdb has no function-level debug entity; `info functions` and function-scoped commands fall back to the symbol table. Backtraces still work — they come from `.eh_frame`. |
| DBG-003 | No `DW_TAG_formal_parameter` / local `DW_TAG_variable` | `info locals` is empty and `print a` fails inside a frame. Stepping is unaffected. |
| DBG-004 | No `DW_TAG_base_type` or any type DIE | `ptype`/`whatis` unavailable; nothing can be pretty-printed by type. Prerequisite for DBG-001..003, since every variable DIE needs a `DW_AT_type`. |
| DBG-005 | ~~arm64 emits no DWARF at all~~ | **CLOSED in Sprint 51.** arm64-linux emits DWARF v4 line tables and `.eh_frame`; `addr2line` resolves a linked executable. `-g` on arm64-**macos** still errors, now naming what it actually needs: Mach-O compact unwind and dSYM generation, which are not `.eh_frame` and are their own piece of work. |

## Why this is not in Sprint 50

Sprint 50 is arm64-macos: a new object format and a new ABI over the same
isel and regalloc. Two reasons it is the wrong home:

- ~~**arm64 has no line tables yet** (DBG-005)~~ -- closed in Sprint 51 for
  arm64-linux. Variable DIEs on a target with
  no `.debug_line` is building the second floor first.
- Sprint 50 is already large, and mixing an orthogonal debug-info workstream
  into it would make any failure ambiguous — the same reasoning that kept the
  arm64 backend gaps out of Sprint 50 in the first place.

## Where it should land

Sprint 51 closed DBG-005 and, in doing so, made the rest cheaper than this
note assumed: `src/cg/debug.c` is now the ONE line-table and CU-DIE emitter
for every target, so DBG-001..004 are written once rather than per backend.

DBG-004 then DBG-001 remain the natural order -- types first, because every
variable DIE needs `DW_AT_type`, then globals, which is the rung that fixes
the reported linker-diagnostic gap for the least work. Both now land in the
shared file; only the CFI half is still per-architecture, and it is complete
for the two ELF targets.

DBG-002/003 (function and local scope) are a larger surface — location
expressions, lexical blocks, and frame-base tracking through the register
allocator — and want their own rung rather than a corner of Sprint 51.
