# Sprint 56 Torture Triage

Generated deterministically from `# cgf-torture-results-v2` streams.

## Provenance

- source-revision: `bfdb8f151e59149a0b995e84db74f817ee1af948`
- compiler-source-sha256: `5c082b98dd1668bbb4f53783829603c665bc61505f6852738fc6383c5cddc115`
- harness-sha256: `c8495eac7944b71a0b78064a208b7fe7da0834be74cc93ca68b5a051aa1e43e9`
- torture-manifest-sha256: `8967e250c609984a4a9e50ade6f0de10a36c5a3d956759b560940fdcc2e52f1a`
- ctestsuite-manifest-sha256: `859ef7266c1ce061c7ed659abd9a2bd2782902d5f4c96085ce35249ae7cddd7e`

| Target | Compiler binary SHA-256 | Compiler driver SHA-256 |
|---|---|---|
| arm64-linux | `b7ae110fd73a76128bc6c3b412aff9f5e7088f50de91bad94f1065949e4467b7` | `b7ae110fd73a76128bc6c3b412aff9f5e7088f50de91bad94f1065949e4467b7` |
| x86_64-linux-gnu | `50d01669b600bf0b6291a8bc3d7b6f1173f74786d8808b8ade9f7ab1c50607fd` | `50d01669b600bf0b6291a8bc3d7b6f1173f74786d8808b8ade9f7ab1c50607fd` |

## Baseline

| Suite | Level | Target | Total | PASS | SKIP | XFAIL | Fail | Applicable pass |
|---|---:|---|---:|---:|---:|---:|---:|---:|
| ctestsuite | O0 | arm64-linux | 219 | 214 | 0 | 0 | 5 | 97.72% |
| ctestsuite | O0 | x86_64-linux-gnu | 219 | 215 | 0 | 0 | 4 | 98.17% |
| ctestsuite | O1 | arm64-linux | 219 | 214 | 0 | 0 | 5 | 97.72% |
| ctestsuite | O1 | x86_64-linux-gnu | 219 | 215 | 0 | 0 | 4 | 98.17% |
| ctestsuite | O2 | arm64-linux | 219 | 214 | 0 | 0 | 5 | 97.72% |
| ctestsuite | O2 | x86_64-linux-gnu | 219 | 215 | 0 | 0 | 4 | 98.17% |
| ctestsuite | O3 | arm64-linux | 219 | 214 | 0 | 0 | 5 | 97.72% |
| ctestsuite | O3 | x86_64-linux-gnu | 219 | 215 | 0 | 0 | 4 | 98.17% |
| ctestsuite | Os | arm64-linux | 219 | 214 | 0 | 0 | 5 | 97.72% |
| ctestsuite | Os | x86_64-linux-gnu | 219 | 215 | 0 | 0 | 4 | 98.17% |
| torture-compile | O0 | arm64-linux | 2016 | 1418 | 376 | 0 | 222 | 86.46% |
| torture-compile | O0 | x86_64-linux-gnu | 2016 | 1416 | 376 | 0 | 224 | 86.34% |
| torture-compile | O1 | arm64-linux | 2016 | 1418 | 376 | 0 | 222 | 86.46% |
| torture-compile | O1 | x86_64-linux-gnu | 2016 | 1417 | 376 | 0 | 223 | 86.40% |
| torture-compile | O2 | arm64-linux | 2016 | 1418 | 376 | 0 | 222 | 86.46% |
| torture-compile | O2 | x86_64-linux-gnu | 2016 | 1417 | 376 | 0 | 223 | 86.40% |
| torture-compile | O3 | arm64-linux | 2016 | 1418 | 376 | 0 | 222 | 86.46% |
| torture-compile | O3 | x86_64-linux-gnu | 2016 | 1417 | 376 | 0 | 223 | 86.40% |
| torture-compile | Os | arm64-linux | 2016 | 1418 | 376 | 0 | 222 | 86.46% |
| torture-compile | Os | x86_64-linux-gnu | 2016 | 1417 | 376 | 0 | 223 | 86.40% |
| torture-execute | O0 | arm64-linux | 1752 | 1023 | 257 | 0 | 472 | 68.43% |
| torture-execute | O0 | x86_64-linux-gnu | 1752 | 1023 | 257 | 0 | 472 | 68.43% |
| torture-execute | O1 | arm64-linux | 1752 | 1025 | 257 | 0 | 470 | 68.56% |
| torture-execute | O1 | x86_64-linux-gnu | 1752 | 1025 | 257 | 0 | 470 | 68.56% |
| torture-execute | O2 | arm64-linux | 1752 | 1025 | 257 | 0 | 470 | 68.56% |
| torture-execute | O2 | x86_64-linux-gnu | 1752 | 1025 | 257 | 0 | 470 | 68.56% |
| torture-execute | O3 | arm64-linux | 1752 | 1025 | 257 | 0 | 470 | 68.56% |
| torture-execute | O3 | x86_64-linux-gnu | 1752 | 1025 | 257 | 0 | 470 | 68.56% |
| torture-execute | Os | arm64-linux | 1752 | 1025 | 257 | 0 | 470 | 68.56% |
| torture-execute | Os | x86_64-linux-gnu | 1752 | 1025 | 257 | 0 | 470 | 68.56% |
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
| gcc-builtin | 4740 | `wontfix-0.1.0` |
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

