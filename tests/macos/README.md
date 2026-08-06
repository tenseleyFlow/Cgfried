# arm64-macos ABI mixed-link programs

Divergence-table evidence for Sprint 50. Each program pairs a Cgfried
translation unit with a **clang** one, because an ABI bug that both halves
share is invisible: the two must disagree for the test to mean anything.

**Run them with `sh tests/macos/run.sh [path/to/cgfried]`, on the Mac.** It
skips loudly anywhere else. There is no CI runner yet — deliverable D6 owns
that — but the script exists because verifying these by hand once each is
exactly how divergence-table row 3 silently regressed row 1: packing the
stack tail at natural size also packed the VARARGS area, putting every
anonymous argument after the first where the callee never looks. Nothing
caught it for two commits.

To sync and build first:

```sh
rsync -az --exclude 'build*/' --exclude '.git/' --exclude 'target/' \
    ./ nomad-1:/tmp/cgfried-s50/
ssh nomad-1 "sh -c 'cd /tmp/cgfried-s50 && make -j18 build/cgfried &&
    CGF_AS=0 sh tests/macos/run.sh build/cgfried'"
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
| `gotlink.c` + `gotlink_defs.c` | — | every UNDEFINED symbol goes through the GOT, a function's address included | `ext_data=41 via_ptr=41`, `arr3=30 s.c=33`, `fn=99 same=1`, `def=1000 stat=2000` |
| `rows47.c` (alone) | 4, 5, 7 | signed `char`, `long double` == `double` incl. libc `%Lf`, `wchar_t` is `int` | `row4 char=-1`, `row5 sizeof=8 ...`, `row7 wchar=4`, `row5 pct_Lf=3.1415926536 -0.5000000000` |

The two constants are worth keeping honest: `65280` and `4556199` were
computed from the C semantics independently of any compiler, so a shared bug
cannot make them agree. `rev23` is the pair that caught the callee half of
row 3 — the caller half alone gave `pack5x=49989689`, because our callee was
still reading eightbyte slots where clang's caller had packed.

`rows47.c` needs no partner: every claim is a `_Static_assert`, so **clang
accepting the same file** is the cross-check — no dump format to parse and
nothing version-specific. It is anti-vacuous by construction:
`clang --target=aarch64-linux-gnu` rejects it at row 4, because plain `char`
is unsigned under AAPCS64 and signed under Apple's ABI.

Row 6 (x18 is the reserved platform register) is not here. It is a
grep-the-assembly gate and lives in `scripts/a64_exec_lane.sh`, which already
runs in CI: arm64-linux reserves x18 as well, so the check does not need a
macOS runner. macOS clobbers x18 *asynchronously*, so allocating it corrupts
a function that never called anything — a failure that needs a context switch
at the wrong instant and will not show up in a test.

These programs use hand-written prototypes rather than `<stdio.h>`, because
SDK header discovery is deliverable D3 and macOS keeps no `/usr/include`.
Once that lands, `tests/corpus/char_sign/` can also run here — its fixtures
now carry `// CHECK(arm64-macos):` rows.
