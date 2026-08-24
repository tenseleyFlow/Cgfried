# Sprint 56 Torture Triage

Generated deterministically from `# cgf-torture-results-v2` streams.

## Provenance

- source-revision: `19f0117830da8806c4a12a3942a28026f7f678d0`
- compiler-source-sha256: `fc966092a5dd81c3cf92f17ef2108609598aaa69676b40f60bf4630fda20e438`
- harness-sha256: `b6e50c45f810d83e0b9e5b5adcc722f8ec2a5e2afdc98a0611386507b01a07b5`
- torture-manifest-sha256: `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`
- ctestsuite-manifest-sha256: `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`

| Target | Compiler binary SHA-256 | Compiler driver SHA-256 |
|---|---|---|
| arm64-linux | `1e5ef6d287a950b1d69868c721a8ed039d4928c2d0d660364226f95280abf453` | `331064306d4d23b20fbe4fb0ac51b4f5a56f8966f7d3ceb75c0c4f7f8c594a14` |
| x86_64-linux-gnu | `1e5ef6d287a950b1d69868c721a8ed039d4928c2d0d660364226f95280abf453` | `1e5ef6d287a950b1d69868c721a8ed039d4928c2d0d660364226f95280abf453` |

## Baseline

| Suite | Level | Target | Total | PASS | SKIP | XFAIL | Fail | Applicable pass |
|---|---:|---|---:|---:|---:|---:|---:|---:|
| ctestsuite | O0 | arm64-linux | 219 | 211 | 0 | 0 | 8 | 96.35% |
| ctestsuite | O0 | x86_64-linux-gnu | 219 | 212 | 0 | 0 | 7 | 96.80% |
| ctestsuite | O1 | arm64-linux | 219 | 211 | 0 | 0 | 8 | 96.35% |
| ctestsuite | O1 | x86_64-linux-gnu | 219 | 212 | 0 | 0 | 7 | 96.80% |
| ctestsuite | O2 | arm64-linux | 219 | 211 | 0 | 0 | 8 | 96.35% |
| ctestsuite | O2 | x86_64-linux-gnu | 219 | 212 | 0 | 0 | 7 | 96.80% |
| ctestsuite | O3 | arm64-linux | 219 | 211 | 0 | 0 | 8 | 96.35% |
| ctestsuite | O3 | x86_64-linux-gnu | 219 | 212 | 0 | 0 | 7 | 96.80% |
| ctestsuite | Os | arm64-linux | 219 | 211 | 0 | 0 | 8 | 96.35% |
| ctestsuite | Os | x86_64-linux-gnu | 219 | 212 | 0 | 0 | 7 | 96.80% |
| torture-compile | O0 | arm64-linux | 2016 | 1401 | 376 | 0 | 239 | 85.43% |
| torture-compile | O0 | x86_64-linux-gnu | 2016 | 1399 | 376 | 0 | 241 | 85.30% |
| torture-compile | O1 | arm64-linux | 2016 | 1401 | 376 | 0 | 239 | 85.43% |
| torture-compile | O1 | x86_64-linux-gnu | 2016 | 1400 | 376 | 0 | 240 | 85.37% |
| torture-compile | O2 | arm64-linux | 2016 | 1401 | 376 | 0 | 239 | 85.43% |
| torture-compile | O2 | x86_64-linux-gnu | 2016 | 1400 | 376 | 0 | 240 | 85.37% |
| torture-compile | O3 | arm64-linux | 2016 | 1401 | 376 | 0 | 239 | 85.43% |
| torture-compile | O3 | x86_64-linux-gnu | 2016 | 1400 | 376 | 0 | 240 | 85.37% |
| torture-compile | Os | arm64-linux | 2016 | 1401 | 376 | 0 | 239 | 85.43% |
| torture-compile | Os | x86_64-linux-gnu | 2016 | 1400 | 376 | 0 | 240 | 85.37% |
| torture-execute | O0 | arm64-linux | 1752 | 1012 | 257 | 0 | 483 | 67.69% |
| torture-execute | O0 | x86_64-linux-gnu | 1752 | 1012 | 257 | 0 | 483 | 67.69% |
| torture-execute | O1 | arm64-linux | 1752 | 1014 | 257 | 0 | 481 | 67.83% |
| torture-execute | O1 | x86_64-linux-gnu | 1752 | 1014 | 257 | 0 | 481 | 67.83% |
| torture-execute | O2 | arm64-linux | 1752 | 1014 | 257 | 0 | 481 | 67.83% |
| torture-execute | O2 | x86_64-linux-gnu | 1752 | 1014 | 257 | 0 | 481 | 67.83% |
| torture-execute | O3 | arm64-linux | 1752 | 1014 | 257 | 0 | 481 | 67.83% |
| torture-execute | O3 | x86_64-linux-gnu | 1752 | 1014 | 257 | 0 | 481 | 67.83% |
| torture-execute | Os | arm64-linux | 1752 | 1014 | 257 | 0 | 481 | 67.83% |
| torture-execute | Os | x86_64-linux-gnu | 1752 | 1014 | 257 | 0 | 481 | 67.83% |
| torture-execute-ieee | O0 | arm64-linux | 78 | 18 | 29 | 0 | 31 | 36.73% |
| torture-execute-ieee | O0 | x86_64-linux-gnu | 78 | 18 | 29 | 0 | 31 | 36.73% |
| torture-execute-ieee | O1 | arm64-linux | 78 | 18 | 29 | 0 | 31 | 36.73% |
| torture-execute-ieee | O1 | x86_64-linux-gnu | 78 | 18 | 29 | 0 | 31 | 36.73% |
| torture-execute-ieee | O2 | arm64-linux | 78 | 18 | 29 | 0 | 31 | 36.73% |
| torture-execute-ieee | O2 | x86_64-linux-gnu | 78 | 18 | 29 | 0 | 31 | 36.73% |
| torture-execute-ieee | O3 | arm64-linux | 78 | 18 | 29 | 0 | 31 | 36.73% |
| torture-execute-ieee | O3 | x86_64-linux-gnu | 78 | 18 | 29 | 0 | 31 | 36.73% |
| torture-execute-ieee | Os | arm64-linux | 78 | 18 | 29 | 0 | 31 | 36.73% |
| torture-execute-ieee | Os | x86_64-linux-gnu | 78 | 18 | 29 | 0 | 31 | 36.73% |

## Known Pre-triaged Classes

| Class | Failed cells | Disposition |
|---|---:|---|
| gcc-builtin | 4720 | `wontfix-0.1.0` |
| nested-functions | 250 | `wontfix-0.1.0` |
| complex | 270 | `out-of-scope` |
| computed-goto | 0 | `wontfix-0.1.0` |
| asm-goto | 70 | `wontfix-0.1.0` |
| vector-mode-attribute | 720 | `wontfix-0.1.0` |

## SKIP Policy

