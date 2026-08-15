# Sprint 58 bootstrap soak

This ledger records hosted CI evidence for the Sprint 58 fixed-point gate. It
does not start from a local run, a configured workflow, or an inferred result.
The clock starts only after both active x86_64 lanes and both active native
arm64 lanes have completed successfully in GitHub Actions with retained logs
and manifests.

## Gate contract

- Daily requirement: 30 consecutive distinct UTC dates on which every active
  scheduled lane is green. A missing or red required run breaks the streak.
- Weekly requirement: after the cross-host lane is truthfully activated, every
  scheduled weekly run inside the same 30-day interval must be green.
- Identity means raw byte identity with no normalization. The workflow
  artifacts must retain the run manifest and bootstrap logs.
- The final cross-host artifact must retain both hosted run manifests, both
  bootstrap reports, both consumed stage manifests, and the canonical ARM64
  header archive. Its report binds every input hash to the exact workflow
  commit.
- A row may be added only from a hosted run URL. Record the exact commit and
  artifact names so the evidence remains auditable after logs expire.

The machine-readable lane and cadence contract is `ci/bootstrap.yml`.

## Current status

**RUNNING: 3/30 distinct UTC dates green.** The ledger started on 2026-08-13.
The first two dates are complete hosted workflows in which every native lane,
the x86 reproducibility probe, and the weekly cross-host comparison passed at
one compiler-source revision per run. The third date combines a same-commit
x86 push run with the scheduled native ARM64 run; the Saturday weekly
cross-host and reproducibility lanes were not due. A missing or red required
run resets the streak.

The weekly cross-host lane is active without comparing unrelated host
toolchains. Native ARM64 archives one canonical system-header sysroot before
its fixed point, so native stage2 and the x86-hosted compiler consume the same
sources and exact header bytes. The lane imports native stage2 assembly
directly from that passed fixed point and verifies its retained hashes before
the raw ARM64 comparison. One native ARM64 runner then assembles, archives,
and links both streams with the same tools before comparing objects, the
runtime archive, and the final compiler. A separate weekly x86 probe
rebuilds with `-j1` in a different root inside the original `-j8` job and
compares against that exact parallel stage, covering both make concurrency
and path independence without changing the external toolchain. The native
ARM64 O2 job additionally executes the 4,096-vector strict-C11 int128 ABI
differential and the 1,432-line binary128 ABI/value differential against the
native GCC/libgcc oracles, using runtime objects built by native stage1.

Kasumi and Hasu also produce one controlled `*-bootstrap.txt` timing receipt
after their shorter nightly measurements. The stage2 timer is preceded
immediately by a fleet-control-v2 sample; the +30% wall-or-user+sys gate is in
trial. A missing host baseline is reported as warmup, and accepting that first
receipt remains a separate reviewed commit. Nomad does not fabricate this
metric: the native fixed-link bootstrap currently supports Linux targets.

## Verified hosted runs

