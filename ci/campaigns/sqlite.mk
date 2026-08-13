# Sprint 59 SQLite descriptor. Required validation is offline after the
# explicit, hash-verified fetch target populates build/campaigns/dl.

include ci/campaigns/common.mk

SQLITE_REF := 3.46.1
SQLITE_VERSION := 3460100
SQLITE_AMALGAMATION_SHA256 := 77823cb110929c2bcb0f5d48e4833b5c59a8a6e40cdea3936b99e199dbbe5784
SQLITE_SOURCE_SHA256 := def3fc292eb9ecc444f6c1950e5c79d8462ed5e7b3d605fd6152d145e1d5abb4
SQLITE_SRC := fetch:https://www.sqlite.org/2024/sqlite-amalgamation-$(SQLITE_VERSION).zip
CGF_CAMPAIGN_SQLITE_WORK ?= $(CGF_CAMPAIGN_BUILD)/sqlite
CGF_CAMPAIGN_SQLITE_CACHE ?= $(CGF_CAMPAIGN_BUILD)/dl
CGF_CAMPAIGN_SQLITE_EXPECTED ?= ci/campaigns/sqlite.expected
CGF_CAMPAIGN_SQLITE_ACTUAL ?= $(CGF_CAMPAIGN_SQLITE_WORK)/results.txt
CGF_CAMPAIGN_SQLITE_RUNNER ?= scripts/campaigns/sqlite.sh
CGF_CAMPAIGN_SQLITE_CHECK ?= scripts/campaign-check.sh
CGF_CAMPAIGN_SQLITE_PRODUCER ?= sqlite-validate

.PHONY: campaign-sqlite campaign-sqlite-fetch campaign-sqlite-run campaign-sqlite-gate campaign-sqlite-meta \
	sqlite-configure sqlite-build sqlite-validate sqlite-baseline-closure sqlite-expected
campaign-sqlite: sqlite-expected

campaign-sqlite-fetch:
	CGF_CAMPAIGN_SQLITE_CACHE="$(abspath $(CGF_CAMPAIGN_SQLITE_CACHE))" \
		$(CGF_CAMPAIGN_SQLITE_RUNNER) fetch

campaign-sqlite-run: build/cgfried build/timeit
	CGF_CAMPAIGN_SQLITE_CACHE="$(abspath $(CGF_CAMPAIGN_SQLITE_CACHE))" \
	CGF_CAMPAIGN_SQLITE_WORK="$(abspath $(CGF_CAMPAIGN_SQLITE_WORK))" \
	CGF_CAMPAIGN_SQLITE_CGF="$(abspath build/cgfried)" \
	CGF_CAMPAIGN_SQLITE_TIMEIT="$(abspath build/timeit)" \
		$(CGF_CAMPAIGN_SQLITE_RUNNER) run

campaign-sqlite-gate: campaign-sqlite-run
	$(CGF_CAMPAIGN_SQLITE_CHECK) "$(CGF_CAMPAIGN_SQLITE_EXPECTED)" \
		"$(CGF_CAMPAIGN_SQLITE_ACTUAL)"

campaign-sqlite-meta:
	scripts/campaigns/sqlite-campaign-test.sh

# The SQLite helper performs extraction, build, and validation atomically so
# these standard descriptor stages are dependency aliases as FORMAT.md allows.
sqlite-configure: campaign-sqlite-fetch build/cgfried build/timeit
	@:

sqlite-build: sqlite-configure
	CGF_CAMPAIGN_SQLITE_CACHE="$(abspath $(CGF_CAMPAIGN_SQLITE_CACHE))" \
	CGF_CAMPAIGN_SQLITE_WORK="$(abspath $(CGF_CAMPAIGN_SQLITE_WORK))" \
	CGF_CAMPAIGN_SQLITE_CGF="$(abspath build/cgfried)" \
	CGF_CAMPAIGN_SQLITE_TIMEIT="$(abspath build/timeit)" \
		$(CGF_CAMPAIGN_SQLITE_RUNNER) run

sqlite-validate: sqlite-build
	@:

sqlite-baseline-closure:
	scripts/campaigns/sqlite-policy-check.sh --require-numeric \
		ci/campaigns/sqlite-baselines.conf

sqlite-expected: $(CGF_CAMPAIGN_SQLITE_PRODUCER) sqlite-baseline-closure
	$(CGF_CAMPAIGN_SQLITE_CHECK) "$(CGF_CAMPAIGN_SQLITE_EXPECTED)" \
		"$(CGF_CAMPAIGN_SQLITE_ACTUAL)"