| Requirement | Detail | Skipped cells |
|---|---|---:|
| - | conditional dg-do | 260 |
| - | conditional effective-target arm_arch_v5t_thumb_ok | 10 |
| - | conditional options directive | 300 |
| - | quoted include unavailable: ..<path> | 70 |
| - | requires DejaGNU builtins multi-source harness | 330 |
| - | requires libm linkage not supported by torture manifest | 10 |
| - | skip-unless:dfp,dfprt | 10 |
| - | skip-unless:indirect_calls,label_values | 10 |
| - | skip-unless:indirect_calls,ptr32plus,untyped_assembly | 20 |
| - | skip-unless:indirect_calls,untyped_assembly | 30 |
| - | skip-unless:indirect_jumps,label_values | 180 |
| - | skip-unless:label_values,nonlocal_goto | 10 |
| - | skip-unless:label_values,trampolines | 10 |
| - | unsupported DejaGNU .x control | 260 |
| - | unsupported dg-do <id> | 10 |
| - | unsupported dg-do assemble | 130 |
| - | unsupported directive dg-add-options | 320 |
| - | unsupported directive dg-error | 50 |
| - | unsupported directive dg-final | 30 |
| - | unsupported directive dg-message | 30 |
| - | unsupported directive dg-prune-output | 40 |
| - | unsupported directive dg-require-alias | 80 |
| - | unsupported directive dg-require-dll | 10 |
| - | unsupported directive dg-require-profiling | 10 |
| - | unsupported directive dg-require-stack-check | 10 |
| - | unsupported directive dg-require-stack-size | 310 |
| - | unsupported directive dg-require-visibility | 10 |
| - | unsupported directive dg-require-weak | 20 |
| - | unsupported directive dg-timeout-factor | 60 |
| - | unsupported directive dg-warning | 10 |
| - | unsupported directive dg-xfail-if | 150 |
| - | unsupported directive dg-xfail-run-if | 10 |
| - | unsupported flag -O | 20 |
| - | unsupported flag -O0 | 20 |
| - | unsupported flag -O2 | 50 |
| - | unsupported flag -Ofast | 10 |
| - | unsupported flag -Og | 20 |
| - | unsupported flag -S | 10 |
| - | unsupported flag -Warray-bounds | 10 |
| - | unsupported flag -Wcompare-distinct-pointer-types | 10 |
| - | unsupported flag -Wdeprecated-declarations | 10 |
| - | unsupported flag -Winline | 10 |
| - | unsupported flag -Wno-old-style-definition | 10 |
| - | unsupported flag -Wno-psabi | 40 |
| - | unsupported flag -Wno-stringop-overflow | 10 |
| - | unsupported flag -fconserve-stack | 10 |
| - | unsupported flag -fcx-limited-range | 10 |
| - | unsupported flag -fdump-tree-optimized | 10 |
| - | unsupported flag -fexceptions | 30 |
| - | unsupported flag -ffast-math | 40 |
| - | unsupported flag -fgimple | 20 |
| - | unsupported flag -fgnu89-inline | 210 |
| - | unsupported flag -findirect-inlining | 10 |
| - | unsupported flag -finput-charset=utf-8 | 10 |
| - | unsupported flag -finstrument-functions | 10 |
| - | unsupported flag -fipa-modref | 10 |
| - | unsupported flag -fira-algorithm=priority | 10 |
| - | unsupported flag -flive-range-shrinkage | 10 |
| - | unsupported flag -floop-parallelize-all | 10 |
| - | unsupported flag -fmodulo-sched | 40 |
| - | unsupported flag -fmove-loop-invariants | 10 |
| - | unsupported flag -fno-builtin-abort | 10 |
| - | unsupported flag -fno-early-inlining | 30 |
| - | unsupported flag -fno-guess-branch-probability | 10 |
| - | unsupported flag -fno-if-conversion | 10 |
| - | unsupported flag -fno-inline | 40 |
| - | unsupported flag -fno-ira-share-spill-slots | 10 |
| - | unsupported flag -fno-ivopts | 10 |
| - | unsupported flag -fno-move-loop-invariants | 10 |
| - | unsupported flag -fno-printf-return-value | 10 |
| - | unsupported flag -fno-strict-overflow | 20 |
| - | unsupported flag -fno-trapping-math | 40 |
| - | unsupported flag -fno-tree-ccp | 10 |
| - | unsupported flag -fno-tree-ch | 10 |
| - | unsupported flag -fno-tree-coalesce-vars | 10 |
| - | unsupported flag -fno-tree-dce | 10 |
| - | unsupported flag -fno-tree-dominator-opts | 10 |
| - | unsupported flag -fno-tree-forwprop | 10 |
| - | unsupported flag -fno-tree-sra | 10 |
| - | unsupported flag -fno-vect-cost-model | 10 |
| - | unsupported flag -fnon-call-exceptions | 10 |
| - | unsupported flag -fpermissive | 710 |
| - | unsupported flag -fsave-optimization-record | 30 |
| - | unsupported flag -ftree-loop-distribution | 10 |
| - | unsupported flag -ftree-slp-vectorize | 10 |
| - | unsupported flag -funroll-loops | 30 |
| - | unsupported flag -funswitch-loops | 10 |
| - | unsupported flag -g | 70 |
| - | unsupported flag -std=gnu23 | 70 |
| - | unsupported flag -w | 20 |
| - | upstream dg-skip-if condition | 1050 |
| indirect_jumps | skip-unless:indirect_jumps | 160 |
| int128 | skip-unless:int128 | 40 |
| label_values | skip-unless:label_values | 230 |
| nonlocal_goto | skip-unless:nonlocal_goto | 10 |
| return_address | skip-unless:return_address | 50 |
| trampolines | skip-unless:trampolines | 160 |
| untyped_assembly | skip-unless:untyped_assembly | 190 |

## Buckets

### Bucket 1

- Count: 4100
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `70f8ab7d8e3ce9b973cfeb05b4e407e6efc4b75594aa1ae13705eadb9e57c152`
- Exemplars: torture-compile/20021001-1.c@O0@arm64-linux, torture-compile/20021001-1.c@O0@x86_64-linux-gnu, torture-compile/20021001-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: <id> is not a builtin this compiler implements
- Labels: pretriaged=4100/4100
- Tags: -
- Optdiv members: 0 of 4100
- Optdiv exemplars: -
- Hypothesis: Exercises a GNU extension tiered out in Sprint 55.
- Disposition: `wontfix-0.1.0`

### Bucket 2

