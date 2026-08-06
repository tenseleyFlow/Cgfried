# afs-as PIC operand syntax — upstream gap (opened during Sprint 51)

x86_64 position-independent code needs two operand spellings the bundled
assembler does not parse:

| spelling | used for | afs-as today |
|---|---|---|
| `sym@GOTPCREL(%rip)` | loading a preemptible object's address | `error: bad RIP-relative displacement 'x@GOTPCREL'` |
| `call sym@PLT` | a call routed through the procedure linkage table | `error: unrecognized operand 'printf@PLT'` |

The RELOCATIONS are already there — `elf.rs` defines `R_X86_64_GOTPCREL`,
`GOTPCRELX`, `REX_GOTPCRELX` and `PLT32` — so this is a parser gap, not an
ELF-writer gap. That is the same shape as Sprint 49's arm64 work, where most
missing rows were siblings of encodings already present.

Until it lands, PIC compilation must route through the system assembler
(`CGF_AS=0`). The codegen itself is verified: `-fPIC`, `-fPIE` and the
default each match gcc's choice of GOT/PLT/direct on the same input, and a
PIE built with `CGF_AS=0` links and runs.

arm64 will need the same treatment for `:got:` / `:got_lo12:`, which Sprint
49's PR landed syntactically but which has never been exercised by an
emitter. arm64-macos needs nothing: Mach-O has been GOT-routed since Sprint
50, and afs-as already assembles that.

## Why this is not worked around here

Emitting a GOT-indirect call ourselves instead of `@PLT` would assemble
today, but it takes lazy-versus-now binding away from the linker and makes
every external call an extra load. The spelling is right; the assembler
should learn it.
