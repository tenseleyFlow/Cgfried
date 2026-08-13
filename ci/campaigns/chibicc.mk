# Sprint 57 chibicc descriptor.

include ci/campaigns/common.mk

CHIBICC_REF := 90d1f7f199cc55b13c7fdb5839d1409806633fdb
CHIBICC_SRC := .docs/refs/chibicc
CGF_CAMPAIGN_CHIBICC_SOURCE ?= $(CHIBICC_SRC)
CGF_CAMPAIGN_CHIBICC_WORK ?= $(CGF_CAMPAIGN_BUILD)/chibicc
CGF_CAMPAIGN_CHIBICC_EXPECTED ?= ci/campaigns/chibicc.expected
CGF_CAMPAIGN_CHIBICC_ACTUAL ?= $(CGF_CAMPAIGN_CHIBICC_WORK)/results.txt
CGF_CAMPAIGN_CHIBICC_RUNNER ?= scripts/campaigns/chibicc.sh
CGF_CAMPAIGN_CHIBICC_PRODUCER ?= campaign-chibicc-run

.PHONY: campaign-chibicc campaign-chibicc-run campaign-chibicc-gate campaign-chibicc-verify-ref
campaign-chibicc: campaign-chibicc-gate

campaign-chibicc-run: campaign-chibicc-verify-ref build/cgfried
	CGF_CAMPAIGN_CHIBICC_SOURCE="$(CGF_CAMPAIGN_CHIBICC_SOURCE)" \
	CGF_CAMPAIGN_CHIBICC_WORK="$(abspath $(CGF_CAMPAIGN_CHIBICC_WORK))" \
	CGF_CAMPAIGN_CHIBICC_CGF="$(abspath build/cgfried)" \
		$(CGF_CAMPAIGN_CHIBICC_RUNNER)

campaign-chibicc-verify-ref:
	@set -eu; \
	got=$$(git -C "$(CGF_CAMPAIGN_CHIBICC_SOURCE)" rev-parse --verify HEAD 2>/dev/null) || { \
		echo 'chibicc ref checkout missing or invalid: $(CGF_CAMPAIGN_CHIBICC_SOURCE)' >&2; exit 1; \
	}; \
	test "$$got" = "$(CHIBICC_REF)" || { \
		echo "chibicc ref mismatch: expected $(CHIBICC_REF), got $$got" >&2; exit 1; \
	}

campaign-chibicc-gate: $(CGF_CAMPAIGN_CHIBICC_PRODUCER) campaign-chibicc-verify-ref
	$(CGF_CAMPAIGN_CHECK) "$(CGF_CAMPAIGN_CHIBICC_EXPECTED)" \
		"$(CGF_CAMPAIGN_CHIBICC_ACTUAL)"

.PHONY: chibicc-configure chibicc-build chibicc-validate chibicc-expected
chibicc-configure: campaign-chibicc-verify-ref
chibicc-build: chibicc-configure $(CGF_CAMPAIGN_CHIBICC_PRODUCER)
chibicc-validate: chibicc-build
chibicc-expected: chibicc-validate
	scripts/campaign-check.sh "$(CGF_CAMPAIGN_CHIBICC_EXPECTED)" \
		"$(CGF_CAMPAIGN_CHIBICC_ACTUAL)"
