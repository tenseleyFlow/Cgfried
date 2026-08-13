# Sprint 57 QBE descriptor.

include ci/campaigns/common.mk

QBE_REF := d62b154d05de438e12e8b5e980d43ef65ea1bb6c
QBE_SRC := .docs/refs/qbe
QBE_HOST_MACHINE := $(shell uname -m)
ifneq ($(filter aarch64 arm64,$(QBE_HOST_MACHINE)),)
QBE_EXPECTED_DEFAULT := ci/campaigns/qbe-arm64.expected
else
QBE_EXPECTED_DEFAULT := ci/campaigns/qbe.expected
endif
CGF ?= build/cgfried
CGF_CAMPAIGN_QBE_SOURCE ?= $(QBE_SRC)
CGF_CAMPAIGN_QBE_WORK ?= $(CGF_CAMPAIGN_BUILD)/qbe
CGF_CAMPAIGN_QBE_EXPECTED ?= $(QBE_EXPECTED_DEFAULT)
CGF_CAMPAIGN_QBE_ACTUAL ?= $(CGF_CAMPAIGN_QBE_WORK)/results.txt
CGF_CAMPAIGN_QBE_RUNNER ?= scripts/campaigns/qbe.sh

.PHONY: campaign-qbe campaign-qbe-run campaign-qbe-gate campaign-qbe-verify-ref
campaign-qbe: campaign-qbe-gate

campaign-qbe-run: build/cgfried campaign-qbe-verify-ref
	CGF_CAMPAIGN_QBE_SOURCE="$(CGF_CAMPAIGN_QBE_SOURCE)" \
	CGF_CAMPAIGN_QBE_WORK="$(CGF_CAMPAIGN_QBE_WORK)" \
	CGF_CAMPAIGN_QBE_EXPECTED="$(CGF_CAMPAIGN_QBE_EXPECTED)" \
	CGF_CAMPAIGN_CHECK="$(CGF_CAMPAIGN_CHECK)" \
	CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_QBE_RUNNER)

campaign-qbe-verify-ref:
	@set -eu; \
	got=$$(git -C "$(CGF_CAMPAIGN_QBE_SOURCE)" rev-parse --verify HEAD 2>/dev/null) || { \
		echo 'qbe ref checkout missing or invalid: $(CGF_CAMPAIGN_QBE_SOURCE)' >&2; exit 1; \
	}; \
	test "$$got" = "$(QBE_REF)" || { \
		echo "qbe ref mismatch: expected $(QBE_REF), got $$got" >&2; exit 1; \
	}

campaign-qbe-gate: campaign-qbe-run
	$(CGF_CAMPAIGN_CHECK) "$(CGF_CAMPAIGN_QBE_EXPECTED)" \
		"$(CGF_CAMPAIGN_QBE_ACTUAL)"

.PHONY: qbe-configure qbe-build qbe-validate qbe-expected
qbe-configure: campaign-qbe-verify-ref
qbe-build: qbe-configure campaign-qbe-run
qbe-validate: qbe-build
qbe-expected: qbe-validate
	scripts/campaign-check.sh "$(CGF_CAMPAIGN_QBE_EXPECTED)" \
		"$(CGF_CAMPAIGN_QBE_ACTUAL)"