| UTC date | Commit | x86 O0 | x86 O2 | arm64 O0 | arm64 O2 | weekly cross | Evidence |
|---|---|---|---|---|---|---|---|
| 2026-08-13 | `c6bf3cf6a91f50dbd561afd9c1cecd19f8a72f83` | PASS | PASS + repro PASS | PASS | PASS | PASS | [run 31665602629](https://github.com/tenseleyFlow/Cgfried/actions/runs/31665602629) |
| 2026-08-14 | `e75ff34c6b03214281ed637c7bbcb38228e76496` | PASS | PASS + repro PASS | PASS | PASS | PASS | [run 31762814206](https://github.com/tenseleyFlow/Cgfried/actions/runs/31762814206) |
| 2026-08-15 | `d9693498ef236d5088c45e3e7adb120593808eed` | PASS | PASS; repro N/A — not due | PASS | PASS | N/A — not due | [x86 run 31857187648](https://github.com/tenseleyFlow/Cgfried/actions/runs/31857187648) + [ARM run 31863013882](https://github.com/tenseleyFlow/Cgfried/actions/runs/31863013882) |

The 2026-08-13 and 2026-08-14 full-activation rows each retain
`sprint58-bootstrap-x86_64-linux-O0`,
`sprint58-bootstrap-x86_64-linux-O2`,
`sprint58-bootstrap-arm64-linux-native-O0`,
`sprint58-bootstrap-arm64-linux-native-O2`, and
`sprint58-bootstrap-arm64-cross-input`,
`sprint58-bootstrap-arm64-cross-native`,
`sprint58-bootstrap-arm64-cross-x86`, and
`sprint58-bootstrap-arm64-cross-final` for 90 days. The final cross report
records `normalization=none`, 113 assembly files, identical raw cross-host
assembly, identical same-toolchain objects/runtime archive/compiler, and
hashes for both run manifests, both bootstrap reports, both consumed stage
manifests, and the exact ARM64 header archive.

The 2026-08-15 paired evidence retains the four applicable x86/native-ARM64
lane artifacts. Weekly cross-host and x86 reproducibility artifacts are
correctly absent because their Sunday cadence was not due.

Fresh downloads of the final artifacts independently reverified all seven
embedded provenance hashes and byte-compared both 113-file assembly/object
trees, the runtime archive, and the final compiler. The 2026-08-14 audit also
matched all eight raw ZIP hashes to the GitHub artifact API, verified all eight
fixed-point stage manifests, and byte-compared the complete 228-file final
cross-host payload.

The 2026-08-15 audit matched all four raw ZIP sizes and SHA-256 digests to the
GitHub artifact API, verified all 1,356 retained run-manifest hashes and all
eight 228-entry fixed-point stage manifests, and byte-compared 912 stage1 /
stage2 payload files with zero differences. Native ARM64 O2 also records GCC,
Clang, and strict-C11 self-host int128 PASS plus 24 binary128 entry points and
1,432 result lines identical to libgcc. An independent review approved the
paired runs as the third consecutive UTC date and confirmed the weekly lanes
were not due on Saturday.

Run `31762814206` retains these raw artifact ZIP SHA-256 digests:

- `sprint58-bootstrap-x86_64-linux-O0`:
  `0a696204eea1feb4ddfa92c859a3ceb4620e621ba7aa078ba0640d9a1f501b38`
- `sprint58-bootstrap-x86_64-linux-O2`:
  `53bbbef047a17ad6af2062fe6fcb754c486b980b3493e801d7670d3d21b72f6d`
- `sprint58-bootstrap-arm64-linux-native-O0`:
  `ed2621abc6a711457f494ec578259bbda60679394db93117f95dff92cd02c119`
- `sprint58-bootstrap-arm64-linux-native-O2`:
  `35722b3be98f69592e04e8ebf2ccf12e4d1b91e4213059f314d4a8dbf2ea27a1`
- `sprint58-bootstrap-arm64-cross-input`:
  `b69fe5bed4a0e2414c59ef076c70d5bee52c53ce1cce2d1517c6e55a080483c0`
- `sprint58-bootstrap-arm64-cross-native`:
  `79e4e2ee2d24c14d98b005a7854810a7bbd3fcdb927e60d611f10f0d6efd054e`
- `sprint58-bootstrap-arm64-cross-x86`:
  `5e09f36345091f85e5a130702f92a5162443adf0f0502d701dbdd36a416258fa`
- `sprint58-bootstrap-arm64-cross-final`:
  `3192da0a8f5dd75940e2a4ce8701e17be056b6ff4c897e9449a0c37fd1de2e37`

Runs `31857187648` and `31863013882` retain these raw artifact ZIP SHA-256
digests:

- `sprint58-bootstrap-x86_64-linux-O0`:
  `75900722e4899aa3f2123350822777d54d56fba28c353e65b9e9892e67c7c362`
- `sprint58-bootstrap-x86_64-linux-O2`:
  `d8c9919573a63f088a4c4ae7d1ff2a01457849ac372d256a70f52a951558598a`
- `sprint58-bootstrap-arm64-linux-native-O0`:
  `4a279b3c186c00ee026773f06b5f100e00656d2bc7c0f57cdade3c85a8729bc2`
- `sprint58-bootstrap-arm64-linux-native-O2`:
  `c9443fa4284a4ff587fdd8579c1dda7b95d98cdcbbd63ad219c4a87d59cb204e`
