Minimized reproducers written here by `build/fuzz_frontend` when an
invariant fails. `scripts/check_fuzz_crashes.sh` fails the build while any
`.c` file is present, so a finding cannot be forgotten: fix it, add a
permanent fixture under `tests/programs/`, then delete the reproducer.
