# The e2e corpus (Sprint 25) — the permanent regression floor

Every fixture compiles with `cgf` (compile -> assemble -> LINK against
system crt/libc), executes under the runner, and asserts through
`// CHECK:` (stdout, in order) and/or `// EXIT_CODE:` (exact, 0-255).
`// ASM_CHECK(<target>):` additionally pins the emitted assembly via a
side-band `cgf -S` (whitespace-normalized substring; `{{...}}` embeds a
POSIX ERE fragment).

Layout: `x86_64/int/` is the integer sublane — it must stay green even
when FP work is in flight; `x86_64/fp/` pulls FP varargs (the AL
protocol), SSE arithmetic, and x87 long double. Run either standalone
by pointing cgf-test at the directory.

EVERY expectation here was verified against gcc -O0 before pinning
(scripts/e2e_gcc_diff.sh keeps ten of them continuously honest) — a
hand-computed EXIT_CODE is how three staged fixtures rotted unseen
before Sprint 25 executed them.

Every post-milestone codegen bug lands a fixture here with a comment
naming the failure mode.
