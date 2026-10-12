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

## Sole-C provenance

A campaign that publishes a `compiler.sole-c` result uses
`scripts/campaigns/sole-c.sh` as the build-tree `CC`. The wrapper binds every
receipt to the exact compiler path and SHA-256, source path and SHA-256, output
path and SHA-256, complete compiler argv, and explicit project-local link
inputs. Its closure manifest enumerates every retained C translation, archive
member, and linked product. Verification rejects missing, duplicate, or extra
translations; compiler or object substitution; stale outputs; archive members
that are not byte-identical to their certified objects; and explicit link
inputs outside the certified closure. Duplicate archive member basenames are
legal and remain distinct: verification pins the ordered name and size
sequence, then compares the complete ordered regular-member payload with the
certified object stream. Darwin's leading `__.SYMDEF` ranlib payload is removed
only when `ar tv` reports the matching prefix; GNU and LLVM archives require no
special-case bytes.

The host-compiler oracle remains a separate pristine tree and contributes no
bytes to the certified products. System assembler, linker, archive tools, CRT,
libc, runner, shell, and build tool are outside the phrase "sole C compiler";
the claim is specifically that every in-scope C translation was performed by
the designated Cgfried executable and that all explicit project-local link
inputs descend from those certified translations. Zlib is the first certified
rung; the post-v0.1.0 PCRE2 campaign is the second, with 35 project objects, 32
byte-checked archive members, and three linked test products. Mbed TLS 3.6.7 is
the third: 142 project objects, 113 byte-checked members across three static
libraries, one explicitly linked upstream self-test product, and 140 generated
suite products. Its 282 retained translations execute 13,266 generated-harness
tests in addition to the 25-suite self-test. Libpng 1.6.59 is the fourth, with
40 retained project objects, 30 byte-checked archive members across libpng and
its pinned zlib dependency, ten linked products, and 36 upstream tests.
Libjpeg-turbo 3.2.0 is the fifth certified closure and the fourth
post-v0.1.0 large-FOSS rung: 297 project objects, 230 byte-checked members
across two static libraries, 12 linked products, and 332 upstream tests.
Zstd 1.5.7 is the sixth certified closure and fifth large-FOSS rung: 81
project objects, 37 byte-checked archive members, seven linked products, and
four deterministically bounded upstream tests. Libxml2 2.15.4 is the seventh
certified closure and sixth large-FOSS rung: 52 project objects, 37
byte-checked archive members, 13 linked products, and all 22 CTest cases.
OpenSSL 3.5.9 is the eighth certified closure and seventh large-FOSS rung:
1,682 Linux C translations, 1,351 byte-checked members across eight archives,
343 linked programs, and all 347 recipe files / 4,043 Linux tests. Git 2.56.0
is the ninth certified closure and eighth large-FOSS rung: 563 C translations,
all 303 ordered `libgit.a` members (including duplicate `fsck.o` and `tree.o`
basenames), 19 linked products, 261 C unit tests, and the complete 33,235-test
shell suite on both native Linux architectures. Darwin retains 565
translations, 305 archive members, ten linked products, and its 33,203-test
platform plan. The campaign pins each platform's success and known-breakage
counts and requires the pristine host-compiler summary to match.

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
zlib, lua, sqlite, and curl.  The eight-entry publication manifest remains the
frozen v0.1.0 compile-the-world ladder. Post-v0.1.0 descriptors may be included
in the Makefile and required on pull requests without retroactively changing
that released manifest; PCRE2 10.48 is the first such large-FOSS rung and runs
on native x86-64 and ARM64 Linux. Mbed TLS 3.6.7 is the second; its
strict-C17 portable-default three-library closure and 30-suite self-test run
independently from its symmetric-only configuration, 25-suite self-test, 140
generated suite runners, 57 normal C sample/test programs, and ten standalone
one-file fuzz programs on the same two native Linux architectures. Libpng
1.6.59 is the third such large-FOSS rung; libjpeg-turbo 3.2.0 is the fourth,
zstd 1.5.7 is the fifth, libxml2 2.15.4 is the sixth, OpenSSL 3.5.9 is the
seventh, and Git 2.56.0 is the eighth. These campaigns run complete static,
sole-C project closures plus pristine host-oracle builds and their upstream
test suites on the same native architectures. Libxml2's official
release archive omits two CMake test drivers present at its pinned tag; their
exact contents are committed and hash-verified into both exported trees so all
22 registered tests execute. Curl's network-dependent full suite and larger
campaigns such as PostgreSQL and GCC remain post-v0.1.0; required bars must not
silently depend on them.
