# Shared Sprint 57 campaign result contract.  Campaign runners publish an
# actual file in the same canonical format as the committed .expected file;
# the gate deliberately compares the complete row sets in both directions.

ifndef CGF_CAMPAIGN_COMMON_INCLUDED
CGF_CAMPAIGN_COMMON_INCLUDED := 1

CGF_CAMPAIGN_BUILD ?= build/campaigns
CGF_CAMPAIGN_CHECK ?= ci/campaigns/check-expected.sh

.PHONY: campaign-expected-meta
campaign-expected-meta:
	ci/campaigns/test-expected.sh
	ci/campaigns/test-musl-toolchain.sh

endif
