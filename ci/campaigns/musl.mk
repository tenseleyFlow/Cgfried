# Sprint 57 musl descriptor.  The producer is supplied by the campaign runner;
# this file owns pins, artifact locations, and the exact result gate.

include ci/campaigns/common.mk

MUSL_REF := b306b16af15c89a04d8e0c55cac2dadbeb39c083
LIBC_TEST_REF := 123433158bf985d7eb3b4072e32121b9e32a1a1a
CGF_CAMPAIGN_MUSL_SOURCE ?= .docs/refs/musl
CGF_CAMPAIGN_LIBC_TEST_SOURCE ?= .docs/refs/libc-test
CGF_CAMPAIGN_MUSL_WORK ?= $(CGF_CAMPAIGN_BUILD)/musl
CGF_CAMPAIGN_MUSL_EXPECTED ?= ci/campaigns/musl.expected
CGF_CAMPAIGN_MUSL_ACTUAL ?= $(CGF_CAMPAIGN_MUSL_WORK)/results.txt
CGF_CAMPAIGN_MUSL_RUNNER ?= scripts/campaigns/musl.sh

.PHONY: campaign-musl campaign-musl-run campaign-musl-gate campaign-musl-verify-refs
campaign-musl: campaign-musl-gate

campaign-musl-run: campaign-musl-verify-refs build/cgfried
	CGF_CAMPAIGN_MUSL_SOURCE="$(CGF_CAMPAIGN_MUSL_SOURCE)" \
	CGF_CAMPAIGN_LIBC_TEST_SOURCE="$(CGF_CAMPAIGN_LIBC_TEST_SOURCE)" \
	CGF_CAMPAIGN_MUSL_WORK="$(abspath $(CGF_CAMPAIGN_MUSL_WORK))" \
	CGF_CAMPAIGN_MUSL_CGF="$(abspath build/cgfried)" \
	CGF_CAMPAIGN_MUSL_EXPECTED="$(CGF_CAMPAIGN_MUSL_EXPECTED)" \
	CGF_CAMPAIGN_CHECK="$(CGF_CAMPAIGN_CHECK)" \
		$(CGF_CAMPAIGN_MUSL_RUNNER)

campaign-musl-verify-refs:
	@set -eu; \
	for spec in '$(CGF_CAMPAIGN_MUSL_SOURCE)|$(MUSL_REF)|musl' \
	            '$(CGF_CAMPAIGN_LIBC_TEST_SOURCE)|$(LIBC_TEST_REF)|libc-test'; do \
		dir=$${spec%%|*}; rest=$${spec#*|}; \
		want=$${rest%%|*}; name=$${rest#*|}; \
		got=$$(git -C "$$dir" rev-parse --verify HEAD 2>/dev/null) || { \
			echo "$$name ref checkout missing or invalid: $$dir" >&2; exit 1; \
		}; \
		test "$$got" = "$$want" || { \
			echo "$$name ref mismatch: expected $$want, got $$got" >&2; exit 1; \
		}; \
	done

campaign-musl-gate: campaign-musl-run
	$(CGF_CAMPAIGN_CHECK) "$(CGF_CAMPAIGN_MUSL_EXPECTED)" \
		"$(CGF_CAMPAIGN_MUSL_ACTUAL)"
