# Determinism playbook

Sprint 58's bootstrap compares stage1 and stage2 bytes without normalization.
The first response to a mismatch is mechanical:

1. Run `scripts/bisect-nondet.sh STAGE1_ROOT STAGE2_ROOT`. It compares sorted
   relative object paths first, then assembly paths, `cgfried`, and
   `libcgf_rt.a`, and reports the first missing or differing artifact. Empty
   artifact groups fail instead of producing a vacuous success.
2. Map that object back to its source TU and rerun with
   `--source TU --stage1-cc STAGE1_CGF --stage2-cc STAGE2_CGF -- FLAGS...`.
3. The script first compares the public frontend views in order: `-E`,
   `--dump-ast`, and `-fdump-sema`. It then recompiles the TU once per compiler
   with `CGF_DUMP_IR=all` into two fresh `CGF_DUMP_IR_DIR` directories. The
   public probes give a quick diagnostic; the single-run tree is authoritative
   because every later boundary belongs to that same compilation.
4. Six-digit phase files sort in pipeline order: post-parse AST, post-sema,
   post-lowering IR; every pass invocation with a total sequence, fixpoint,
   iteration, and pass ordinal; post-opt f128 legalization; allocated MIR; and
   final assembly. Files are exclusive-create so accidental directory reuse
   fails rather than overwriting evidence. A matching final asm followed by a
   differing object points below `-S`, at assembler/object emission; matching
   objects followed by a runtime/final mismatch points at archive or link
   emission.

If both compiler binaries emit different but individually repeatable phase
dumps, treat that as a stage0-versus-stage1 miscompile and use the Sprint 56
optimization-divergence workflow. It is not evidence of within-compiler
nondeterminism.

## Static audit

`scripts/audit-determinism.sh` scans owned compiler C sources for unstable
sorts, address-derived output, `%p`, direct-address `fwrite`, padding-sensitive
whole-object `memcmp`, random APIs, `strcoll`, and unsafe `readdir`
consumption. A necessary exception must carry an adjacent
`determinism-audit allow CATEGORY: reason` comment.

One exact built-in exception exists: the two `readdir` loops in
`src/driver/toolchain.c:locate_libgcc_dir`. They examine all readable
candidates and select the lexicographically greatest full path with `strcmp`,
so enumeration order cannot affect the result. The allowlist matches those
two loop expressions only, not the file or function wholesale.

The whole-object comparison check is not theoretical. The first O2 fixed-point
attempt exposed a `GvnOperand` whose semantic fields matched while two padding
bytes did not. Field-wise equality plus zero-initialized key construction fixed
the miscompile; `tests/bootstrap/faults/padding_compare.c` now proves the audit
finds that class. `IrByteRange` is the sole reviewed exception: it is exactly
two initialized `u64` fields and its adjacent waiver records that invariant.

Keep `LC_ALL=C` and `SOURCE_DATE_EPOCH=0` in bootstrap stages. Do not strip
`.comment`, build IDs, padding, or any other bytes to make the comparison pass;
find and repair the source of the difference.

The playbook tests compile two variants of a miniature compiler driver for
each of three real fault mechanisms. The injected variants consume raw
`readdir` order, write a padded struct representation, or format a live stack
address with `%p`; their safe partners sort, zero, or use a stable ID. Those
executions themselves produce the differing stage artifacts, after which the
bisector must report `unsorted_readdir` at sema, `padding_write` at IR, and
`pointer_format` at assembly. No canned phase switch or hand-written object
mismatch stands in for the fault.

## Weekly independence probes

`scripts/bootstrap-repro.sh` uses the stage1 compiler to rebuild the complete
stage under `-j1` in a distinct output root, then compares assembly, objects,
the runtime archive, and the compiler against the parallel stage2 tree. This
single gate covers make-order independence and the rule that build-directory
paths cannot reach emitted bytes. The workflow runs both roots in one job;
moving the reference through an artifact into a fresh runner would silently
turn this into a comparison of two system assemblers and linkers.

The ARM64 cross-host ritual cannot compare binaries independently linked on
x86 and ARM hosts: different assemblers, CRTs, and linkers would make that a
toolchain comparison. The native ARM runner therefore archives its system
headers *before* its fixed point; native stage1/stage2 and the x86-hosted
compiler consume those exact bytes through `--sysroot`. The cross lane imports
the passed native fixed point's exact stage2 assembly, verifies every file
against its retained manifest, and compares it to the x86-hosted stream.
One native ARM runner then assembles, archives, and links both assembly trees
with one recorded toolchain before comparing the resulting bytes. Each source
stream carries its hosted run manifest, bootstrap report, and stage artifact
manifest. The final job validates both run manifests against the workflow
commit, binds their hashes and the canonical header-archive hash into a
`cgfried.bootstrap-run.v1` report, and retains all of those inputs beside the
comparison result.

## Controlled timing

`scripts/fleet-bootstrap.sh` runs the O2 fixed point with exactly eight jobs
on Kasumi and Hasu after the shorter nightly performance lanes. Immediately
before the timed stage2 build, `scripts/bootstrap-control.sh` captures the
effective power tuple and a fleet-control-v2 idle/load sample; uncontrolled
hosts fail before a timing receipt is written. `scripts/bootstrap-time-gate.sh`
requires matching target, host, power, logical-CPU, sysroot, protocol, opt
level, and job-count provenance and trips above +30% wall or user+sys time.
The first controlled receipt remains warmup until a separate review accepts
an immutable host baseline. Once present, the existing Sprint 54 report
automatically includes the `stage1.O2.*` metrics.
