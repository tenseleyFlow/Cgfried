# arm64-macos ABI mixed-link programs

Divergence-table evidence for Sprint 50. Each program pairs a Cgfried
translation unit with a **clang** one, because an ABI bug that both halves
share is invisible: the two must disagree for the test to mean anything.

There is no automated lane yet — deliverable D6 owns that, and it needs the
macOS CI runner. Until then these run by hand on nomad-1. The recipe, for
each pair, is:

```sh
rsync -az --exclude 'build*/' --exclude '.git/' --exclude 'target/' \
    ./ nomad-1:/tmp/cgfried-s50/
ssh nomad-1 "sh -c 'cd /tmp/cgfried-s50 && make -j18 build/cgfried'"
ssh nomad-1 "sh -c 'cd /tmp/cgfried-s50/tests/macos &&
    ../../build/cgfried -S -o t.s <ours>.c && clang -c -o t.o t.s &&
    clang -o t t.o <theirs>.c && ./t'"
```

`cgf -c` needs an assembler and `cargo` is missing on nomad-1, so the bundled
afs-as is unavailable there: go through `-S` and let clang assemble.

nomad-1's login shell is **fish**, which has no `do ... done` — wrap every
remote command in `sh -c`. Quoting through ssh + fish + sh is lossy; write
files locally and rsync them rather than heredoc-ing them across.

| pair | row | what it proves | expected |
|---|---|---|---|
| `vacall.c` + `vadefs.c` | 1 | our CALLER places anonymous arguments on the stack; also exercises system libc `printf` | `sum9=45`, `avg3=3.5000`, `mix=306`, then two `printf` lines |
| `rev_callee.c` + `rev_caller.c` | 1 | our CALLEE reads them from the stack with a plain cursor | `rsum=45`, `rmix=7.00` |
| `vaboth.c` | 1 | both halves ours, at -O0/-O1/-O2 | `tally=55`, `dsum=4.00` |
| `rows23.c` + `rows23_defs.c` | 2, 3 | our CALLER widens sub-32-bit arguments and packs the stack tail at natural size | `ext4=65280`, `pack5=4556199` |
| `rev23.c` + `rev23_main.c` | 2, 3 | our CALLEE reads that packed tail at the same offsets | `ext4x=65280`, `pack5x=4556199` |

The two constants are worth keeping honest: `65280` and `4556199` were
computed from the C semantics independently of any compiler, so a shared bug
cannot make them agree. `rev23` is the pair that caught the callee half of
row 3 — the caller half alone gave `pack5x=49989689`, because our callee was
still reading eightbyte slots where clang's caller had packed.