- Count: 700
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `66d1fbec7cc38c7773dabe44a03d3cb030bbd39b07efd6a681e2066e08757bcd`
- Exemplars: torture-compile/icfmatch.c@O0@arm64-linux, torture-compile/icfmatch.c@O0@x86_64-linux-gnu, torture-compile/icfmatch.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: the <id> attribute is not supported: it would create vector types with no SysV or AAPCS64 parameter contract (docs<path>
- Labels: pretriaged=700/700
- Tags: -
- Optdiv members: 0 of 700
- Optdiv exemplars: -
- Hypothesis: Exercises a GNU extension tiered out in Sprint 55.
- Disposition: `wontfix-0.1.0`

### Bucket 3

- Count: 560
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `6350a4ee5bef78504001ddad6adf026b0ee68f80282b77424f57f830ff7a98eb`
- Exemplars: torture-compile/20000105-1.c@O0@arm64-linux, torture-compile/20000105-1.c@O0@x86_64-linux-gnu, torture-compile/20000105-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: <id> is not a builtin this compiler implements (see src<path>
- Labels: pretriaged=560/560
- Tags: -
- Optdiv members: 0 of 560
- Optdiv exemplars: -
- Hypothesis: Exercises a GNU extension tiered out in Sprint 55.
- Disposition: `wontfix-0.1.0`

### Bucket 4

- Count: 370
- Cluster: signal=`-`; phase=`sema`
- Fingerprint: `b28fda1f26cb0a1089e244af93736842513e4362d7553c1141717119a70cc3d9`
- Exemplars: torture-compile/20021110.c@O0@arm64-linux, torture-compile/20021110.c@O0@x86_64-linux-gnu, torture-compile/20021110.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: a struct or union must have at least one named member; the GNU no-named-member extension is not supported (docs<path>
- Labels: -
- Tags: -
- Optdiv members: 0 of 370
- Optdiv exemplars: -
- Hypothesis: GNU zero-sized empty structs are deliberately refused because they violate the alias and memory-safety object-extent model.
- Disposition: `wontfix-0.1.0`

### Bucket 5

- Count: 270
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `7536168f49b887b0c922ece0f8e5fa7364c8092f1c52aaf66bbf57e02ae04491`
- Exemplars: torture-compile/20060625-1.c@O0@arm64-linux, torture-compile/20060625-1.c@O0@x86_64-linux-gnu, torture-compile/20060625-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: '_Complex' is out of scope for v0.1.0: cgfried defines __STDC_NO_COMPLEX__
- Labels: pretriaged=270/270
- Tags: -
- Optdiv members: 0 of 270
- Optdiv exemplars: -
- Hypothesis: Uses C complex arithmetic, outside the v0.1.0 language scope.
- Disposition: `out-of-scope`

### Bucket 6

- Count: 250
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `c124dfd380c232ae3de95f4090aa8b506367b5f5fd1b0eb7118562e9549e9e89`
- Exemplars: torture-compile/20010605-1.c@O0@arm64-linux, torture-compile/20010605-1.c@O0@x86_64-linux-gnu, torture-compile/20010605-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: nested functions are not supported: gcc implements them with an executable trampoline on the stack, which is outside the v0.1.0 scope contract (docs<path>
- Labels: pretriaged=250/250
- Tags: -
- Optdiv members: 0 of 250
- Optdiv exemplars: -
- Hypothesis: Exercises a GNU extension tiered out in Sprint 55.
- Disposition: `wontfix-0.1.0`

### Bucket 7

- Count: 80
- Cluster: signal=`6`; phase=`run`
- Runtime split: `non-optdiv`
- Fingerprint: `d96de1e2731a304c638ba95f8598821d510810fc7fbed796448f9e738467fe00`
- Exemplars: torture-execute/20020412-1.c@O0@arm64-linux, torture-execute/20020412-1.c@O1@arm64-linux, torture-execute/20020412-1.c@O2@arm64-linux
- Diagnostic: program killed by signal 6
- Labels: -
- Tags: -
- Optdiv members: 0 of 80
- Optdiv exemplars: -
- Hypothesis: QEMU supplies target-specific abort stderr for the ARM SIGABRT cases; the merged runtime family still requires testcase-specific decomposition.
- Disposition: `fix-sprint:s56.5-runtime-abort-decomposition`

### Bucket 8

- Count: 80
- Cluster: signal=`6`; phase=`run`
- Runtime split: `non-optdiv`
- Fingerprint: `de25f493e2a030af329f5f01121c9f9249da8508936bfa0a119d0a5c8f638731`
- Exemplars: torture-execute/20020412-1.c@O0@x86_64-linux-gnu, torture-execute/20020412-1.c@O1@x86_64-linux-gnu, torture-execute/20020412-1.c@O2@x86_64-linux-gnu
- Diagnostic: program killed by signal 6
- Labels: -
- Tags: -
- Optdiv members: 0 of 80
- Optdiv exemplars: -
- Hypothesis: Signal-only normalization merges cross-target runtime SIGABRT cases from distinct wrong-result families that require testcase-specific decomposition.
- Disposition: `fix-sprint:s56.5-runtime-abort-decomposition`

### Bucket 9

- Count: 70
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `90b3c7e710b66595490d7160026f5ab0d8246476e2d4143febe0d361bc48c061`
- Exemplars: torture-compile/20000224-1.c@O0@arm64-linux, torture-compile/20000224-1.c@O0@x86_64-linux-gnu, torture-compile/20000224-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: expected '}' at end of initializer list
- Labels: -
- Tags: -
- Optdiv members: 0 of 70
- Optdiv exemplars: -
- Hypothesis: The parser does not accept the historical GNU field-colon designated-initializer syntax.
- Disposition: `fix-sprint:s56.5-old-style-designators`

### Bucket 10

- Count: 70
- Cluster: signal=`-`; phase=`ir-verify`
- Fingerprint: `c84a313e2bfb3cc72057ff69d014a9d22d1bf0c7d9032d7efef08ba1de4bbb1d`
- Exemplars: torture-compile/pr106751.c@O0@arm64-linux, torture-compile/pr106751.c@O0@x86_64-linux-gnu, torture-compile/pr106751.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: 'asm goto' is not supported; jumping out of an asm block needs control-flow edges the IR verifier could only trust rather than check (docs<path>
- Labels: pretriaged=70/70
- Tags: -
- Optdiv members: 0 of 70
- Optdiv exemplars: -
- Hypothesis: Exercises a GNU extension tiered out in Sprint 55.
- Disposition: `wontfix-0.1.0`

### Bucket 11

- Count: 70
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `f3ff5037426a3f4ed405b6fbc638a4d4d9d4411875a6434a2502a3cc083c5d58`
- Exemplars: torture-compile/960201-1.c@O0@arm64-linux, torture-compile/960201-1.c@O0@x86_64-linux-gnu, torture-compile/960201-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: the <id> attribute is not yet implemented, and ignoring it would change layout, linkage or behaviour rather than just a diagnostic (docs<path>
- Labels: -
- Tags: -
- Optdiv members: 0 of 70
- Optdiv exemplars: -
- Hypothesis: transparent_union is deliberately refused because ignoring its calling-convention semantics would miscompile calls.
- Disposition: `wontfix-0.1.0`

### Bucket 12

- Count: 50
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `364b4e24f4d3bba9084ddbcc0a0a425e2ed0a29768c23cf5b9abe9c8713521c7`
- Exemplars: torture-execute-ieee/fp-cmp-4f.c@O0@arm64-linux, torture-execute-ieee/fp-cmp-4f.c@O0@x86_64-linux-gnu, torture-execute-ieee/fp-cmp-4f.c@O1@arm64-linux
- Diagnostic: <path>:<loc>: error: <id> is not a builtin this compiler implements
- Labels: pretriaged=50/50
- Tags: -
- Optdiv members: 0 of 50
- Optdiv exemplars: -
- Hypothesis: Exercises a GNU extension tiered out in Sprint 55.
- Disposition: `wontfix-0.1.0`

### Bucket 13

- Count: 40
- Cluster: signal=`-`; phase=`ld`
- Fingerprint: `0c2661f71dc283b5d3141d5aead0f8add44c3878ade8e9b0d08482860df6a2fa`
- Exemplars: ctestsuite/00174.c@O0@arm64-linux, ctestsuite/00174.c@O0@x86_64-linux-gnu, ctestsuite/00174.c@O1@arm64-linux
- Diagnostic: [ld-class=missing-libm] (<section>+<offset>): undefined reference to <id>
- Labels: -
- Tags: c99, needs-cpp, needs-libc, portable
- Optdiv members: 0 of 40
- Optdiv exemplars: -
- Hypothesis: The case requires libm, but the torture execution link contract does not yet add the required math library.
- Disposition: `fix-sprint:s56.5-torture-libm-linkage`

### Bucket 14

- Count: 40
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `36853079a0fded16af84c567c7fbf2556e105b6053547abae732d74906b5cd90`
- Exemplars: torture-compile/20011217-2.c@O0@arm64-linux, torture-compile/20011217-2.c@O0@x86_64-linux-gnu, torture-compile/20011217-2.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: expected a type name but found '__extension__'
- Labels: -
- Tags: -
- Optdiv members: 0 of 40
- Optdiv exemplars: -
- Hypothesis: The implemented __extension__ marker is not accepted in a cast type-name before a compound literal.
- Disposition: `fix-sprint:s56.5-extension-type-name`

### Bucket 15

- Count: 40
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `afb9f891e77ea9ff3bedc8426dab776716e83fb433d22c8e2f1ed92a0df6cd4c`
- Exemplars: torture-compile/20030910-1.c@O0@arm64-linux, torture-compile/20030910-1.c@O0@x86_64-linux-gnu, torture-compile/20030910-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: expected ';' after an expression
- Labels: -
- Tags: -
- Optdiv members: 0 of 40
- Optdiv exemplars: -
- Hypothesis: The case uses GNU complex declarations and component operators, outside the v0.1.0 language scope.
- Disposition: `out-of-scope`

### Bucket 16

- Count: 40
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `d2a3673523fa1086b58179c65a40f750586e433d12880533ff901759e5e9ad76`
- Exemplars: torture-execute/pr123864.c@O0@arm64-linux, torture-execute/pr123864.c@O0@x86_64-linux-gnu, torture-execute/pr123864.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: expected a declaration but found '['
- Labels: -
- Tags: -
- Optdiv members: 0 of 40
- Optdiv exemplars: -
- Hypothesis: The case uses C23 double-bracket attribute syntax while v0.1.0 targets C17 and GNU C17.
- Disposition: `out-of-scope`

### Bucket 17

- Count: 40
- Cluster: signal=`-`; phase=`sema`
- Fingerprint: `d9666dc4a0d584ad384b4e3a2cc92ecb5c9d2946c1d15fbead406b8bf7b9cc5e`
- Exemplars: torture-compile/pr45919.c@O0@arm64-linux, torture-compile/pr45919.c@O0@x86_64-linux-gnu, torture-compile/pr45919.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: initialization of a flexible array member is a GNU extension that is not supported
- Labels: -
- Tags: -
- Optdiv members: 0 of 40
- Optdiv exemplars: -
- Hypothesis: GNU initialization of a flexible array member remains rejected by a stale Sprint 55 deferral.
- Disposition: `fix-sprint:s56.5-flexible-array-initializer`

### Bucket 18

- Count: 40
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `e83262900091a3f95ecb503518ea616ec0814569020da57daccd5240c82d49a2`
- Exemplars: torture-compile/pr42196-1.c@O0@arm64-linux, torture-compile/pr42196-1.c@O0@x86_64-linux-gnu, torture-compile/pr42196-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: expected a member declaration but found <id>
- Labels: -
- Tags: -
- Optdiv members: 0 of 40
- Optdiv exemplars: -
- Hypothesis: The union member uses GNU complex integer type and real/imaginary component operators, outside the v0.1.0 language scope.
- Disposition: `out-of-scope`

### Bucket 19

- Count: 40
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `f765937aa5544b30bfa7277c9210cf239099e5f3c63a6598adb06d05a6dbab46`
- Exemplars: torture-compile/941019-1.c@O0@arm64-linux, torture-compile/941019-1.c@O0@x86_64-linux-gnu, torture-compile/941019-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: unknown type name <id>
- Labels: -
- Tags: -
- Optdiv members: 0 of 40
- Optdiv exemplars: -
- Hypothesis: The declaration uses GNU complex long double type, outside the v0.1.0 language scope.
- Disposition: `out-of-scope`

### Bucket 20

- Count: 30
- Cluster: signal=`-`; phase=`sema`
- Fingerprint: `719f040fbd028be5e8a9f8add4109a1d3215b8a6e44d8084de50159c6182fe7c`
- Exemplars: torture-compile/pr38789.c@O0@arm64-linux, torture-compile/pr38789.c@O0@x86_64-linux-gnu, torture-compile/pr38789.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: <id> is not a constant expression
- Labels: -
- Tags: -
- Optdiv members: 0 of 30
- Optdiv exemplars: -
- Hypothesis: Address-constant folding rejects a nested address-of member expression that denotes a valid static initializer.
- Disposition: `fix-sprint:s56.5-nested-member-address-constant`

### Bucket 21

- Count: 30
- Cluster: signal=`-`; phase=`cg`
- Fingerprint: `c0cda5231420c23bdfdec54c0367f9cfb03120dae294b0351b7a16294bac87ac`
- Exemplars: torture-compile/limits-blockid.c@O0@arm64-linux, torture-compile/limits-blockid.c@O0@x86_64-linux-gnu, torture-compile/limits-blockid.c@O1@arm64-linux
- Diagnostic: timeout: sending signal TERM to command 'build<path>
- Labels: -
- Tags: -
- Optdiv members: 0 of 30
- Optdiv exemplars: -
- Hypothesis: Large macro-expanded or optimization-heavy cases exceed the compiler timeout, exposing frontend or code-generation scalability limits.
- Disposition: `fix-sprint:s56.5-compiler-scalability-timeout`

### Bucket 22

- Count: 30
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `f0bc10177e20eb2f0a1a67cdf600fb7f0e43ce8a6600f73a7c51df8943d83791`
- Exemplars: torture-execute/complex-1.c@O0@arm64-linux, torture-execute/complex-1.c@O0@x86_64-linux-gnu, torture-execute/complex-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: invalid suffix on floating constant '1.0i'
- Labels: -
- Tags: -
- Optdiv members: 0 of 30
- Optdiv exemplars: -
- Hypothesis: GNU imaginary floating suffixes exercise complex arithmetic, which Cgfried explicitly excludes from the v0.1.0 language scope.
- Disposition: `out-of-scope`

### Bucket 23

- Count: 20
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `03642ec037628939d63f557579ae8895dd1b51e49365ffe769eea1fb1efa0797`
- Exemplars: torture-compile/20001109-1.c@O0@arm64-linux, torture-compile/20001109-1.c@O0@x86_64-linux-gnu, torture-compile/20001109-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: '_Alignof' requires a type name (the GNU expression form is not supported)
- Labels: -
- Tags: -
- Optdiv members: 0 of 20
- Optdiv exemplars: -
- Hypothesis: GNU __alignof__ expression form is rejected even though GNU expression extensions are otherwise supported.
- Disposition: `fix-sprint:s56.5-alignof-expression`

### Bucket 24

- Count: 20
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `073307837dcc1b051c517c46997080291ea26133ddf35af0e3139fe4931e0a6c`
- Exemplars: torture-compile/pr54559.c@O0@arm64-linux, torture-compile/pr54559.c@O0@x86_64-linux-gnu, torture-compile/pr54559.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: invalid suffix on floating constant '1.0iF'
- Labels: -
- Tags: -
- Optdiv members: 0 of 20
- Optdiv exemplars: -
- Hypothesis: GNU imaginary floating suffixes exercise complex arithmetic, which Cgfried explicitly excludes from the v0.1.0 language scope.
- Disposition: `out-of-scope`

### Bucket 25

- Count: 20
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `23af7754f9be9ce88a3c3ee27dd514f3c5f3fc71fb93562c41a282be2e1ab505`
- Exemplars: torture-compile/complex-1.c@O0@arm64-linux, torture-compile/complex-1.c@O0@x86_64-linux-gnu, torture-compile/complex-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: expected ')' after parameter list
- Labels: -
- Tags: -
- Optdiv members: 0 of 20
- Optdiv exemplars: -
- Hypothesis: The case uses C23 double-bracket attribute syntax while v0.1.0 targets C17 and GNU C17.
- Disposition: `out-of-scope`

### Bucket 26

- Count: 20
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `33116fbd38b9e5cef7d3f1e8ede14274205f2596c9cec2737eacf9a7132cfc25`
- Exemplars: ctestsuite/00216.c@O0@arm64-linux, ctestsuite/00216.c@O0@x86_64-linux-gnu, ctestsuite/00216.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: GNU range designators are not supported
- Labels: -
- Tags: needs-cpp, needs-libc, portable
- Optdiv members: 0 of 20
- Optdiv exemplars: -
- Hypothesis: GNU range designators in aggregate initializers remain rejected despite the completed GNU-extension campaign.
- Disposition: `fix-sprint:s56.5-range-designators`

### Bucket 27

- Count: 20
- Cluster: signal=`-`; phase=`ir-verify`
- Fingerprint: `66d1fbec7cc38c7773dabe44a03d3cb030bbd39b07efd6a681e2066e08757bcd`
- Exemplars: torture-execute/pr121957.c@O0@arm64-linux, torture-execute/pr121957.c@O0@x86_64-linux-gnu, torture-execute/pr121957.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: the <id> attribute is not supported: it would create vector types with no SysV or AAPCS64 parameter contract (docs<path>
- Labels: pretriaged=20/20
- Tags: -
- Optdiv members: 0 of 20
- Optdiv exemplars: -
- Hypothesis: Exercises a GNU extension tiered out in Sprint 55.
- Disposition: `wontfix-0.1.0`

### Bucket 28

- Count: 20
- Cluster: signal=`-`; phase=`sema`
- Fingerprint: `a8a165d75ac747bc8398b95018f15e9ec04be1fae4c417d91fc62a5e5722fba6`
- Exemplars: torture-compile/20000701-1.c@O0@arm64-linux, torture-compile/20000701-1.c@O0@x86_64-linux-gnu, torture-compile/20000701-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: cannot dereference a 'void *'
- Labels: -
- Tags: -
- Optdiv members: 0 of 20
- Optdiv exemplars: -
- Hypothesis: Expression typing rejects GNU void dereference expressions and also misclassifies a conditional between compatible enum and int pointers as void pointer.
- Disposition: `fix-sprint:s56.5-void-deref-and-pointer-composite`

### Bucket 29

- Count: 20
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `e578de15b58de70f4d707195edcd798c9ae00241d327927a5a4414878810ea33`
- Exemplars: torture-compile/991213-1.c@O0@arm64-linux, torture-compile/991213-1.c@O0@x86_64-linux-gnu, torture-compile/991213-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: expected ';' after declaration
- Labels: -
- Tags: -
- Optdiv members: 0 of 20
- Optdiv exemplars: -
- Hypothesis: The cases use GNU complex types or complex component operators, while complex arithmetic is explicitly outside the v0.1.0 language scope.
- Disposition: `out-of-scope`

### Bucket 30

- Count: 20
- Cluster: signal=`-`; phase=`sema`
- Fingerprint: `eb35ff3aa15c37c48869e89bea29b63daea35ed97aee8f0feb16f7ab6ece52e9`
- Exemplars: torture-compile/pr16566-1.c@O0@arm64-linux, torture-compile/pr16566-1.c@O0@x86_64-linux-gnu, torture-compile/pr16566-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: a struct with a flexible array member cannot be a member of another struct (the GNU form is not supported)
- Labels: -
- Tags: -
- Optdiv members: 0 of 20
- Optdiv exemplars: -
- Hypothesis: GNU flexible-array-bearing structs are rejected when embedded in another struct even though Sprint 16 deferred this accepted GNU form to Sprint 55.
- Disposition: `fix-sprint:s56.5-nested-flexible-array-members`

### Bucket 31

- Count: 11
- Cluster: signal=`-`; phase=`ICE`
- Fingerprint: `06b3e0e177e7540cdb368d991c85fa819d09cc8d4e610d7f6f9ee33dfdd53c10`
- Exemplars: torture-compile/mangle-1.c@O0@arm64-linux, torture-compile/mangle-1.c@O0@x86_64-linux-gnu, torture-compile/mangle-1.c@O1@arm64-linux
- Diagnostic: [as] <work><path>: Assembler messages:
- Labels: -
- Tags: -
- Optdiv members: 0 of 11
- Optdiv exemplars: -
- Hypothesis: Accepted asm-label and large-shift cases produce assembler-invalid backend text instead of legal target assembly.
- Disposition: `fix-sprint:s56.5-assembler-invalid-output`

### Bucket 32

- Count: 10
- Cluster: signal=`-`; phase=`cg`
- Fingerprint: `-`
- Exemplars: torture-compile/limits-exprparen.c@O0@arm64-linux, torture-compile/limits-exprparen.c@O0@x86_64-linux-gnu, torture-compile/limits-exprparen.c@O1@arm64-linux
- Diagnostic: output limit exceeded
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: The ten-thousand-level expression nesting stress exceeds the supported translation limit and now reaches the harness output guard before a bounded diagnostic is retained.
- Disposition: `out-of-scope`

### Bucket 33

- Count: 10
- Cluster: signal=`-`; phase=`run`
- Runtime split: `non-optdiv`
- Fingerprint: `-`
- Exemplars: torture-execute/970217-1.c@O0@arm64-linux, torture-execute/970217-1.c@O0@x86_64-linux-gnu, torture-execute/970217-1.c@O1@arm64-linux
- Diagnostic: program exited 1
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: VLA parameter-bound side effects are evaluated with the wrong ordering or cardinality, so sub receives 10 instead of 11.
- Disposition: `fix-sprint:s56.5-vla-parameter-bound-side-effects`

### Bucket 34

- Count: 10
- Cluster: signal=`-`; phase=`sema`
- Fingerprint: `087e22d20269bf72f0f467ee68df2ecc04c2ceeb7dd9d73b38b716c761f5b1cb`
- Exemplars: torture-execute/960416-1.c@O0@arm64-linux, torture-execute/960416-1.c@O0@x86_64-linux-gnu, torture-execute/960416-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: cannot cast to non-scalar type 'union <anonymous>'
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: GNU scalar-to-union casts used for representation access are rejected before lowering.
- Disposition: `fix-sprint:s56.5-gnu-scalar-to-union-casts`

### Bucket 35

- Count: 10
- Cluster: signal=`-`; phase=`run`
- Fingerprint: `0c6f5475c400ab79f1a12d301c82b04860d2803f7956b7313947e54f334cb189`
- Exemplars: ctestsuite/00205.c@O0@arm64-linux, ctestsuite/00205.c@O0@x86_64-linux-gnu, ctestsuite/00205.c@O1@arm64-linux
- Diagnostic: stdout differs from expected output
- Labels: -
- Tags: c89, needs-cpp, needs-libc, portable
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: Flattened aggregate initialization assigns values to the nested array and trailing struct members in the wrong order.
- Disposition: `fix-sprint:s56.5-flattened-aggregate-initializers`

### Bucket 36

- Count: 10
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `1bee8da051ce7f8b888b00500383c6a9df265d662f65d5387d4439303836f0cd`
- Exemplars: torture-execute/complex-5.c@O0@arm64-linux, torture-execute/complex-5.c@O0@x86_64-linux-gnu, torture-execute/complex-5.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: invalid suffix on floating constant '1.0fi'
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: The case uses imaginary floating suffixes and complex arithmetic, which are explicitly outside the v0.1.0 language scope.
- Disposition: `out-of-scope`

### Bucket 37

- Count: 10
- Cluster: signal=`-`; phase=`run`
- Fingerprint: `2082b1f4d8085345bd3ead486cdd5244655c0b103571ee5a780f92679bac374c`
- Exemplars: ctestsuite/00206.c@O0@arm64-linux, ctestsuite/00206.c@O0@x86_64-linux-gnu, ctestsuite/00206.c@O1@arm64-linux
- Diagnostic: stdout differs from expected output
- Labels: -
- Tags: c89, needs-cpp, needs-libc, portable
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: Pragma push_macro and pop_macro do not preserve and restore macro definitions independently of same-named macros.
- Disposition: `fix-sprint:s56.5-pragma-macro-stack`

### Bucket 38

- Count: 10
- Cluster: signal=`-`; phase=`ICE`
- Fingerprint: `31dd1e91bebbd5fad8aef76683a6729f7c1ca76edf2982ba87e33b259a5c7a37`
- Exemplars: torture-compile/pr71109.c@O0@arm64-linux, torture-compile/pr71109.c@O0@x86_64-linux-gnu, torture-compile/pr71109.c@O1@arm64-linux
- Diagnostic: cgfried: error: ir verify [9] in @bar, block 'while.body2': call to @foo passes 2 args; it takes 3
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: IR verification treats a call through an old-style non-prototype declaration as if the later prototype's fixed arity already applied.
- Disposition: `fix-sprint:s56.5-unprototyped-call-ir`

### Bucket 39

- Count: 10
- Cluster: signal=`-`; phase=`pp`
- Fingerprint: `3563161491e75bebdf12602cda39e1a37df2973eaf8f944662c69abb98b6adbe`
- Exemplars: torture-compile/20010510-1.c@O0@arm64-linux, torture-compile/20010510-1.c@O0@x86_64-linux-gnu, torture-compile/20010510-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: invalid preprocessing directive #ident
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: The GNU #ident preprocessing directive is rejected instead of being consumed and represented or safely ignored.
- Disposition: `fix-sprint:s56.5-gnu-ident-directive`

### Bucket 40

- Count: 10
- Cluster: signal=`-`; phase=`sema`
- Fingerprint: `356cb2e2b1bed51b107023d33508d405e238b0b154adea090a4ae053b4fc61b5`
- Exemplars: torture-compile/20011114-1.c@O0@arm64-linux, torture-compile/20011114-1.c@O0@x86_64-linux-gnu, torture-compile/20011114-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: variable <id> declared void
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: GNU extern-void symbol declarations used solely for address constants are rejected as void objects.
- Disposition: `fix-sprint:s56.5-extern-void-symbols`

### Bucket 41

- Count: 10
- Cluster: signal=`-`; phase=`sema`
- Fingerprint: `48e045cd63ef0497bf3a9f14f4807d91118562a0f430cc6a6de6b5b80ffb9342`
- Exemplars: torture-execute/20000223-1.c@O0@arm64-linux, torture-execute/20000223-1.c@O0@x86_64-linux-gnu, torture-execute/20000223-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: invalid application of '_Alignof' to incomplete type 'void'
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: GNU __alignof__(void) is rejected instead of returning the target's extension alignment.
- Disposition: `fix-sprint:s56.5-alignof-void`

### Bucket 42

- Count: 10
- Cluster: signal=`-`; phase=`sema`
- Fingerprint: `501d37ef121eb397ed8945d1c3a428e05d363d95da41feca3821ce7c0474b105`
- Exemplars: ctestsuite/00219.c@O0@arm64-linux, ctestsuite/00219.c@O0@x86_64-linux-gnu, ctestsuite/00219.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: _Generic association for 'const int' duplicates an earlier one
- Labels: -
- Tags: c89, needs-cpp, needs-libc, portable
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: _Generic duplicate-association checking incorrectly strips top-level qualifiers and conflates int with const int.
- Disposition: `fix-sprint:s56.5-generic-qualified-associations`

### Bucket 43

- Count: 10
- Cluster: signal=`-`; phase=`pp`
- Fingerprint: `577b3a7c47b959332a58fa4e5b7a6cfcfb6e422ec958ce295551830122e30730`
- Exemplars: torture-execute/pr117432.c@O0@arm64-linux, torture-execute/pr117432.c@O0@x86_64-linux-gnu, torture-execute/pr117432.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: macro <id> expects 2 argument(s), got 1
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: The shipped va_start macro cannot handle GNU ellipsis-only functions whose invocation has no last named parameter.
- Disposition: `fix-sprint:s56.5-gnu-varargs-without-named-parameter`

### Bucket 44

- Count: 10
- Cluster: signal=`-`; phase=`sema`
- Fingerprint: `5e10d749ed583a36c352321e1e6165c368cf45cb4074a3abdc3559c01df8443b`
- Exemplars: torture-compile/pr27528.c@O0@arm64-linux, torture-compile/pr27528.c@O0@x86_64-linux-gnu, torture-compile/pr27528.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: unsupported asm constraint character <id> in <id>; an unmodelled constraint would assemble and then run with the operand in the wrong place
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: The GCC s symbolic-address inline-asm constraint is not implemented, so constant object and string addresses are rejected before lowering.
- Disposition: `fix-sprint:s56.5-symbolic-asm-constant-constraint`

### Bucket 45

- Count: 10
- Cluster: signal=`-`; phase=`run`
- Fingerprint: `616e5e0e7b869f0c5d51017e18b7c302d45c0e17036ad9fe165a756a42427650`
- Exemplars: ctestsuite/00213.c@O0@arm64-linux, ctestsuite/00213.c@O0@x86_64-linux-gnu, ctestsuite/00213.c@O1@arm64-linux
- Diagnostic: stdout differs from expected output
- Labels: -
- Tags: c89, portable
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: CFG reachability and label-edge lowering mishandle gotos into syntactically dead loop and statement-expression regions.
- Disposition: `fix-sprint:s56.5-label-reachable-dead-regions`

### Bucket 46

- Count: 10
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `63894c5b52edea1018992f21ad3550b33865536439f4fd724edc5e49bc0bd974`
- Exemplars: torture-compile/pr33855.c@O0@arm64-linux, torture-compile/pr33855.c@O0@x86_64-linux-gnu, torture-compile/pr33855.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: invalid suffix on floating constant '1.0fj'
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: The case uses imaginary j-suffix constants and complex arithmetic, which are explicitly outside the v0.1.0 language scope.
- Disposition: `out-of-scope`

### Bucket 47

- Count: 10
- Cluster: signal=`-`; phase=`ir-verify`
- Fingerprint: `70f8ab7d8e3ce9b973cfeb05b4e407e6efc4b75594aa1ae13705eadb9e57c152`
- Exemplars: torture-compile/pr123386.c@O0@arm64-linux, torture-compile/pr123386.c@O0@x86_64-linux-gnu, torture-compile/pr123386.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: <id> is not a builtin this compiler implements
- Labels: pretriaged=10/10
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: Exercises a GNU extension tiered out in Sprint 55.
- Disposition: `wontfix-0.1.0`

### Bucket 48

- Count: 10
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `7deb38bc3f36e319952f404c30047bbdda54f6c62ed33a68713a0adab0c6e0df`
- Exemplars: torture-execute/complex-4.c@O0@arm64-linux, torture-execute/complex-4.c@O0@x86_64-linux-gnu, torture-execute/complex-4.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: expected ')' after parenthesized expression
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: The case casts to GNU complex types, while complex arithmetic is explicitly outside the v0.1.0 language scope.
- Disposition: `out-of-scope`

### Bucket 49

- Count: 10
- Cluster: signal=`-`; phase=`sema`
- Fingerprint: `82945e9550f35c73abe3693f52a63ffdb9513b97cadf8ca4a22f585b738b53ff`
- Exemplars: torture-compile/pr48517.c@O0@arm64-linux, torture-compile/pr48517.c@O0@x86_64-linux-gnu, torture-compile/pr48517.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: variable <id> has incomplete type 'const unsigned short []'
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: An incomplete static array is not completed from an array compound-literal initializer.
- Disposition: `fix-sprint:s56.5-compound-literal-array-completion`

### Bucket 50

- Count: 10
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `83fdeab1ef4cbb24f3ac29ee248430e84ed701a663cefcc9027c129cac399e17`
- Exemplars: torture-compile/pr99324.c@O0@arm64-linux, torture-compile/pr99324.c@O0@x86_64-linux-gnu, torture-compile/pr99324.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: parameter names (without types) are only allowed in a function definition
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: The case defines an old-style nested function, which Sprint 55 deliberately excludes from v0.1.0.
- Disposition: `wontfix-0.1.0`

### Bucket 51

- Count: 10
- Cluster: signal=`-`; phase=`sema`
- Fingerprint: `89938bfb2700a23731e0b730cfc1d58f7ec13bd0059bda1e9aedc616cc40c36a`
- Exemplars: torture-compile/20071108-1.c@O0@arm64-linux, torture-compile/20071108-1.c@O0@x86_64-linux-gnu, torture-compile/20071108-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: invalid application of 'sizeof' to incomplete type 'int [3] [vla]'
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: Runtime VLA dimensions are incorrectly treated as incomplete when sizeof is applied to a multidimensional VLA.
- Disposition: `fix-sprint:s56.5-multidimensional-vla-sizeof`

### Bucket 52

- Count: 10
- Cluster: signal=`-`; phase=`ld`
- Fingerprint: `8d61a6cd5e279f461d9b8326daed634ac0d1ac6efe2f9cfa38669290d6137fe4`
- Exemplars: torture-execute/941202-1.c@O0@arm64-linux, torture-execute/941202-1.c@O0@x86_64-linux-gnu, torture-execute/941202-1.c@O1@arm64-linux
- Diagnostic: [ld-class=missing-alloca] (<section>+<offset>): undefined reference to <id>
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: GNU89's plain alloca spelling is emitted as an external call instead of lowering through the implemented alloca builtin.
- Disposition: `fix-sprint:s56.5-gnu-alloca-alias`

### Bucket 53

- Count: 10
- Cluster: signal=`-`; phase=`pp`
- Fingerprint: `91d2215b56a7eef42a5038abc931eace3e3f0478ed15892b898474014e9ab6d1`
- Exemplars: torture-compile/950919-1.c@O0@arm64-linux, torture-compile/950919-1.c@O0@x86_64-linux-gnu, torture-compile/950919-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: unexpected token in #if expression
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: The preprocessor does not recognize GCC's deprecated #predicate(answer) assertion syntax inside #if expressions.
- Disposition: `fix-sprint:s56.5-gnu-pp-assertions`

### Bucket 54

- Count: 10
- Cluster: signal=`-`; phase=`cg`
- Fingerprint: `94be3ef186a0396bf67e3c63095e5edcc107266d44173624a9ffef72569eaf32`
- Exemplars: torture-compile/pr78694.c@O0@arm64-linux, torture-compile/pr78694.c@O0@x86_64-linux-gnu, torture-compile/pr78694.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: a reference to an extern _Thread_local (the initial-exec model) is not lowered yet: lands in Sprint 51
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: extern thread-local references need the unimplemented Linux initial-exec lowering path
- Disposition: `fix-sprint:s56.5-extern-tls-initial-exec`

### Bucket 55

- Count: 10
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `9691b83f72e5abe92459479e7169dbe0b5131f572a6725dc5e9a90f2015b1b75`
- Exemplars: torture-compile/20001222-1.c@O0@arm64-linux, torture-compile/20001222-1.c@O0@x86_64-linux-gnu, torture-compile/20001222-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: invalid suffix on floating constant '2.0i'
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: GNU imaginary floating suffixes require complex arithmetic which is outside the v0.1.0 language scope
- Disposition: `out-of-scope`

### Bucket 56

- Count: 10
- Cluster: signal=`-`; phase=`ld`
- Fingerprint: `a1baad615656922dd0c5cf03bae7bde4254cdc850813b578887e94bd88520db6`
- Exemplars: torture-execute/stdarg-4.c@O0@arm64-linux, torture-execute/stdarg-4.c@O0@x86_64-linux-gnu, torture-execute/stdarg-4.c@O1@arm64-linux
- Diagnostic: [ld-class=undefined-symbol] (<section>+<offset>): undefined reference to <id>
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: Only the first file-scope declarator in a comma-separated tentative-definition declaration is emitted, leaving sibling globals undefined.
- Disposition: `fix-sprint:s56.5-sibling-global-emission`

### Bucket 57

- Count: 10
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `acf3af7f1c9c3a7c11b6175147cd3c92078cd42a313164f6b5145b98513ab18c`
- Exemplars: torture-compile/930118-1.c@O0@arm64-linux, torture-compile/930118-1.c@O0@x86_64-linux-gnu, torture-compile/930118-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: <id> block-scoped labels are not supported; our labels have function scope (docs<path>
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: GNU block-scoped __label__ is a documented deliberate v0.1.0 refusal because cgfried labels have function scope
- Disposition: `wontfix-0.1.0`

### Bucket 58

- Count: 10
- Cluster: signal=`-`; phase=`sema`
- Fingerprint: `ae6394ea751369cfa3f15c078e72a70e5989fc74012072b928877865cabac9cb`
- Exemplars: torture-compile/pr42708-1.c@O0@arm64-linux, torture-compile/pr42708-1.c@O0@x86_64-linux-gnu, torture-compile/pr42708-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: cannot cast to non-scalar type 'union YYSTYPE'
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: GNU same-type union casts are rejected instead of behaving as aggregate identity conversions
- Disposition: `fix-sprint:s56.5-gnu-aggregate-self-cast`

### Bucket 59

- Count: 10
- Cluster: signal=`-`; phase=`ICE`
- Fingerprint: `c2fcb3becbded033d3b707693a8dc98bc8b07ee0a64130ac57481b20d79780f2`
- Exemplars: torture-compile/20040317-1.c@O0@arm64-linux, torture-compile/20040317-1.c@O0@x86_64-linux-gnu, torture-compile/20040317-1.c@O1@arm64-linux
- Diagnostic: cgfried: internal compiler error at src<path>:<loc>: lowering lost the local <id> (no alloca binding)
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: VLA array-parameter adjustment loses the local alloca binding during lowering
- Disposition: `fix-sprint:s56.5-vla-parameter-lowering`

### Bucket 60

- Count: 10
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `cc5dfe026bb1609d02fc9a84176be08676c8368afe27a452bcfa84e94873550a`
- Exemplars: torture-execute/20041124-1.c@O0@arm64-linux, torture-execute/20041124-1.c@O0@x86_64-linux-gnu, torture-execute/20041124-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: invalid suffix on integer constant '200i'
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: GNU imaginary integer suffixes require complex arithmetic which is outside the v0.1.0 language scope
- Disposition: `out-of-scope`

### Bucket 61

- Count: 10
- Cluster: signal=`-`; phase=`run`
- Fingerprint: `cfa8a29b24c9c9402d3d557912a1d0bd9dc054a7c7c29e8d4a1d3e81365645eb`
- Exemplars: ctestsuite/00220.c@O0@arm64-linux, ctestsuite/00220.c@O0@x86_64-linux-gnu, ctestsuite/00220.c@O1@arm64-linux
- Diagnostic: stdout differs from expected output
- Labels: -
- Tags: c99, needs-cpp, needs-libc, portable
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: UTF-8 source bytes are widened independently instead of decoded into Unicode code points for wide string literals
- Disposition: `fix-sprint:s56.5-utf8-wide-literal-decoding`

### Bucket 62

- Count: 10
- Cluster: signal=`-`; phase=`sema`
- Fingerprint: `f3a8a201c01269b5a2cccf5f1eac8bdde70f5dbc79692886a1639eab549fb354`
- Exemplars: torture-compile/20001018-1.c@O0@arm64-linux, torture-compile/20001018-1.c@O0@x86_64-linux-gnu, torture-compile/20001018-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: invalid application of 'sizeof' to incomplete type 'char []'
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: composite-type formation fails to inherit the prior bound when an inner extern redeclares the array with incomplete type
- Disposition: `fix-sprint:s56.5-composite-array-bound`

### Bucket 63

- Count: 10
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `f6e3227c914735cdb08ca070859e01469beb8bd02551aa2274362048b0685cf0`
- Exemplars: torture-execute/20041201-1.c@O0@arm64-linux, torture-execute/20041201-1.c@O0@x86_64-linux-gnu, torture-execute/20041201-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: invalid suffix on integer constant '2i'
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: GNU imaginary integer suffixes require complex arithmetic which is outside the v0.1.0 language scope
- Disposition: `out-of-scope`

### Bucket 64

- Count: 10
- Cluster: signal=`-`; phase=`sema`
- Fingerprint: `f9c7d944f95255e125b87ba41cc8c076edc1534f3b1f60329baf2d2af9660dfb`
- Exemplars: torture-compile/20010605-2.c@O0@arm64-linux, torture-compile/20010605-2.c@O0@x86_64-linux-gnu, torture-compile/20010605-2.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: cannot cast to non-scalar type 'union u'
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: GNU same-type union casts are rejected instead of behaving as aggregate identity conversions
- Disposition: `fix-sprint:s56.5-gnu-aggregate-self-cast`

### Bucket 65

- Count: 10
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `fa6021f7913ae8533903065c2ea43cf97c022e38d708bbeee2ccca8638647ce7`
- Exemplars: torture-execute/complex-7.c@O0@arm64-linux, torture-execute/complex-7.c@O0@x86_64-linux-gnu, torture-execute/complex-7.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: invalid suffix on floating constant '2.2if'
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: GNU imaginary floating suffixes require complex arithmetic which is outside the v0.1.0 language scope
- Disposition: `out-of-scope`

### Bucket 66

- Count: 5
- Cluster: signal=`-`; phase=`ICE`
- Fingerprint: `4a0f15109e15770bc7c0e89d07ea45c887c378584cae9904408d66d14bc97d7e`
- Exemplars: torture-compile/20031023-4.c@O0@arm64-linux, torture-compile/20031023-4.c@O1@arm64-linux, torture-compile/20031023-4.c@O2@arm64-linux
- Diagnostic: <path>:<loc>: note: previous definition is here
- Labels: -
- Tags: -
- Optdiv members: 0 of 5
- Optdiv exemplars: -
- Hypothesis: arm64 regalloc cannot represent the testcase's greater-than-2-GiB automatic frame; corrected target-header routing makes the retained previous-definition note the normalized fingerprint.
- Disposition: `fix-sprint:s56.5-arm64-large-stack-frame`

### Bucket 67

- Count: 5
- Cluster: signal=`-`; phase=`sema`
- Fingerprint: `5af19e793c6f35ae33c0aa34abf6552c58591c99400d16038ff878b4332002d0`
- Exemplars: torture-compile/inline-asm-1.c@O0@arm64-linux, torture-compile/inline-asm-1.c@O1@arm64-linux, torture-compile/inline-asm-1.c@O2@arm64-linux
- Diagnostic: <source>:<loc>: error: an asm with more than one register output is not supported on arm64 yet
- Labels: -
- Tags: -
- Optdiv members: 0 of 5
- Optdiv exemplars: -
- Hypothesis: arm64 multi-register-output extended asm is a documented deliberate v0.1.0 refusal
- Disposition: `wontfix-0.1.0`

### Bucket 68

- Count: 5
- Cluster: signal=`-`; phase=`as`
- Fingerprint: `61f7d78fdc241146d501a94c8a4318e23bd540586c8eab78ee1478c26d74d3e4`
- Exemplars: torture-compile/920521-1.c@O0@x86_64-linux-gnu, torture-compile/920521-1.c@O1@x86_64-linux-gnu, torture-compile/920521-1.c@O2@x86_64-linux-gnu
- Diagnostic: cgfried: error: the assembler rejected the assembly for '<source>' (kept at '<work><path> line 10: " f").
- Labels: -
- Tags: -
- Optdiv members: 0 of 5
- Optdiv exemplars: -
- Hypothesis: The test embeds raw target-specific asm mnemonics that are invalid on both supported x86_64 and arm64 assemblers.
- Disposition: `out-of-scope`

### Bucket 69

- Count: 5
- Cluster: signal=`-`; phase=`sema`
- Fingerprint: `6315641ba67866fc0ce23827d143e76d664e050b8b0c57fccaa7b1d735aad925`
- Exemplars: torture-compile/inline-asm-1.c@O0@x86_64-linux-gnu, torture-compile/inline-asm-1.c@O1@x86_64-linux-gnu, torture-compile/inline-asm-1.c@O2@x86_64-linux-gnu
- Diagnostic: <source>:<loc>: error: an asm with more than one register output is not supported yet unless every extra output names one fixed x86 register and has exactly one matching input; a general extra output would require another MIR def (docs<path>
- Labels: -
- Tags: -
- Optdiv members: 0 of 5
- Optdiv exemplars: -
- Hypothesis: allocator-chosen extra x86 register outputs are a documented deliberate v0.1.0 refusal
- Disposition: `wontfix-0.1.0`

### Bucket 70

- Count: 5
- Cluster: signal=`-`; phase=`ICE`
- Fingerprint: `737ff185cf3959109ce8d0dd4bfa4cb1ce024b2ddc5b45d604a5b78292ea2caf`
- Exemplars: torture-compile/20030323-1.c@O0@x86_64-linux-gnu, torture-compile/20030323-1.c@O1@x86_64-linux-gnu, torture-compile/20030323-1.c@O2@x86_64-linux-gnu
- Diagnostic: cgfried: error: mir verify @banana bb1:<loc>: imm64 outside movabs
- Labels: -
- Tags: -
- Optdiv members: 0 of 5
- Optdiv exemplars: -
- Hypothesis: x86 switch lowering leaves a large case constant in an immediate form that requires movabs
- Disposition: `fix-sprint:s56.5-x86-imm64-materialization`

### Bucket 71

- Count: 5
- Cluster: signal=`-`; phase=`ICE`
- Fingerprint: `759ab6b2e233e715bd42b543868bd046fd8e396ab73ca78a4e2ce7a213324010`
- Exemplars: ctestsuite/00204.c@O0@arm64-linux, ctestsuite/00204.c@O1@arm64-linux, ctestsuite/00204.c@O2@arm64-linux
- Diagnostic: cgfried: internal compiler error at src<path>:<loc>: abi_arg_place: stacked aggregate of 64 bytes needs 8 eightbytes, over the 4-leaf plan
- Labels: -
- Tags: c99, needs-cpp, needs-libc, portable
- Optdiv members: 0 of 5
- Optdiv exemplars: -
- Hypothesis: arm64 ABI planning caps stacked aggregate arguments at four leaves and ICEs on a valid 64-byte argument
- Disposition: `fix-sprint:s56.5-arm64-stacked-large-aggregate-abi`

### Bucket 72

- Count: 5
- Cluster: signal=`-`; phase=`sema`
- Fingerprint: `b97306b069e9fa49dc42de21ce16c5095369585ba07b1a9833d61be77b99e683`
- Exemplars: torture-compile/pr34966.c@O0@x86_64-linux-gnu, torture-compile/pr34966.c@O1@x86_64-linux-gnu, torture-compile/pr34966.c@O2@x86_64-linux-gnu
- Diagnostic: <source>:<loc>: error: unsupported x87 asm operand shape; supported forms are one read-write long double ("+t"), musl's t<path> remainder loop, and a clobbered t input converted to memory
- Labels: -
- Tags: -
- Optdiv members: 0 of 5
- Optdiv exemplars: -
- Hypothesis: The valid GCC "=t" x87 output with a "0" tied double input is rejected by Cgfried's restricted x87 operand-shape lowering.
- Disposition: `fix-sprint:s56.5-x87-tied-double-operand-shape`

### Bucket 73

- Count: 5
- Cluster: signal=`-`; phase=`as`
- Fingerprint: `ef08225eb7c8eae8444a144fc059c944c4d777a17209092f661c7671b59e78a9`
- Exemplars: torture-compile/920521-1.c@O0@arm64-linux, torture-compile/920521-1.c@O1@arm64-linux, torture-compile/920521-1.c@O2@arm64-linux
- Diagnostic: cgfried: error: the assembler rejected the assembly for '<source>' (kept at '<work><path> line 11: " f").
- Labels: -
- Tags: -
- Optdiv members: 0 of 5
- Optdiv exemplars: -
- Hypothesis: The test embeds raw target-specific asm text that the arm64 cross assembler rejects at a target-dependent assembly line.
- Disposition: `out-of-scope`

### Bucket 74

- Count: 4
- Cluster: signal=`-`; phase=`ld`
- Fingerprint: `aace2c03842f0e2505bcb4d7ea1906463cf98a369466b5c20a2753de0e9efc31`
- Exemplars: torture-execute/20030330-1.c@O0@arm64-linux, torture-execute/20030330-1.c@O0@x86_64-linux-gnu, torture-execute/medce-1.c@O0@arm64-linux
- Diagnostic: [ld-class=deliberate-link-error] (<section>+<offset>): undefined reference to <id>
- Labels: -
- Tags: -
- Optdiv members: 0 of 4
- Optdiv exemplars: -
- Hypothesis: O0 emission retains unreachable or unreferenced calls deliberately named link_error
- Disposition: `fix-sprint:s56.5-dead-code-link-elimination`

## Policy Overlay

- Applied decisions: 65
- Stale decisions: 0

## Coverage

- Failed cells: 7595
- Bucketed cells: 7595
- Unbucketed cells: 0
- Unresolved buckets: 0
- Bucket coverage: 100.00%
- Misc bucket share: 0.00% (no misc bucket is emitted)
- Phase-end target: at least 95% of applicable execute cells passing.
