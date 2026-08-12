# Torture XFAIL Debt Ledger

Stable `TORT-NNN` entries live here whenever an imported torture manifest uses
an `xfail:TORT-NNN` disposition.  Each entry must name the affected manifest
cells, the failure class, its owner, and the condition for retiring it.

There are no active torture XFAIL entries in the Sprint 56 baseline.  Known
inapplicable cases are explicit `skip` rows, while applicable failures remain
classified failures in the generated triage report and pass-list ratchet.
