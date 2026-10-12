# Eighth post-v0.1.0 large-FOSS campaign: checksum-pinned Git is built as a
# complete portable-C closure and runs its C unit and shell test suites against
# a separate host-compiler build of the same official release archive.

include ci/campaigns/common.mk

GIT_REF := a018953688f1b10bddf91bff8747068f5f4746a4
GIT_VERSION := 2.56.0
GIT_SHA256 := 26c56c296b38c0695b26fa95f475f1d01704d2d38e73465ca30b0b2f5dc789d3
GIT_SRC := fetch:https://www.kernel.org/pub/software/scm/git/git-$(GIT_VERSION).tar.xz
GIT_URL := $(patsubst fetch:%,%,$(GIT_SRC))
CGF ?= build/cgfried
CGF_CAMPAIGN_GIT_ARCHIVE ?= $(CGF_CAMPAIGN_BUILD)/dl/git-$(GIT_VERSION).tar.xz
CGF_CAMPAIGN_GIT_WORK ?= $(CGF_CAMPAIGN_BUILD)/git
CGF_CAMPAIGN_GIT_EXPECTED ?= ci/campaigns/git.expected
CGF_CAMPAIGN_GIT_ACTUAL ?= $(CGF_CAMPAIGN_GIT_WORK)/results.txt
CGF_CAMPAIGN_GIT_RUNNER ?= scripts/campaigns/git.sh
CGF_CAMPAIGN_GIT_CHECK ?= scripts/campaign-check.sh
CGF_CAMPAIGN_GIT_PRODUCER ?= git-validate

.PHONY: campaign-git campaign-git-fetch campaign-git-verify-source \
	git-configure git-build git-validate git-expected

campaign-git: git-expected

campaign-git-fetch:
	@set -eu; \
	mkdir -p "$(dir $(CGF_CAMPAIGN_GIT_ARCHIVE))"; \
	if test -f "$(CGF_CAMPAIGN_GIT_ARCHIVE)" && \
	   printf '%s  %s\n' "$(GIT_SHA256)" "$(CGF_CAMPAIGN_GIT_ARCHIVE)" | \
	       sha256sum -c - >/dev/null 2>&1; then \
		:; \
	else \
		test "$${CGF_CAMPAIGN_OFFLINE:-0}" != 1 || { \
			echo 'Git archive is absent or invalid in the offline cache: $(CGF_CAMPAIGN_GIT_ARCHIVE)' >&2; \
			exit 1; \
		}; \
		tmp="$(CGF_CAMPAIGN_GIT_ARCHIVE).tmp.$$$$"; \
		trap 'rm -f "$$tmp"' EXIT HUP INT TERM; \
		if command -v curl >/dev/null 2>&1; then \
			curl -fL --retry 3 -o "$$tmp" "$(GIT_URL)"; \
		elif command -v wget >/dev/null 2>&1; then \
			wget -O "$$tmp" "$(GIT_URL)"; \
		else \
			echo 'Git fetch needs curl or wget' >&2; exit 1; \
		fi; \
		printf '%s  %s\n' "$(GIT_SHA256)" "$$tmp" | sha256sum -c -; \
		mv "$$tmp" "$(CGF_CAMPAIGN_GIT_ARCHIVE)"; \
		trap - EXIT HUP INT TERM; \
	fi

campaign-git-verify-source: campaign-git-fetch
	@set -eu; \
	got=$$(sha256sum "$(CGF_CAMPAIGN_GIT_ARCHIVE)" | awk '{print $$1}'); \
	test "$$got" = "$(GIT_SHA256)" || { \
		echo "Git archive checksum mismatch: expected $(GIT_SHA256), got $$got" >&2; \
		exit 1; \
	}

git-configure: campaign-git-verify-source build/cgfried rt
	CGF_CAMPAIGN_GIT_ARCHIVE="$(abspath $(CGF_CAMPAIGN_GIT_ARCHIVE))" \
	CGF_CAMPAIGN_GIT_WORK="$(abspath $(CGF_CAMPAIGN_GIT_WORK))" \
	CGF_CAMPAIGN_GIT_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_GIT_RUNNER) configure

git-build: git-configure
	CGF_CAMPAIGN_GIT_ARCHIVE="$(abspath $(CGF_CAMPAIGN_GIT_ARCHIVE))" \
	CGF_CAMPAIGN_GIT_WORK="$(abspath $(CGF_CAMPAIGN_GIT_WORK))" \
	CGF_CAMPAIGN_GIT_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_GIT_RUNNER) build

git-validate: git-build
	CGF_CAMPAIGN_GIT_ARCHIVE="$(abspath $(CGF_CAMPAIGN_GIT_ARCHIVE))" \
	CGF_CAMPAIGN_GIT_WORK="$(abspath $(CGF_CAMPAIGN_GIT_WORK))" \
	CGF_CAMPAIGN_GIT_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_GIT_RUNNER) validate

git-expected: $(CGF_CAMPAIGN_GIT_PRODUCER)
	$(CGF_CAMPAIGN_GIT_CHECK) "$(CGF_CAMPAIGN_GIT_EXPECTED)" \
		"$(CGF_CAMPAIGN_GIT_ACTUAL)"
