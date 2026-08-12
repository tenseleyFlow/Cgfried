# c-testsuite case provenance and licenses

The imported `tests/ctestsuite` cases are not covered by c-testsuite's root
MIT license. `ORIGINS.tsv` pins the two origins declared by the cases' `.otags`
files. The accompanying license texts are byte-for-byte copies from those
exact commits, and `ARTIFACTS.sha256` pins their bytes.

The bundle also preserves c-testsuite's pinned root `LICENSE` and
`tests/LICENSE` notice as `ctestsuite-LICENSE` and
`ctestsuite-tests-LICENSE`.

`00001.c` has no `.otags` provenance. It and its sidecars are therefore
excluded from the imported corpus; `EXCLUSIONS.tsv` records that decision.
