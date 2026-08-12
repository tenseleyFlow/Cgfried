# Sprint 57 TinyCC descriptor.

include ci/campaigns/common.mk

TINYCC_REF := 380597704ee9784442ef3c7ef06e105258a11c5d
CGF_CAMPAIGN_TINYCC_SOURCE ?= .docs/refs/tinycc
CGF_CAMPAIGN_TINYCC_WORK ?= $(CGF_CAMPAIGN_BUILD)/tinycc
CGF_CAMPAIGN_TINYCC_EXPECTED ?= ci/campaigns/tinycc.expected
CGF_CAMPAIGN_TINYCC_ACTUAL ?= $(CGF_CAMPAIGN_TINYCC_WORK)/results.txt
CGF_CAMPAIGN_TINYCC_RUNNER ?= scripts/campaigns/tinycc.sh
CGF_CAMPAIGN_TINYCC_PRODUCER ?= campaign-tinycc-run

.PHONY: campaign-tinycc campaign-tinycc-run campaign-tinycc-gate campaign-tinycc-verify-ref
campaign-tinycc: campaign-tinycc-gate

campaign-tinycc-run: campaign-tinycc-verify-ref build/cgfried
	CGF_CAMPAIGN_TINYCC_SOURCE="$(CGF_CAMPAIGN_TINYCC_SOURCE)" \
	CGF_CAMPAIGN_TINYCC_WORK="$(abspath $(CGF_CAMPAIGN_TINYCC_WORK))" \
	CGF_CAMPAIGN_TINYCC_EXPECTED="$(CGF_CAMPAIGN_TINYCC_EXPECTED)" \
	CGF_CAMPAIGN_CHECK="$(CGF_CAMPAIGN_CHECK)" \
	CGF_CAMPAIGN_TINYCC_CGF="$(abspath build/cgfried)" \
		$(CGF_CAMPAIGN_TINYCC_RUNNER)

campaign-tinycc-verify-ref:
	@set -eu; \
	got=$$(git -C "$(CGF_CAMPAIGN_TINYCC_SOURCE)" rev-parse --verify HEAD 2>/dev/null) || { \
		echo 'tinycc ref checkout missing or invalid: $(CGF_CAMPAIGN_TINYCC_SOURCE)' >&2; exit 1; \
	}; \
	test "$$got" = "$(TINYCC_REF)" || { \
		echo "tinycc ref mismatch: expected $(TINYCC_REF), got $$got" >&2; exit 1; \
	}

campaign-tinycc-gate: $(CGF_CAMPAIGN_TINYCC_PRODUCER) campaign-tinycc-verify-ref
	$(CGF_CAMPAIGN_CHECK) "$(CGF_CAMPAIGN_TINYCC_EXPECTED)" \
		"$(CGF_CAMPAIGN_TINYCC_ACTUAL)"
