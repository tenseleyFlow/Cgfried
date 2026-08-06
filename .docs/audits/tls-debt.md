# Thread-local storage — debt ledger (opened during Sprint 50)

`_Thread_local` parses, types, and records correctly. It has never been
**lowered**. Until Sprint 50 it reached the backend as an ordinary global and
every thread shared one copy — a silent miscompile on every target, with no
diagnostic anywhere.

Measured on x86_64-linux, four threads each incrementing a `_Thread_local int`
one thousand times:

| producer | `main counter=` |
|---|---|
| gcc 16 | `0` |
| cgfried (before) | `1000` |

The generated code is the giveaway: we emitted `tls_v` into `.data` and read
it with `movl tls_v(%rip), %ecx`, where gcc emits `.tdata` and
`movl %fs:tls_v@tpoff, %eax`.

**As of Sprint 50 it is a hard error at lowering**, naming Sprint 50, the same
rule Sprint 28 applied to the runtime's unimplemented entry points: a stub
that returns a plausible value is the one failure mode no test distinguishes
from success. `-fsyntax-only` and `-fdump-sema` still accept it, because the
analysis module is not object emission and the sema-level rules (6.7.1
placement constraints, the linkage matrix) are already tested and correct.

## Why this is its own deliverable

Nothing about the gap is target-specific — no target ever had it. Sprint 50
owns it only because its deliverable 5 is the first to name `_Thread_local` at
all. The work is three separate mechanisms plus an upstream assembler change:

| ID | Piece | Notes |
|---|---|---|
| TLS-001 | ELF `.tdata`/`.tbss` sections and the local-exec model on x86_64 | `movl %fs:sym@tpoff` — `R_X86_64_TPOFF32` |
| TLS-002 | Local-exec on arm64-linux | `mrs xN, tpidr_el0` plus `:tprel_hi12:` / `:tprel_lo12_nc:` |
| TLS-003 | Mach-O thread-local variables | `__DATA,__thread_vars` descriptors and a call through the descriptor — a completely different mechanism from ELF TLS, not a spelling change |
| TLS-004 | afs-as support for the TLS relocation families on both architectures | The bundled assembler has none today |
| TLS-005 | Initial-exec / general-dynamic models | Only needed once we emit PIC or shared objects, which is Sprint 51. Local-exec is sufficient for the non-PIE executables v0.1.0 produces. |

TLS-005 is deliberately last: local-exec covers everything the current output
model can produce, and implementing the dynamic models before there is a
shared object to test them against would be unverifiable.

## Blast radius of the hard error, measured

Nothing regressed when it landed:

- The five `tests/programs/sema16/tls_*.c` fixtures use `-fsyntax-only` or
  `-fdump-sema` and still pass.
- `__thread` is not a keyword for us, so musl's thread-local translation units
  were already in the pinned deferral set and their counts are unchanged.
- `src/rt/cgf_safe_alloc.c` uses `_Thread_local` for the Sprint 44 range
  cache, but the runtime is built by `RT_CC` (the host compiler), not by cgf.
  **Sprint 58 flips that**, so TLS-001 is a hard prerequisite for the
  self-hosting bootstrap — it cannot slip past Phase 12.
