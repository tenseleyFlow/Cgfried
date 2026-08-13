# Sprint 59 Lua descriptor.  Source acquisition is checksum-pinned and cached;
# the runner consumes only verified local archives so its execution is offline.

include ci/campaigns/common.mk

LUA_REF := 5.4.7
LUA_SRC := fetch:https://www.lua.org/ftp/lua-$(LUA_REF).tar.gz
LUA_TESTS_SRC := fetch:https://www.lua.org/tests/lua-$(LUA_REF)-tests.tar.gz
LUA_SOURCE_SHA256 := 9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
LUA_TESTS_SHA256 := 8a4898ffe4c7613c8009327a0722db7a41ef861d526c77c5b46114e59ebf811e

CGF_CAMPAIGN_LUA_CACHE ?= $(CGF_CAMPAIGN_BUILD)/dl
CGF_CAMPAIGN_LUA_ARCHIVE ?= $(CGF_CAMPAIGN_LUA_CACHE)/lua-$(LUA_REF).tar.gz
CGF_CAMPAIGN_LUA_TESTS_ARCHIVE ?= $(CGF_CAMPAIGN_LUA_CACHE)/lua-$(LUA_REF)-tests.tar.gz
CGF_CAMPAIGN_LUA_WORK ?= $(CGF_CAMPAIGN_BUILD)/lua
CGF_CAMPAIGN_LUA_EXPECTED ?= ci/campaigns/lua.expected
CGF_CAMPAIGN_LUA_ACTUAL ?= $(CGF_CAMPAIGN_LUA_WORK)/results.txt
CGF_CAMPAIGN_LUA_MUSL_WORK ?= $(CGF_CAMPAIGN_BUILD)/lua-musl
CGF_CAMPAIGN_LUA_MUSL_EXPECTED ?= ci/campaigns/lua-musl.expected
CGF_CAMPAIGN_LUA_MUSL_ACTUAL ?= $(CGF_CAMPAIGN_LUA_MUSL_WORK)/results.txt
CGF_CAMPAIGN_LUA_RUNNER ?= scripts/campaigns/lua.sh
CGF_CAMPAIGN_LUA_MUSL_SYSROOT ?= $(CGF_CAMPAIGN_BUILD)/musl/cgf-b/install

.PHONY: campaign-lua campaign-lua-fetch campaign-lua-run campaign-lua-gate \
	campaign-lua-musl-run campaign-lua-musl-gate lua-configure lua-build \
	lua-validate lua-expected lua-musl-build lua-musl-validate lua-musl-expected

campaign-lua: campaign-lua-gate

# Existing, valid cache entries are never fetched again.  A fresh download is
# installed only after its checksum matches, so interrupted downloads cannot
# poison later offline runs.
campaign-lua-fetch:
	@set -eu; \
	mkdir -p "$(CGF_CAMPAIGN_LUA_CACHE)"; \
	for spec in \
	  '$(CGF_CAMPAIGN_LUA_ARCHIVE)|$(LUA_SOURCE_SHA256)|https://www.lua.org/ftp/lua-$(LUA_REF).tar.gz' \
	  '$(CGF_CAMPAIGN_LUA_TESTS_ARCHIVE)|$(LUA_TESTS_SHA256)|https://www.lua.org/tests/lua-$(LUA_REF)-tests.tar.gz'; do \
		file=$${spec%%|*}; rest=$${spec#*|}; want=$${rest%%|*}; url=$${rest#*|}; \
		if test -f "$$file" && printf '%s  %s\n' "$$want" "$$file" | sha256sum -c - >/dev/null 2>&1; then \
			continue; \
		fi; \
		if test "$${CGF_CAMPAIGN_OFFLINE:-0}" = 1; then \
			echo "campaign-lua-fetch: offline cache miss or checksum mismatch: $$file" >&2; \
			exit 1; \
		fi; \
		tmp="$$file.tmp.$$$$"; \
		trap 'rm -f "$$tmp"' EXIT HUP INT TERM; \
		curl -fL --retry 3 --output "$$tmp" "$$url"; \
		printf '%s  %s\n' "$$want" "$$tmp" | sha256sum -c -; \
		mv "$$tmp" "$$file"; \
		trap - EXIT HUP INT TERM; \
	done

campaign-lua-run: campaign-lua-fetch build/cgfried
	CGF_CAMPAIGN_LUA_ARCHIVE="$(abspath $(CGF_CAMPAIGN_LUA_ARCHIVE))" \
	CGF_CAMPAIGN_LUA_TESTS_ARCHIVE="$(abspath $(CGF_CAMPAIGN_LUA_TESTS_ARCHIVE))" \
	CGF_CAMPAIGN_LUA_WORK="$(abspath $(CGF_CAMPAIGN_LUA_WORK))" \
	CGF_CAMPAIGN_LUA_CGF="$(abspath build/cgfried)" \
	CGF_CAMPAIGN_LUA_MODE=native \
		$(CGF_CAMPAIGN_LUA_RUNNER)

campaign-lua-gate: campaign-lua-run
	$(CGF_CAMPAIGN_CHECK) "$(CGF_CAMPAIGN_LUA_EXPECTED)" \
		"$(CGF_CAMPAIGN_LUA_ACTUAL)"

campaign-lua-musl-run: campaign-lua-fetch build/cgfried
	CGF_CAMPAIGN_LUA_ARCHIVE="$(abspath $(CGF_CAMPAIGN_LUA_ARCHIVE))" \
	CGF_CAMPAIGN_LUA_TESTS_ARCHIVE="$(abspath $(CGF_CAMPAIGN_LUA_TESTS_ARCHIVE))" \
	CGF_CAMPAIGN_LUA_WORK="$(abspath $(CGF_CAMPAIGN_LUA_MUSL_WORK))" \
	CGF_CAMPAIGN_LUA_CGF="$(abspath build/cgfried)" \
	CGF_CAMPAIGN_LUA_MUSL_SYSROOT="$(abspath $(CGF_CAMPAIGN_LUA_MUSL_SYSROOT))" \
	CGF_CAMPAIGN_LUA_MODE=musl-static \
		$(CGF_CAMPAIGN_LUA_RUNNER)

campaign-lua-musl-gate: campaign-lua-musl-run
	$(CGF_CAMPAIGN_CHECK) "$(CGF_CAMPAIGN_LUA_MUSL_EXPECTED)" \
		"$(CGF_CAMPAIGN_LUA_MUSL_ACTUAL)"

# FORMAT.md aliases: Lua has no configure script, so configure means resolving
# and verifying the two immutable upstream inputs.
lua-configure: campaign-lua-fetch
lua-build: lua-configure campaign-lua-run
lua-validate: lua-build
lua-expected: lua-validate
	scripts/campaign-check.sh "$(CGF_CAMPAIGN_LUA_EXPECTED)" \
		"$(CGF_CAMPAIGN_LUA_ACTUAL)"

lua-musl-build: campaign-lua-musl-run
lua-musl-validate: campaign-lua-musl-run
lua-musl-expected: campaign-lua-musl-run
	scripts/campaign-check.sh "$(CGF_CAMPAIGN_LUA_MUSL_EXPECTED)" \
		"$(CGF_CAMPAIGN_LUA_MUSL_ACTUAL)"
