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

**ARMED; NOT STARTED.** No verified hosted bootstrap evidence has been
recorded. The first pushed workflow run starts the ledger only if every
required native lane is green; the first weekly row additionally requires the
cross-host and reproducibility probes.

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
| _none_ | — | — | — | — | — | armed | No hosted evidence yet |
