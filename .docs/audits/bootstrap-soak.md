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

**RUNNING: 9/30 consecutive distinct UTC dates green.** The current streak
started on 2026-08-19. The first streak began on 2026-08-13 and reached 5/30
through 2026-08-17. It reset on 2026-08-18 because the required x86 O0 job was
cancelled during system-toolchain installation: bootstrap was skipped, the
evidence-manifest step failed, and no x86 O0 artifact was retained. The green
native ARM64 pair on that date cannot cure a missing required daily lane.
August 19 is therefore day 1 of the new streak, August 20 is day 2, August 21
is day 3, August 22 is day 4, August 23 is day 5, August 24 is day 6, August
25 is day 7, August 26 is day 8, and August 27 is day 9. A missing or red
required run resets the streak.

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
| 2026-08-16 | `c4b45b9c1a7e3a119ac2b963948558115285f3db` | PASS | PASS + repro PASS | PASS | PASS | PASS | [run 31926344136](https://github.com/tenseleyFlow/Cgfried/actions/runs/31926344136) |
| 2026-08-17 | `0f54ba26465023cb4187d4aabea2fb6dd89f83e7` | PASS | PASS; repro N/A — not due | PASS | PASS | N/A — not due | [x86 run 31985700744](https://github.com/tenseleyFlow/Cgfried/actions/runs/31985700744) + [ARM run 31993084787](https://github.com/tenseleyFlow/Cgfried/actions/runs/31993084787) |
| 2026-08-18 | `9ec43d92e9ad024adac6559bbcb95bc547483069` | **RESET — cancelled; bootstrap skipped; evidence manifest failed; no artifact** | PASS; repro N/A — not due | PASS | PASS | N/A — not due | [x86 run 32089117040](https://github.com/tenseleyFlow/Cgfried/actions/runs/32089117040) + [ARM run 32097369403](https://github.com/tenseleyFlow/Cgfried/actions/runs/32097369403) |
| 2026-08-19 | `af914c89ba538a89addb03366630218b6e14851c` | PASS | PASS; repro N/A — not due | PASS | PASS | N/A — not due | [x86 run 32205833254](https://github.com/tenseleyFlow/Cgfried/actions/runs/32205833254) + [ARM run 32214058949](https://github.com/tenseleyFlow/Cgfried/actions/runs/32214058949) |
| 2026-08-20 | `6460c3c2c666fab58ad6fafb9ea371755302dcff` | PASS | PASS; repro N/A — not due | PASS | PASS | N/A — not due | [x86 run 32321924998](https://github.com/tenseleyFlow/Cgfried/actions/runs/32321924998) + [ARM run 32330135984](https://github.com/tenseleyFlow/Cgfried/actions/runs/32330135984) |
| 2026-08-21 | `db6f114ac38c34d675f190485a7c9af157296c31` | PASS | PASS; repro N/A — not due | PASS | PASS | N/A — not due | [x86 run 32437193020](https://github.com/tenseleyFlow/Cgfried/actions/runs/32437193020) + [ARM run 32445410094](https://github.com/tenseleyFlow/Cgfried/actions/runs/32445410094) |
| 2026-08-22 | `9cdc3e87928b28e9485022840ca736a6739c4f7d` | PASS | PASS; repro N/A — not due | PASS | PASS | N/A — not due | [x86 run 32544159147](https://github.com/tenseleyFlow/Cgfried/actions/runs/32544159147) + [ARM run 32550348530](https://github.com/tenseleyFlow/Cgfried/actions/runs/32550348530) |
| 2026-08-23 | `b54fda679e95e7543d4125f1c850474adc6ac290` | PASS | PASS + repro PASS | PASS | PASS | PASS | [run 32617645741](https://github.com/tenseleyFlow/Cgfried/actions/runs/32617645741) |
| 2026-08-24 | `83f846e8ba4b83a58ec273fa4e2c1b6055f2a51a` | PASS | PASS; repro N/A — not due | PASS | PASS | N/A — not due | [x86 run 32686915858](https://github.com/tenseleyFlow/Cgfried/actions/runs/32686915858) + [ARM run 32688703871](https://github.com/tenseleyFlow/Cgfried/actions/runs/32688703871) |
| 2026-08-25 | `65cd492820d009ddb69b8d23ffe1213792fde209` | PASS | PASS; repro N/A — not due | PASS | PASS | N/A — not due | [x86 run 32798572708](https://github.com/tenseleyFlow/Cgfried/actions/runs/32798572708) + [ARM run 32807232448](https://github.com/tenseleyFlow/Cgfried/actions/runs/32807232448) |
| 2026-08-26 | `69113c47f5880e12cef84d40bb2242765d005888` | PASS | PASS; repro N/A — not due | PASS | PASS | N/A — not due | [x86 run 32919964767](https://github.com/tenseleyFlow/Cgfried/actions/runs/32919964767) + [ARM run 32928733136](https://github.com/tenseleyFlow/Cgfried/actions/runs/32928733136) |
| 2026-08-27 | `6ef24ca219b8c47bd72f7f2bb821463e60d42a1d` | PASS | PASS; repro N/A — not due | PASS | PASS | N/A — not due | [x86 run 33056638768](https://github.com/tenseleyFlow/Cgfried/actions/runs/33056638768) + [ARM run 33080889159](https://github.com/tenseleyFlow/Cgfried/actions/runs/33080889159) |

Manual full-lattice checkpoint [run
32603828216](https://github.com/tenseleyFlow/Cgfried/actions/runs/32603828216)
also passed on 2026-08-22 at exact head
`8fb99082a1356045aeb942766307b7899e59ce84`: x86 O0/O2, x86 O2
reproducibility, native ARM64 O0/O2 including both ABI differentials, and the
complete cross-host comparison are green. This supplemental observation does
not create a fifth distinct UTC date and does not replace the scheduled Sunday
weekly run required by the gate contract.

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

The 2026-08-16 and 2026-08-23 full activations retain the same eight-artifact
set as the August 13 and 14 full activations: the four daily artifacts listed
above,
`sprint58-bootstrap-arm64-cross-input`,
`sprint58-bootstrap-arm64-cross-native`,
`sprint58-bootstrap-arm64-cross-x86`, and
`sprint58-bootstrap-arm64-cross-final`. The August 17, 19–22, and 24–26
paired runs retain the four daily artifacts:
`sprint58-bootstrap-x86_64-linux-O0`,
`sprint58-bootstrap-x86_64-linux-O2`,
`sprint58-bootstrap-arm64-linux-native-O0`, and
`sprint58-bootstrap-arm64-linux-native-O2`. Weekly cross-host and x86
reproducibility artifacts are correctly absent on those non-Sunday dates.
The supplemental August 22 full-lattice run retains the complete eight-artifact
set without changing that cadence conclusion.

The August 18 x86 run retains `sprint58-bootstrap-x86_64-linux-O2` but no
`sprint58-bootstrap-x86_64-linux-O0` artifact. The separate ARM run retains
`sprint58-bootstrap-arm64-linux-native-O0` and
`sprint58-bootstrap-arm64-linux-native-O2`; those successful lanes do not
replace the missing required x86 O0 evidence.

This August 16–22 reconciliation uses the verified GitHub Actions run, job,
and retained-artifact metadata linked above. The Actions API reports these
SHA-256 digests for the retained artifact ZIPs:

- 2026-08-16 run `31926344136`:
  - `sprint58-bootstrap-x86_64-linux-O0`:
    `357201f5a76f7fd30231bf796b4f1c65b6a8743f8f14078fb8a6ac2e78c8fb1f`
  - `sprint58-bootstrap-x86_64-linux-O2`:
    `05c49f773f7b59d54454ad951cf5ed23308877cabfba9d7679926407c5e5c236`
  - `sprint58-bootstrap-arm64-linux-native-O0`:
    `6c66f9ddda89a47e05085dd101e52e8fd4da8760591de479a03b42cd8dc712d3`
  - `sprint58-bootstrap-arm64-linux-native-O2`:
    `e9da0ac17d3339a5df3aa18844a7aaa4851e6fa3253f7ba21aa5443b4d74fa99`
  - `sprint58-bootstrap-arm64-cross-input`:
    `30022632fc0de42e26aad02e2bfa05abd29979acb3335bfd578b24c83e3caba9`
  - `sprint58-bootstrap-arm64-cross-native`:
    `613df5e14183555798821b9f08ca2cb8cac2c2da9ee5883aa4940f310a01520a`
  - `sprint58-bootstrap-arm64-cross-x86`:
    `c924d3531de7b3f18f058990277282a0c05e6f384c57ab763a9c586c1fb9733d`
  - `sprint58-bootstrap-arm64-cross-final`:
    `bcbada3b67aa6775582e28ff41884c6952c10e5592c4eb600cbf83ce34c3733c`
- 2026-08-17 x86 run `31985700744` and ARM run `31993084787`:
  - `sprint58-bootstrap-x86_64-linux-O0`:
    `159a8bad6dd6b8286c0bb09aacac6146c95940e2d07cc90e79d77c8d5c344749`
  - `sprint58-bootstrap-x86_64-linux-O2`:
    `ac906612b7e2025ed7b3fedd01717fb6ffed18323684333cecc55479fb0414fe`
  - `sprint58-bootstrap-arm64-linux-native-O0`:
    `8aeb03c214cc214996f1cb5da71c368a7f34a632d7bad1560dcb31fe0ca51bef`
  - `sprint58-bootstrap-arm64-linux-native-O2`:
    `727e8d689f82a86b0534732af517c0550f4cadbe4785236d17ccce9b9fc201a2`
- 2026-08-18 x86 run `32089117040` and ARM run `32097369403`:
  - `sprint58-bootstrap-x86_64-linux-O2`:
    `f0869d528cbfadc0822c465f1b8025d820c8b9fbaf873f38a587610f0843f90d`
  - `sprint58-bootstrap-arm64-linux-native-O0`:
    `a03233ff87499956f698349d47f182aec67242f92e043b919a63faf046f025f7`
  - `sprint58-bootstrap-arm64-linux-native-O2`:
    `39147b7210d695dc5b1993b6992481f7b80a81bd2db28e9519d4be0472b6baf7`
- 2026-08-19 x86 run `32205833254` and ARM run `32214058949`:
  - `sprint58-bootstrap-x86_64-linux-O0`:
    `8d5637e6fd63dead5a36d744b6ca98a4e46210570e8edac7fa340402272f3497`
  - `sprint58-bootstrap-x86_64-linux-O2`:
    `798c80b5a8d5e8e12b2d50cc91f7c12680a11a8581392802349835e823797bad`
  - `sprint58-bootstrap-arm64-linux-native-O0`:
    `aee712b57e19d112e313feac8c4b51cc39d9cd2c144f223ffda90fb8554ef829`
  - `sprint58-bootstrap-arm64-linux-native-O2`:
    `a2e4fc85bc7d5208846d361156d4461cc3f91f2d89c018ff10c54db1ef1029fd`
- 2026-08-20 x86 run `32321924998` and ARM run `32330135984`:
  - `sprint58-bootstrap-x86_64-linux-O0`:
    `6a0391d507ab65e72508d1eb4a4e428c05388be6afdc5d2748236cc9ecbde961`
  - `sprint58-bootstrap-x86_64-linux-O2`:
    `af71f82a0006f72966883832bf9c859e107330da78962e6aa471a7360d4a154f`
  - `sprint58-bootstrap-arm64-linux-native-O0`:
    `9fedfcd636d65b3d194470236295ae6e57e6153f7b4ab21ca9bf90e7a4fe8ce1`
  - `sprint58-bootstrap-arm64-linux-native-O2`:
    `0a64ba7148f614374bf18d0b63e32fdb437d9b8f3042bdca6741055b1b888512`
- 2026-08-21 x86 run `32437193020` and ARM run `32445410094`:
  - `sprint58-bootstrap-x86_64-linux-O0`:
    `3b8e8b6b5be4cbc51ad2c61d5fc7e2300bfa8088eb2fbfaaf537515133b3eb0b`
  - `sprint58-bootstrap-x86_64-linux-O2`:
    `ab28c72ba91bfff5fad0f8a434e03270405141cdd8a30499cf25a0c8b6994086`
  - `sprint58-bootstrap-arm64-linux-native-O0`:
    `3666e79c0e0bb40c2a7c8c99d53e962d1db9599589fbe530c7a739219ce964a5`
  - `sprint58-bootstrap-arm64-linux-native-O2`:
    `68bbd973c92492d8761aceba394a9940c4d47ceb642d36f4c363e4c1fa365100`
- 2026-08-22 x86 run `32544159147` and ARM run `32550348530`:
  - `sprint58-bootstrap-x86_64-linux-O0`:
    `6afd36dc61d9113105d7a93bc4d3f97f891676146e440525650637fc7e497096`
  - `sprint58-bootstrap-x86_64-linux-O2`:
    `a8c747649feafc8865c686be5eb0f35dd47d1774ed21352bade0fa0c00557a74`
  - `sprint58-bootstrap-arm64-linux-native-O0`:
    `e1aca2af5d2b022f5208d53bc2d309bec07364d7dead85d98620d317ee9ae773`
  - `sprint58-bootstrap-arm64-linux-native-O2`:
    `5de22dc671aad66875cc61eff684ca259b3f78075636aba3357024ac3e8c673d`
- 2026-08-22 manual full-lattice run `32603828216`:
  - `sprint58-bootstrap-x86_64-linux-O0`:
    `80574b816ca08dc309b069da86508d856ade8f5d3e217f4bfd2de4fc6f84f06e`
  - `sprint58-bootstrap-x86_64-linux-O2`:
    `7d85e88eea2d7f6a00d52b8c9653ef7f63b0937631ca539afb8752686f214ca3`
  - `sprint58-bootstrap-arm64-linux-native-O0`:
    `d6542cc9a200a7010d79328b4da330dc9a1b383b2c0462bb0dfa50f0c85e7bc2`
  - `sprint58-bootstrap-arm64-linux-native-O2`:
    `c9888348842668674712d4ccf2fd1f46ac37c82677e84bd94c86faec91c3f66a`
  - `sprint58-bootstrap-arm64-cross-input`:
    `1e8b8140abf7883806221ab57ae89f04ce123d5ea7b5e6bbfb9b1e1b64ffc12c`
  - `sprint58-bootstrap-arm64-cross-native`:
    `00b52e7838a866a6d10cecafb5f3ae037b22ab2ff9ad7b283f813c66bc05eb81`
  - `sprint58-bootstrap-arm64-cross-x86`:
    `4fa88006597f655e9aec48a07b61795ecbac35e42a0e10b736c7c95f19c3c9b3`
  - `sprint58-bootstrap-arm64-cross-final`:
    `0cd94066a7df47979f15f7f375d54c7f0eea7ac488fe9337a9c900ce08fb8654`
- 2026-08-23 scheduled full-lattice run `32617645741`:
  - `sprint58-bootstrap-x86_64-linux-O0`:
    `2e1897b8925de890473149e5c84cd5078b61eaeeaeedbbbbeb07f17c8b022e45`
  - `sprint58-bootstrap-x86_64-linux-O2`:
    `ce547d227844b2632478276ca755c0f5e1b408a1eda71743619b602c4c50c112`
  - `sprint58-bootstrap-arm64-linux-native-O0`:
    `d78315a5ba12a5fed7ee8a4d2e6c76e55122e475801fae461f3e09eb6e18f8a2`
  - `sprint58-bootstrap-arm64-linux-native-O2`:
    `8a6367eb8010e5506a80e274b8a1a5b63f9a60efd4865f2326c1aa57f77db45a`
  - `sprint58-bootstrap-arm64-cross-input`:
    `43124e39af8c906b11a4c7a3a43fe5a49ad9140d74047500fa2c1e1b37953e0b`
  - `sprint58-bootstrap-arm64-cross-native`:
    `05b469be30a4285e4da4280a84a299f53c5a182670334016085b48bcf0b1f31e`
  - `sprint58-bootstrap-arm64-cross-x86`:
    `9dfcd4499ca3687ba2629dd5a8fc1f2cb3e3d64c585db410b2edf02a9490ffae`
  - `sprint58-bootstrap-arm64-cross-final`:
    `ecdb0b45a67626d409746df2b217f1d20e6b2747bce9042cc3ab4e8224ee1759`
- 2026-08-24 x86 run `32686915858` and ARM run `32688703871`:
  - `sprint58-bootstrap-x86_64-linux-O0`:
    `5cc9c49adef67f84d5e62332099482cf750e189f0addf44351c77cd374d90c6e`
  - `sprint58-bootstrap-x86_64-linux-O2`:
    `9150d868b02a8a8deb4d3bd8aa38c7b6ee3f22db38a355e3d2f6428bf3afbf57`
  - `sprint58-bootstrap-arm64-linux-native-O0`:
    `920d899755195deeb8cdf23ebe34e2ce3764d286546f52bc3aebb912d0843d75`
  - `sprint58-bootstrap-arm64-linux-native-O2`:
    `8c3630e7346b8d8dde920e9073dacd53772a9d9641cd034ba21aa8e8504d060f`
- 2026-08-25 x86 run `32798572708` and ARM run `32807232448`:
  - `sprint58-bootstrap-x86_64-linux-O0`:
    `f2615576137b449925cc6d6cfe95b998c8631ad119e4407a6bc6bb15f41606ce`
  - `sprint58-bootstrap-x86_64-linux-O2`:
    `fb154fee6627c34759d181697bd9b3bc4f16aa674f278a101c853a29619235d1`
  - `sprint58-bootstrap-arm64-linux-native-O0`:
    `4edad986c4b55ffcf0f57485f18c5dabbee535c4fa37b5078a2d1a48ee2631eb`
  - `sprint58-bootstrap-arm64-linux-native-O2`:
    `0995efe6d4b77aad9998563105a471d6eb9ba126a1110fcd9905ab8fd1390728`
- 2026-08-26 x86 run `32919964767` and ARM run `32928733136`:
  - `sprint58-bootstrap-x86_64-linux-O0`:
    `4b62e7f29483af28fe2a3220b0284217d1277d101a5ad665acf9a82074ed8bf7`
  - `sprint58-bootstrap-x86_64-linux-O2`:
    `9de1715bfbf07713bd50332dd7738f6eaf83ea40511b57ad13b447c6adc89800`
  - `sprint58-bootstrap-arm64-linux-native-O0`:
    `a7747843b379738589c3180c1e0bb74fee518b5642c26837c9b078a93502b753`
  - `sprint58-bootstrap-arm64-linux-native-O2`:
    `ddece12fb04d162b9daef1af133577bdc382f922d9cc987881f8fe2714d39cf8`

These are API-reported ZIP digests, not locally recomputed ZIP hashes. For the
August 16–22 scheduled evidence, this documentation pass did not download the
ZIP bytes or independently recompute the internal manifest and payload hashes.
That independent content audit remains an explicit evidence gap for those
runs; the ledger does not claim it was performed.

The supplemental manual run is independently stronger evidence: a fresh
download of its final artifact matched all seven embedded provenance hashes,
verified both 113-entry assembly manifests against the retained files, and
byte-compared all 228 corresponding assembly, object, runtime-archive, and
compiler payload files with zero differences. Its x86 run manifest records
`reproducibility_outcome=success`; its native O2 manifest records both
`int128_abi_outcome=success` and `fp128_abi_outcome=success`.

The August 23–26 reconciliation is also content-audited. Across each date's
four daily artifacts, fresh downloads verified all 1,356 run-manifest hashes,
all eight 228-entry fixed-point stage manifests, and 912 stage1/stage2 payload
comparisons with zero differences; every run manifest records the exact table
commit, `normalization=none`, and `outcome=success`. The August 23 final
cross-host artifact additionally matched all seven embedded provenance hashes,
both 113-entry assembly manifests, the canonical header archive, and all 228
paired assembly/object/archive/compiler payloads byte-for-byte. The three
intermediate cross-host ZIPs were not separately downloaded; their GitHub API
digests are recorded above, while the final artifact independently
authenticates the exact inputs the comparison consumed.

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
