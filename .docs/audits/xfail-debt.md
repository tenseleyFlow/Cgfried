# XFAIL debt ledger

Every `// XFAIL(<selector>):` directive in a test source must cite an
`XF-NNNN` id from the table below; the test runner treats an unknown or
missing id as a configuration error.

Rules:

- IDs are never reused.
- Closing an XFAIL flips its status to `closed (YYYY-MM-DD)` — rows are never
  deleted. The ledger is history, not just state.
- An open entry names the sprint that owns fixing it. "Nobody's sprint" is
  not a status.
- XPASS (an XFAIL that unexpectedly passes) is a hard test failure: fixing
  the bug forces the annotation off and the row closed, loudly.

| ID | Owning sprint | Description | Status |
|---|---|---|---|