- Count: 4120
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `70f8ab7d8e3ce9b973cfeb05b4e407e6efc4b75594aa1ae13705eadb9e57c152`
- Exemplars: torture-compile/20021001-1.c@O0@arm64-linux, torture-compile/20021001-1.c@O0@x86_64-linux-gnu, torture-compile/20021001-1.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: <id> is not a builtin this compiler implements
- Labels: pretriaged=4120/4120
- Tags: -
- Optdiv members: 0 of 4120
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

- Count: 390
- Cluster: signal=`-`; phase=`sema`
- Fingerprint: `b28fda1f26cb0a1089e244af93736842513e4362d7553c1141717119a70cc3d9`
- Exemplars: ctestsuite/00216.c@O0@arm64-linux, ctestsuite/00216.c@O0@x86_64-linux-gnu, ctestsuite/00216.c@O1@arm64-linux
- Diagnostic: <source>:<loc>: error: a struct or union must have at least one named member; the GNU no-named-member extension is not supported (docs<path>
- Labels: -
- Tags: needs-cpp, needs-libc, portable
- Optdiv members: 0 of 390
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

### Bucket 8

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

### Bucket 9

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

### Bucket 10

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

### Bucket 11

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

### Bucket 12

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

### Bucket 13

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

### Bucket 14

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

### Bucket 15

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

### Bucket 16

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

### Bucket 17

- Count: 25
- Cluster: signal=`-`; phase=`cg`
- Fingerprint: `c0cda5231420c23bdfdec54c0367f9cfb03120dae294b0351b7a16294bac87ac`
- Exemplars: torture-compile/limits-blockid.c@O0@arm64-linux, torture-compile/limits-blockid.c@O0@x86_64-linux-gnu, torture-compile/limits-blockid.c@O1@arm64-linux
- Diagnostic: timeout: sending signal TERM to command 'build<path>
- Labels: -
- Tags: -
- Optdiv members: 0 of 25
- Optdiv exemplars: -
- Hypothesis: Large macro-expanded or optimization-heavy cases exceed the compiler timeout, exposing frontend or code-generation scalability limits.
- Disposition: `fix-sprint:s56.5-compiler-scalability-timeout`

### Bucket 18

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

### Bucket 19

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

### Bucket 20

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

### Bucket 21

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

### Bucket 22

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

### Bucket 23

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

### Bucket 24

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

### Bucket 25

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

### Bucket 26

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

### Bucket 27

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

### Bucket 28

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

### Bucket 29

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

### Bucket 30

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

### Bucket 31

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

### Bucket 32

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

### Bucket 33

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

### Bucket 34

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

### Bucket 35

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

### Bucket 36

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

### Bucket 37

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

### Bucket 38

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

### Bucket 39

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

### Bucket 40

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

### Bucket 41

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

### Bucket 42

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

### Bucket 43

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

### Bucket 44

- Count: 10
- Cluster: signal=`6`; phase=`run`
- Runtime split: `non-optdiv`
- Fingerprint: `225de8a38f9b3f9c763137df7509cbf7d11311902b2357d389e99f4820d1bd3d`
- Exemplars: torture-execute/bitfld-5.c@O0@arm64-linux, torture-execute/bitfld-5.c@O0@x86_64-linux-gnu, torture-execute/bitfld-5.c@O1@arm64-linux
- Diagnostic: program killed by signal 6
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: bitfld-5 isolates arithmetic width preservation for a 40-bit unsigned long long bit-field passed through a struct argument.
- Disposition: `fix-sprint:s56.5-bitfield-expression-precision`

### Bucket 45

- Count: 10
- Cluster: signal=`6`; phase=`run`
- Runtime split: `non-optdiv`
- Fingerprint: `27f6214b615bb83b6c752664d40c40eecac1962ae4b19b683f2cd41405ed506b`
- Exemplars: torture-execute/20070919-1.c@O0@arm64-linux, torture-execute/20070919-1.c@O0@x86_64-linux-gnu, torture-execute/20070919-1.c@O1@arm64-linux
- Diagnostic: program killed by signal 6
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: 20070919-1 isolates assignment and statement-expression copying of variably sized record objects.
- Disposition: `fix-sprint:s56.5-vla-record-copy`

### Bucket 46

- Count: 10
- Cluster: signal=`6`; phase=`run`
- Runtime split: `non-optdiv`
- Fingerprint: `311376d049ccad9776443a19bcedcfe3bbdf3ec1a105fb033844257ddc3bcdc3`
- Exemplars: torture-execute/bf-sign-2.c@O0@arm64-linux, torture-execute/bf-sign-2.c@O0@x86_64-linux-gnu, torture-execute/bf-sign-2.c@O1@arm64-linux
- Diagnostic: program killed by signal 6
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: bf-sign-2 isolates integer-promotion signedness for narrow signed and unsigned bit-fields with int, long, and long long bases.
- Disposition: `fix-sprint:s56.5-bitfield-integer-promotions`

### Bucket 47

- Count: 10
- Cluster: signal=`6`; phase=`run`
- Runtime split: `non-optdiv`
- Fingerprint: `346234650213ec78ebaf4143f02fdefbe838316d0cc5332af84cba18a39e4566`
- Exemplars: torture-execute/widechar-1.c@O0@arm64-linux, torture-execute/widechar-1.c@O0@x86_64-linux-gnu, torture-execute/widechar-1.c@O1@arm64-linux
- Diagnostic: program killed by signal 6
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: widechar-1 isolates preprocessing-constant evaluation of a nonzero wide character escape.
- Disposition: `fix-sprint:s56.5-wide-character-pp-constant`

### Bucket 48

- Count: 10
- Cluster: signal=`6`; phase=`run`
- Runtime split: `non-optdiv`
- Fingerprint: `3e9eddc56601aa9900461d4031da53e09cab766350f0579c6d6ee5f9e6ba9864`
- Exemplars: torture-execute/bitfld-3.c@O0@arm64-linux, torture-execute/bitfld-3.c@O0@x86_64-linux-gnu, torture-execute/bitfld-3.c@O1@arm64-linux
- Diagnostic: program killed by signal 6
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: bitfld-3 isolates arithmetic results reduced to the declared precision of 33-, 40-, and 41-bit fields.
- Disposition: `fix-sprint:s56.5-bitfield-expression-precision`

### Bucket 49

- Count: 10
- Cluster: signal=`6`; phase=`run`
- Runtime split: `non-optdiv`
- Fingerprint: `6d8f6f38e92475641cc4bc51667d5e28ac38e04bf26fccaf67cff48d18e37bab`
- Exemplars: torture-execute/pr32244-1.c@O0@arm64-linux, torture-execute/pr32244-1.c@O0@x86_64-linux-gnu, torture-execute/pr32244-1.c@O1@arm64-linux
- Diagnostic: program killed by signal 6
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: pr32244-1 isolates left-shift evaluation in the declared 40-bit precision of a bit-field.
- Disposition: `fix-sprint:s56.5-bitfield-expression-precision`

### Bucket 50

- Count: 10
- Cluster: signal=`6`; phase=`run`
- Runtime split: `non-optdiv`
- Fingerprint: `72f0a8de235b4f864b7b2ee1fd51d172a9f8ec0058658b63c14e2706a133e4aa`
- Exemplars: torture-execute/20040411-1.c@O0@arm64-linux, torture-execute/20040411-1.c@O0@x86_64-linux-gnu, torture-execute/20040411-1.c@O1@arm64-linux
- Diagnostic: program killed by signal 6
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: 20040411-1 isolates runtime sizeof evaluation for a variably modified typedef across control-flow paths.
- Disposition: `fix-sprint:s56.5-vla-typedef-size`

### Bucket 51

- Count: 10
- Cluster: signal=`6`; phase=`run`
- Runtime split: `non-optdiv`
- Fingerprint: `75360aa5f7d08e94f983fd6e1b15420e1eff8fad4c953b0373152ab1ed06c617`
- Exemplars: torture-execute/pr34971.c@O0@arm64-linux, torture-execute/pr34971.c@O0@x86_64-linux-gnu, torture-execute/pr34971.c@O1@arm64-linux
- Diagnostic: program killed by signal 6
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: pr34971 isolates rotate-shaped shifts and addition in the declared 40-bit precision of a bit-field.
- Disposition: `fix-sprint:s56.5-bitfield-expression-precision`

### Bucket 52

- Count: 10
- Cluster: signal=`6`; phase=`run`
- Runtime split: `non-optdiv`
- Fingerprint: `8b3f9d0d72e1a1de3dd96a992ca40886226c67a56932534e4734de627118a500`
- Exemplars: torture-execute/20021127-1.c@O0@arm64-linux, torture-execute/20021127-1.c@O0@x86_64-linux-gnu, torture-execute/20021127-1.c@O1@arm64-linux
- Diagnostic: program killed by signal 6
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: 20021127-1 relies on hosted llabs builtin semantics taking precedence over its deliberately aborting fallback definition.
- Disposition: `fix-sprint:s56.5-builtin-llabs-semantics`

### Bucket 53

- Count: 10
- Cluster: signal=`6`; phase=`run`
- Runtime split: `non-optdiv`
- Fingerprint: `a6031ed7b03bf2189b2ec3061d25a5c42f489fa0494a72a8fe118cf63ceba79f`
- Exemplars: torture-execute/990130-1.c@O0@arm64-linux, torture-execute/990130-1.c@O0@x86_64-linux-gnu, torture-execute/990130-1.c@O1@arm64-linux
- Diagnostic: program killed by signal 6
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: 990130-1 isolates single evaluation of an lvalue expression used by a read-write inline-asm operand.
- Disposition: `fix-sprint:s56.5-asm-rmw-single-evaluation`

### Bucket 54

- Count: 10
- Cluster: signal=`6`; phase=`run`
- Runtime split: `non-optdiv`
- Fingerprint: `cd599d475f370373d0de4712e0e04cb8e741fee01e727d413a17a2682b7beed7`
- Exemplars: torture-execute/20020412-1.c@O0@arm64-linux, torture-execute/20020412-1.c@O0@x86_64-linux-gnu, torture-execute/20020412-1.c@O1@arm64-linux
- Diagnostic: program killed by signal 6
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: 20020412-1 isolates va_arg copying of variably sized aggregate arguments.
- Disposition: `fix-sprint:s56.5-vla-va-arg`

### Bucket 55

- Count: 10
- Cluster: signal=`6`; phase=`run`
- Runtime split: `non-optdiv`
- Fingerprint: `dcf802f4f92750b79fc1252c757dc4442ad5f96bafe609dda373b17d7fa24013`
- Exemplars: torture-execute/991014-1.c@O0@arm64-linux, torture-execute/991014-1.c@O0@x86_64-linux-gnu, torture-execute/991014-1.c@O1@arm64-linux
- Diagnostic: program killed by signal 6
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: 991014-1 isolates overflow-safe size and member-offset computation for enormous complete record and union types.
- Disposition: `fix-sprint:s56.5-huge-object-layout`

### Bucket 56

- Count: 10
- Cluster: signal=`6`; phase=`run`
- Runtime split: `non-optdiv`
- Fingerprint: `df844223f1b05ef026cd21566d47c96d9b0563e486b4439ec45034b351067ab6`
- Exemplars: torture-execute/20041218-2.c@O0@arm64-linux, torture-execute/20041218-2.c@O0@x86_64-linux-gnu, torture-execute/20041218-2.c@O1@arm64-linux
- Diagnostic: program killed by signal 6
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: 20041218-2 isolates packed record layout whose array member bound is captured from a runtime value before that value changes.
- Disposition: `fix-sprint:s56.5-packed-vla-record-layout`

### Bucket 57

- Count: 10
- Cluster: signal=`6`; phase=`run`
- Runtime split: `non-optdiv`
- Fingerprint: `eafbd9ee6d2047e6733c9c4389049f61e8421fd953404f6c163d1c20c3b9f119`
- Exemplars: torture-execute/bitfld-1.c@O0@arm64-linux, torture-execute/bitfld-1.c@O0@x86_64-linux-gnu, torture-execute/bitfld-1.c@O1@arm64-linux
- Diagnostic: program killed by signal 6
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: bitfld-1 isolates the difference between ordinary integer promotion of a narrow unsigned bit-field and an explicit full-width unsigned cast.
- Disposition: `fix-sprint:s56.5-bitfield-integer-promotions`

### Bucket 58

- Count: 10
- Cluster: signal=`6`; phase=`run`
- Runtime split: `non-optdiv`
- Fingerprint: `f6d3c6c3983a5af0fec102c0ffaab51cd2e135250114d440c72fe4bd8854da47`
- Exemplars: torture-execute/20050215-1.c@O0@arm64-linux, torture-execute/20050215-1.c@O0@x86_64-linux-gnu, torture-execute/20050215-1.c@O1@arm64-linux
- Diagnostic: program killed by signal 6
- Labels: -
- Tags: -
- Optdiv members: 0 of 10
- Optdiv exemplars: -
- Hypothesis: 20050215-1 isolates propagation of an ELF aligned attribute from a typedef to an emitted object.
- Disposition: `fix-sprint:s56.5-aligned-typedef-object-layout`

### Bucket 59

- Count: 5
- Cluster: signal=`-`; phase=`cg`
- Fingerprint: `-`
- Exemplars: torture-compile/limits-exprparen.c@O0@arm64-linux, torture-compile/limits-exprparen.c@O1@arm64-linux, torture-compile/limits-exprparen.c@O2@arm64-linux
- Diagnostic: output limit exceeded
- Labels: -
- Tags: -
- Optdiv members: 0 of 5
- Optdiv exemplars: -
- Hypothesis: The ten-thousand-level expression nesting stress exceeds the supported translation limit and now reaches the harness output guard before a bounded diagnostic is retained.
- Disposition: `out-of-scope`

### Bucket 60

- Count: 5
- Cluster: signal=`-`; phase=`ICE`
- Fingerprint: `31c786bdddd648d22e6d6b264d944e323874292e54902d0803498fc8451e2704`
- Exemplars: torture-compile/20031023-4.c@O0@arm64-linux, torture-compile/20031023-4.c@O1@arm64-linux, torture-compile/20031023-4.c@O2@arm64-linux
- Diagnostic: <path>:<loc>: warning: <id> attribute directive ignored [-Wattributes]
- Labels: -
- Tags: -
- Optdiv members: 0 of 5
- Optdiv exemplars: -
- Hypothesis: The valid greater-than-2-GiB automatic frame reaches arm64 regalloc, whose frame-object accounting ICEs above 2 GiB; retained attribute warnings supply the current normalized fingerprint.
- Disposition: `fix-sprint:s56.5-arm64-large-stack-frame`

### Bucket 61

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

### Bucket 62

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

### Bucket 63

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

### Bucket 64

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

### Bucket 65

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

### Bucket 66

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

### Bucket 67

- Count: 5
- Cluster: signal=`-`; phase=`parse`
- Fingerprint: `e52eb5707422df12f2923cc404dd36f80c00c96e1901f9885f67a72595134e73`
- Exemplars: torture-compile/limits-exprparen.c@O0@x86_64-linux-gnu, torture-compile/limits-exprparen.c@O1@x86_64-linux-gnu, torture-compile/limits-exprparen.c@O2@x86_64-linux-gnu
- Diagnostic: <source>:<loc>: error: bracket nesting exceeds 256
- Labels: -
- Tags: -
- Optdiv members: 0 of 5
- Optdiv exemplars: -
- Hypothesis: The ten-thousand-level expression nesting stress reaches Cgfried's explicit bracket-nesting translation limit on hosts where parsing stops before the harness output guard.
- Disposition: `out-of-scope`

### Bucket 68

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

### Bucket 69

- Count: 5
- Cluster: signal=`11`; phase=`cg`
- Fingerprint: `de25f493e2a030af329f5f01121c9f9249da8508936bfa0a119d0a5c8f638731`
- Exemplars: torture-compile/limits-caselabels.c@O0@arm64-linux, torture-compile/limits-caselabels.c@O1@arm64-linux, torture-compile/limits-caselabels.c@O2@arm64-linux
- Diagnostic: timeout: the monitored command dumped core
- Labels: -
- Tags: -
- Optdiv members: 0 of 5
- Optdiv exemplars: -
- Hypothesis: The 100,000-case-label stress drives arm64 code generation beyond the bounded compiler budget; the hosted native process surfaces the failure as SIGSEGV while the local cross-target reproduction exhausts its timeout.
- Disposition: `fix-sprint:s56.5-arm64-large-switch-scalability`

### Bucket 70

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

- Applied decisions: 61
- Stale decisions: 0

## Coverage

- Failed cells: 7285
- Bucketed cells: 7285
- Unbucketed cells: 0
- Unresolved buckets: 0
- Bucket coverage: 100.00%
- Misc bucket share: 0.00% (no misc bucket is emitted)
- Phase-end target: at least 95% of applicable execute cells passing.
