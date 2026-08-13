# Campaign descriptor and result contract

Every compile-the-world campaign is described by `ci/campaigns/<project>.mk`
and ratcheted by `ci/campaigns/<project>.expected`.  Descriptors are executable
make fragments, but their public surface is deliberately uniform so local,
PR, nightly, and cross-target callers use the same stages.

## Descriptor surface

For a project named `foo-bar`, the variable prefix is `FOO_BAR` and the target
prefix remains `foo-bar`.  Every descriptor defines these two nonempty values:

```make
FOO_BAR_REF := <immutable tag or commit>
FOO_BAR_SRC := .docs/refs/foo-bar
# or: FOO_BAR_SRC := fetch:<https-url-with-a-verified-content-pin>
```

`*_REF` is the source identity checked before compilation.  A fetched archive
also records and verifies its content hash in the descriptor or fetch helper;
the download cache lives below `build/campaigns/dl`.  Required validation must
work offline from an already populated cache.

Every descriptor exposes four phony stages:

```make
foo-bar-configure:  # verify/fetch the pin, configure with CC=$(CGF), retain logs
foo-bar-build:      # build the requested compiler/target/optimization lane
foo-bar-validate:   # run the campaign's documented pass bar
foo-bar-expected:   # exact ratchet: scripts/campaign-check.sh EXPECTED ACTUAL
```

The stages form an ordered dependency chain.  A campaign whose established
runner performs configuration, build, and validation atomically may expose
those stages as dependency aliases; it must still retain the separate logs and
publish one deterministic result file.  `foo-bar-expected` is the closure gate.
All compiler invocations use the caller's `CGF` (normally `build/cgfried`), and
configuration explicitly routes `CC=$(CGF)`.

Run `scripts/campaign-lint.sh` to validate every installed descriptor and
expected file plus `ladder.yml`.  Paths may be passed to lint a bounded
descriptor set without requiring a complete in-progress ladder; `--ladder
PATH` validates a specified complete manifest.

`ladder.yml` is intentionally a dependency-free YAML subset.  It has schema
`cgf-campaign-ladder-v1` and exactly eight entries, sorted by name: chibicc,
curl, lua, musl, qbe, sqlite, tinycc, zlib.  Each entry has, in order, `name`,
`descriptor`, `expected`, `target`, `lanes`, `cadence`, and `bar`.  The first
four values derive exactly as `ci/campaigns/<name>.mk`,
`ci/campaigns/<name>.expected`, and `<name>-expected`; all referenced files and
the target must exist.  Operational metadata is a nonempty single-line scalar.

`nightly-variants.tsv` binds every published variant to four tab-separated
fields: project, variant, expected-result path, and the exact artifact producer
name.  It is sorted by variant and pins the complete 15-variant topology.  The
manifest lint also checks the workflow's static producers and native matrix;
the privileged reporter keeps artifact namespaces separate and rejects any
producer that supplies a variant it does not own.

Each reporter run retains one `campaign-ledger-evidence` artifact.
`publisher.txt` records the captured/matched/published totals, and `results/`
contains the exact metadata and result pair for all 15 variants, including
fail-closed synthetic results for missing producers. `failures/manifest.tsv`
explicitly records the failure count and names each report. The same directory
contains one deterministic Markdown report per drifting variant, so a fully
matched run has a committed `# count=0` manifest instead of relying on an empty
directory surviving artifact upload. The publisher refuses to reuse a failure-
report root, so stale or partially overwritten evidence cannot satisfy a later
nightly.

## Expected-result format

Sprint 57 established the versioned format below.  Sprint 59 keeps it rather
than introducing a second parser or a lossy colon encoding:

```text
# cgf-campaign-results-v1
# columns=key<TAB>outcome<TAB>detail
build<TAB>PASS<TAB>compiler=cgfried
test.example<TAB>PASS<TAB>cases=2
test.known-gap<TAB>SKIP<TAB>CAMP-FOO-BAR-001;case=upstream-id
```

There is exactly one row per named outcome.  Keys use lowercase letters,
digits, dots, underscores, and hyphens; outcomes are `PASS`, `FAIL`, or `SKIP`;
details are nonempty.  Rows are unique by key and sorted in `LC_ALL=C` byte
order.  `SKIP` and committed `FAIL` details start with their stable
`CAMP-<PROJECT>-NNN` finding ID.  The ID lives in `FINDINGS.md`; compiler
findings link a minimized permanent regression under the repository's existing
`tests/programs/`, `tests/corpus/`, `tests/unit/`, or focused script suites.
Published scope exclusions must instead explain the bounded omitted behavior.
The public lint, comparison, and drift-report commands export `LC_ALL=C`; lint
rejects a deviation ID from another project's namespace, an ID absent from the
ledger, or a compiler finding whose cited regression path does not exist.

The actual result uses the identical schema.  `scripts/campaign-check.sh`
compares complete row sets in both directions.  A regression, deleted result,
new result, or unrecorded improvement therefore fails.  The expected file and
finding ledger move in the same change as an intentional result update.

Output, config logs, test logs, source receipts, and measurements belong below
`build/campaigns/<project>/`; generated files never modify a reference checkout.
Result generation and ordering must not depend on filesystem enumeration,
locale, wall-clock time, or parallel completion order.

## Current ladder and deferred scope

The Sprint 57 campaigns are musl, chibicc, tinycc, and qbe.  Sprint 59 adds
zlib, lua, sqlite, and curl.  The full eight-descriptor set is the v0.1.0
compile-the-world ladder.  Curl's network-dependent full suite and larger
campaigns such as OpenSSL, PostgreSQL, and GCC remain post-v0.1.0; required
bars must not silently depend on them.
