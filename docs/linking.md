# Linking with cgfried

## The canonical link line

For `cgf a.o b.o -lm -o p` on x86_64-linux-gnu the driver execs, verbatim
(`-###` prints it):

```
ld -dynamic-linker /lib64/ld-linux-x86-64.so.2 -o p
   <crtdir>/crt1.o <crtdir>/crti.o
   a.o b.o -lm                    # user inputs, EXACT command-line order
   --start-group [libcgf_rt.a] -lc --end-group
                                      # runtime slot + libc
   <crtdir>/crtn.o
```

`-static` drops `-dynamic-linker` and groups the tail
(`--start-group libcgf_rt.a -lc --end-group` — glibc's `libc.a` has
internal symbol cycles; grouping is the same fix gcc applies with
`--start-group -lgcc -lgcc_eh -lc --end-group`).

crtbegin.o/crtend.o are deliberately absent: they carry GCC's constructor,
TM-clone, and legacy `.eh_frame` registration machinery, while plain C with a
linker-built `.eh_frame_hdr` needs none of it. glibc's nonshared `atexit`
shim does require the executable identity token normally found there, so
`libcgf_rt.a` supplies the single hidden `__dso_handle` object. The runtime
and libc are grouped because libc introduces that undefined reference after
the runtime's first archive scan.

## crt discovery

First `crt1.o` hit wins, probing in order: `CGF_CRT_DIR` (an explicit
override that misses FAILS — it never falls through), each `-B dir` in
flag order, then `/usr/lib/x86_64-linux-gnu` (Debian/Ubuntu),
`/usr/lib64` (Fedora/RHEL), `/usr/lib` (Arch), `/lib`. A total miss
names every probed path, one per line, and exits 2.

## Archive order is position-dependent (and cgf preserves it)

An archive satisfies only the references pending *at its position* on
the link line. The driver never reorders your inputs — a drop-in `cc`
must reproduce the failure, not "helpfully" fix it:

```
cgf main.o -lzz -o p     # ok: at libzz's position 'zzfn' is undefined
                         #     -> the member is extracted
cgf -lzz main.o -o p     # FAILS: at libzz's position nothing is
                         #     undefined; main.o then introduces 'zzfn'
                         #     -> undefined reference (gcc fails too)
```

`--start-group`/`--end-group` and friends pass through via `-Wl,`;
the dynamic libc line never needs grouping.

## Subtraction flags

| Flag | removes crt1/crti/crtn | removes libcgf_rt.a + `-lc` |
|---|---|---|
| `-nostartfiles` | yes | no |
| `-nodefaultlibs` | no | yes |
| `-nostdlib` | yes | yes |

User-supplied `-l` flags always survive subtraction.

## Static glibc caveat

`ld` surfaces `.gnu.warning`-sourced messages for NSS-class functions
("Using 'getaddrinfo' in statically linked applications requires at
runtime the shared libraries…"). cgfried streams linker stderr through
verbatim — those warnings are real and yours to heed.

## Errors

Linker exit != 0 → cgf exit 2, the tool's stderr verbatim, then one
trailer: `cgfried: error: linker command failed with exit code N (use
-v to see invocation)`. A missing linker binary → exit 3 naming the
path and the `CGF_*` variable that selected it.
