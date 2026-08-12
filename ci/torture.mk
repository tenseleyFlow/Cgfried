# Sprint 56 torture-suite matrix orchestration.  This fragment is included by
# the top-level Makefile and is also intentionally usable with `make -f` by its
# infrastructure self-test.

BUILD ?= build
CGF_TORTURE_CC ?= $(BUILD)/cgfried
CGF_TORTURE_PROVENANCE_CC ?= $(CGF_TORTURE_CC)
CGF_TORTURE_PROVENANCE_RECEIPT ?= $(CGF_TORTURE_PROVENANCE_CC).provenance
CGF_TORTURE_TARGET ?=
CGF_TORTURE_LEVELS ?= O0 O1 O2 O3 Os
CGF_TORTURE_RESULTS ?=
# Optional whitespace-separated result streams used when refreshing the
# combined x86_64 + arm64 ratchet.  Empty means the result produced by this
# invocation only; stale result files are never discovered implicitly.
CGF_TORTURE_BASELINE_RESULTS ?=
CGF_TORTURE_WORK ?= $(BUILD)/torture/work
CGF_TORTURE_PASSING ?= tests/torture/passing.txt
CGF_TORTURE_TRIAGE ?= .docs/audits/torture-triage.md
CGF_TORTURE_XFAIL_LEDGER ?= tests/xfail-ledger.md
CGF_TORTURE_POLICY ?= tests/torture-policy.tsv
CGF_CTESTSUITE_POLICY ?= tests/ctestsuite-policy.tsv
CGF_TORTURE_TRIAGE_POLICY ?= tests/torture-triage-policy.tsv

CGF_TORTURE_RUNNER ?= scripts/torture-run.sh
CGF_TORTURE_PROVENANCE ?= scripts/torture-provenance.sh
CGF_TORTURE_TRIAGE_TOOL ?= scripts/triage-torture.sh
CGF_TORTURE_IMPORT ?= scripts/import-torture.sh
CGF_CTESTSUITE_IMPORT ?= scripts/import-c-testsuite.sh
CGF_TORTURE_MANIFEST ?= tests/torture/MANIFEST
CGF_CTESTSUITE_MANIFEST ?= tests/ctestsuite/MANIFEST

.PHONY: torture-import torture-run torture-gate torture-baseline \
	torture-import-verify \
	torture-import-meta torture-meta

torture-import:
	CGF_TORTURE_POLICY="$(CGF_TORTURE_POLICY)" \
		"$(CGF_TORTURE_IMPORT)"
	CGF_CTESTSUITE_POLICY="$(CGF_CTESTSUITE_POLICY)" \
		"$(CGF_CTESTSUITE_IMPORT)"

# The loop is deliberately one recipe rather than recursive make: each cell
# gets a distinct output and work directory, then a single deterministic merge
# owns CGF_TORTURE_RESULTS.
torture-run: $(CGF_TORTURE_PROVENANCE_CC) $(CGF_TORTURE_PROVENANCE_RECEIPT)
	@set -eu; \
	target='$(CGF_TORTURE_TARGET)'; \
	if [ -z "$$target" ]; then \
		target=$$("$(CGF_TORTURE_CC)" -dumpmachine); \
	fi; \
	if [ -z "$$target" ]; then \
		echo 'torture.mk: compiler -dumpmachine returned an empty target' >&2; \
		exit 1; \
	fi; \
	results='$(CGF_TORTURE_RESULTS)'; \
	if [ -z "$$results" ]; then \
		results='$(BUILD)'/torture/results-"$$target"-v2.txt; \
	fi; \
	work='$(CGF_TORTURE_WORK)'; \
	mkdir -p "$$(dirname "$$results")" "$$(dirname "$$work")"; \
	results_dir=$$(CDPATH='' cd "$$(dirname "$$results")" && pwd -P); \
	results_path=$$results_dir/$$(basename "$$results"); \
	tmp=; publish_tmp=; \
	trap 'test -z "$$tmp" || rm -rf "$$tmp"; test -z "$$publish_tmp" || rm -f "$$publish_tmp"' EXIT HUP INT TERM; \
	tmp=$$(mktemp -d "$$work.matrix.XXXXXX"); \
	"$(CGF_TORTURE_PROVENANCE)" \
		--receipt "$(CGF_TORTURE_PROVENANCE_RECEIPT)" \
		--driver "$(CGF_TORTURE_CC)" \
		--compiler "$(CGF_TORTURE_PROVENANCE_CC)" \
		--runner "$(CGF_TORTURE_RUNNER)" \
		--target "$$target" \
		--torture-manifest "$(CGF_TORTURE_MANIFEST)" \
		--ctestsuite-manifest "$(CGF_CTESTSUITE_MANIFEST)" \
		>"$$tmp/provenance.before"; \
	all_rows="$$tmp/all.rows"; \
	: >"$$all_rows"; \
	for suite in torture-compile torture-execute torture-execute-ieee ctestsuite; do \
		case $$suite in \
			ctestsuite) manifest='$(CGF_CTESTSUITE_MANIFEST)' ;; \
			*) manifest='$(CGF_TORTURE_MANIFEST)' ;; \
		esac; \
		for level in $(CGF_TORTURE_LEVELS); do \
			cell="$$tmp/$$suite.$$level"; \
			mkdir -p "$$cell/work"; \
			echo "torture: suite=$$suite level=$$level target=$$target"; \
			"$(CGF_TORTURE_RUNNER)" \
				--cc "$(CGF_TORTURE_CC)" \
				--suite "$$suite" \
				--level "$$level" \
				--target "$$target" \
				--manifest "$$manifest" \
				--output "$$cell/results" \
				--work "$$cell/work"; \
			if [ ! -f "$$cell/results" ]; then \
				echo "torture.mk: $$suite/$$level produced no results file" >&2; \
				exit 1; \
			fi; \
			first=$$(sed -n '1p' "$$cell/results"); \
			second=$$(sed -n '2p' "$$cell/results"); \
			if [ "$$first" != '# cgf-torture-results-v1' ] || \
			   [ "$$second" != '# columns=key	suite	file	level	target	outcome	signal	fingerprint	phase	detail' ]; then \
				echo "torture.mk: $$suite/$$level has an invalid v1 results header" >&2; \
				exit 1; \
			fi; \
			if sed '1,2d' "$$cell/results" | grep '^#' >/dev/null; then \
				echo "torture.mk: $$suite/$$level has an unexpected extra header" >&2; \
				exit 1; \
			fi; \
			if ! sed '1,2d' "$$cell/results" | sed '/^$$/d' | \
			   awk -v suite="$$suite" -v level="$$level" -v target="$$target" \
			       'BEGIN { FS = sprintf("%c", 9) } \
			       NF != 10 { bad = 1; next } \
			       { for (i = 1; i <= 10; i++) if ($$i == "") bad = 1 } \
			       $$2 != suite || $$4 != level || $$5 != target { bad = 1 } \
			       $$1 != $$2 "/" $$3 "@" $$4 "@" $$5 { bad = 1 } \
			       $$6 !~ /^(PASS|SKIP|XFAIL|COMPILE_FAIL|OUTPUT_FAIL|WRONG_EXIT|SIGNAL|TIMEOUT|ICE)$$/ { bad = 1 } \
			       $$9 !~ /^(pp|parse|sema|ir-verify|opt|cg|as|ld|run|ICE|policy)$$/ { bad = 1 } \
			       $$6 == "SIGNAL" && ($$7 !~ /^[0-9]+$$/ || ($$7 + 0) < 1 || \
			           ($$7 + 0) > 127 || $$7 != ($$7 + 0) "") { bad = 1 } \
			       $$6 != "SIGNAL" && $$7 != "-" { bad = 1 } \
			       END { exit bad }'; then \
				echo "torture.mk: $$suite/$$level has a malformed result row" >&2; \
				exit 1; \
			fi; \
			sed '1,2d' "$$cell/results" | sed '/^$$/d' >"$$cell/rows"; \
			cut -f 1 "$$cell/rows" | LC_ALL=C sort >"$$cell/actual.keys"; \
			if LC_ALL=C uniq -d "$$cell/actual.keys" | grep . >/dev/null; then \
				echo "torture.mk: $$suite/$$level produced duplicate result cell keys" >&2; \
				exit 1; \
			fi; \
			if [ "$$suite" = ctestsuite ]; then \
				awk -v suite="$$suite" -v level="$$level" -v target="$$target" \
				    'BEGIN { FS = sprintf("%c", 9) } $$1 == "case" { \
				        print suite "/" $$2 "@" level "@" target \
				    }' "$$manifest"; \
			else \
				case $$suite in \
					torture-compile) mode=compile; prefix=compile/ ;; \
					torture-execute) mode=run; prefix=execute/ ;; \
					torture-execute-ieee) mode=run-ieee; prefix=execute-ieee/ ;; \
				esac; \
				awk -v suite="$$suite" -v level="$$level" -v target="$$target" \
				    -v mode="$$mode" -v prefix="$$prefix" \
				    'BEGIN { FS = sprintf("%c", 9) } \
				     $$3 == mode && index($$1, prefix) == 1 { \
				        print suite "/" substr($$1, length(prefix) + 1) \
				            "@" level "@" target \
				     }' "$$manifest"; \
			fi | LC_ALL=C sort >"$$cell/expected.keys"; \
			if ! cmp -s "$$cell/expected.keys" "$$cell/actual.keys"; then \
				echo "torture.mk: $$suite/$$level result keys do not match its manifest" >&2; \
				comm -23 "$$cell/expected.keys" "$$cell/actual.keys" | \
					sed 's/^/torture.mk: missing result key: /' >&2; \
				comm -13 "$$cell/expected.keys" "$$cell/actual.keys" | \
					sed 's/^/torture.mk: unexpected result key: /' >&2; \
				exit 1; \
			fi; \
			cat "$$cell/rows" >>"$$all_rows"; \
			echo "torture: completed suite=$$suite level=$$level rows=$$(wc -l <"$$cell/rows" | tr -d ' ')"; \
		done; \
	done; \
	if [ ! -s "$$all_rows" ]; then \
		echo 'torture.mk: matrix produced no result rows' >&2; \
		exit 1; \
	fi; \
	LC_ALL=C sort "$$all_rows" >"$$tmp/sorted.rows"; \
	cut -f 1 "$$tmp/sorted.rows" | LC_ALL=C sort >"$$tmp/sorted.keys"; \
	if LC_ALL=C uniq -d "$$tmp/sorted.keys" | grep . >/dev/null; then \
		echo 'torture.mk: matrix produced duplicate result cell keys' >&2; \
		exit 1; \
	fi; \
	"$(CGF_TORTURE_PROVENANCE)" \
		--receipt "$(CGF_TORTURE_PROVENANCE_RECEIPT)" \
		--driver "$(CGF_TORTURE_CC)" \
		--compiler "$(CGF_TORTURE_PROVENANCE_CC)" \
		--runner "$(CGF_TORTURE_RUNNER)" \
		--target "$$target" \
		--torture-manifest "$(CGF_TORTURE_MANIFEST)" \
		--ctestsuite-manifest "$(CGF_CTESTSUITE_MANIFEST)" \
		>"$$tmp/provenance.after"; \
	if ! cmp -s "$$tmp/provenance.before" "$$tmp/provenance.after"; then \
		echo 'torture.mk: provenance changed while the matrix was running' >&2; \
		exit 1; \
	fi; \
	publish_tmp=$$(mktemp "$$results_dir/.$$(basename "$$results").XXXXXX"); \
	{ \
		echo '# cgf-torture-results-v2'; \
		printf '# columns=key\tsuite\tfile\tlevel\ttarget\toutcome\tsignal\tfingerprint\tphase\tdetail\n'; \
		cat "$$tmp/provenance.before"; \
		cat "$$tmp/sorted.rows"; \
	} >"$$publish_tmp"; \
	chmod 644 "$$publish_tmp"; \
	mv -f "$$publish_tmp" "$$results_path"

torture-gate: torture-import-verify torture-run
	@set -eu; \
	target='$(CGF_TORTURE_TARGET)'; \
	if [ -z "$$target" ]; then target=$$("$(CGF_TORTURE_CC)" -dumpmachine); fi; \
	results='$(CGF_TORTURE_RESULTS)'; \
	if [ -z "$$results" ]; then \
		results='$(BUILD)'/torture/results-"$$target"-v2.txt; \
	fi; \
	CGF_TORTURE_TRIAGE_POLICY="$(CGF_TORTURE_TRIAGE_POLICY)" \
	"$(CGF_TORTURE_TRIAGE_TOOL)" --gate \
		"$(CGF_TORTURE_PASSING)" "$$results"

# This is the only target that writes the baseline and triage outputs.  Normal
# runs and gates never refresh either committed artifact implicitly.  The
# baseline-results variable is intentionally a whitespace-separated list;
# repository-generated result paths contain no whitespace.
torture-baseline: torture-import-verify torture-run
	@set -eu; \
	target='$(CGF_TORTURE_TARGET)'; \
	if [ -z "$$target" ]; then target=$$("$(CGF_TORTURE_CC)" -dumpmachine); fi; \
	results='$(CGF_TORTURE_RESULTS)'; \
	if [ -z "$$results" ]; then \
		results='$(BUILD)'/torture/results-"$$target"-v2.txt; \
	fi; \
	baseline_results='$(CGF_TORTURE_BASELINE_RESULTS)'; \
	if [ -z "$$baseline_results" ]; then baseline_results=$$results; fi; \
	set -- $$baseline_results; \
	if [ -n '$(CGF_TORTURE_BASELINE_RESULTS)' ]; then \
		results_dir=$$(CDPATH='' cd "$$(dirname "$$results")" && pwd -P); \
		results_canonical=$$results_dir/$$(basename "$$results"); \
		fresh_seen=0; \
		for input do \
			input_dir=$$(CDPATH='' cd "$$(dirname "$$input")" && pwd -P) || { \
				echo "torture.mk: cannot canonicalize baseline result path: $$input" >&2; \
				exit 1; \
			}; \
			input_canonical=$$input_dir/$$(basename "$$input"); \
			if [ "$$input_canonical" = "$$results_canonical" ] || \
			   [ "$$input" -ef "$$results" ]; then fresh_seen=1; fi; \
		done; \
		if [ "$$fresh_seen" -ne 1 ]; then \
			echo "torture.mk: explicit baseline results omit freshly generated result: $$results_canonical" >&2; \
			exit 1; \
		fi; \
	fi; \
	passing='$(CGF_TORTURE_PASSING)'; \
	baseline_tmp=$$(mktemp -d "$${TMPDIR:-/tmp}/cgf-torture-baseline.XXXXXX"); \
	trap 'rm -rf "$$baseline_tmp"' EXIT HUP INT TERM; \
	: >"$$baseline_tmp/rows"; \
	: >"$$baseline_tmp/input.targets"; \
	: >"$$baseline_tmp/input.paths"; \
	for input do \
		test -f "$$input" && test -r "$$input" || { \
			echo "torture.mk: baseline result is not a readable regular file: $$input" >&2; \
			exit 1; \
		}; \
		input_dir=$$(CDPATH='' cd "$$(dirname "$$input")" && pwd -P) || { \
			echo "torture.mk: cannot canonicalize baseline result path: $$input" >&2; \
			exit 1; \
		}; \
		input=$$input_dir/$$(basename "$$input"); \
		printf '%s\n' "$$input" >>"$$baseline_tmp/input.paths"; \
		first=$$(sed -n '1p' "$$input"); \
		second=$$(sed -n '2p' "$$input"); \
		if [ "$$first" != '# cgf-torture-results-v2' ] || \
		   [ "$$second" != '# columns=key	suite	file	level	target	outcome	signal	fingerprint	phase	detail' ]; then \
			echo "torture.mk: baseline result has an invalid v2 preamble: $$input" >&2; \
			exit 1; \
		fi; \
		if ! sed -n '3,10p' "$$input" | awk \
		   'function hash_line(prefix, value) { \
		        return index(value, prefix) == 1 && length(value) == length(prefix) + 64 && \
		            substr(value, length(prefix) + 1) ~ /^[0-9a-f]+$$/ \
		    } \
		    NR == 1 { value = substr($$0, 19); good = index($$0, "# source-revision=") == 1 && \
		        (value == "unversioned" || (value ~ /^[0-9a-f]+$$/ && (length(value) == 40 || length(value) == 64))) } \
		    NR == 2 { good = good && hash_line("# compiler-source-sha256=", $$0) } \
		    NR == 3 { good = good && hash_line("# harness-sha256=", $$0) } \
		    NR == 4 { good = good && hash_line("# torture-manifest-sha256=", $$0) } \
		    NR == 5 { good = good && hash_line("# ctestsuite-manifest-sha256=", $$0) } \
		    NR == 6 { good = good && ($$0 == "# target=x86_64-linux-gnu" || $$0 == "# target=arm64-linux") } \
		    NR == 7 { good = good && hash_line("# compiler-binary-sha256=", $$0) } \
		    NR == 8 { good = good && hash_line("# compiler-driver-sha256=", $$0) } \
		    END { exit !(NR == 8 && good) }'; then \
			echo "torture.mk: baseline result has malformed provenance: $$input" >&2; \
			exit 1; \
		fi; \
		header_target=$$(sed -n '8s/^# target=//p' "$$input"); \
		if grep -F -x "$$header_target" "$$baseline_tmp/input.targets" >/dev/null; then \
			echo "torture.mk: duplicate baseline result target: $$header_target" >&2; \
			exit 1; \
		fi; \
		printf '%s\n' "$$header_target" >>"$$baseline_tmp/input.targets"; \
		if ! sed '1,10d' "$$input" | awk -v header_target="$$header_target" \
		   'BEGIN { FS = sprintf("%c", 9) } \
		    /^#/ || NF != 10 { bad = 1; next } \
		    { for (i = 1; i <= 10; i++) if ($$i == "") bad = 1 } \
		    $$2 !~ /^(torture-compile|torture-execute|torture-execute-ieee|ctestsuite)$$/ { bad = 1 } \
		    $$4 !~ /^(O0|O1|O2|O3|Os)$$/ { bad = 1 } \
		    $$5 !~ /^(x86_64-linux-gnu|arm64-linux)$$/ || $$5 != header_target { bad = 1 } \
		    $$1 != $$2 "/" $$3 "@" $$4 "@" $$5 { bad = 1 } \
		    END { exit bad }'; then \
			echo "torture.mk: baseline result has a malformed or mislabelled row: $$input" >&2; \
			exit 1; \
		fi; \
		sed '1,10d' "$$input" >>"$$baseline_tmp/rows"; \
		sed -n '3,7p' "$$input" >"$$baseline_tmp/common.current"; \
		if [ ! -f "$$baseline_tmp/common" ]; then \
			cp "$$baseline_tmp/common.current" "$$baseline_tmp/common"; \
		elif ! cmp -s "$$baseline_tmp/common" "$$baseline_tmp/common.current"; then \
			echo "torture.mk: baseline results have mismatched common provenance: $$input" >&2; \
			exit 1; \
		fi; \
	done; \
	test -s "$$baseline_tmp/rows" || { \
		echo 'torture.mk: baseline results contain no rows' >&2; exit 1; }; \
	cut -f 1 "$$baseline_tmp/rows" | LC_ALL=C sort >"$$baseline_tmp/actual.keys"; \
	if LC_ALL=C uniq -d "$$baseline_tmp/actual.keys" | grep . >/dev/null; then \
		echo 'torture.mk: baseline results contain duplicate cell keys' >&2; \
		exit 1; \
	fi; \
	LC_ALL=C sort "$$baseline_tmp/input.targets" >"$$baseline_tmp/targets"; \
	{ echo arm64-linux; echo x86_64-linux-gnu; } >"$$baseline_tmp/required.targets"; \
	if ! cmp -s "$$baseline_tmp/required.targets" "$$baseline_tmp/targets"; then \
		echo 'torture.mk: combined baseline results require exactly arm64-linux and x86_64-linux-gnu' >&2; \
		exit 1; \
	fi; \
	: >"$$baseline_tmp/expected.keys"; \
	while IFS= read -r baseline_target; do \
		for level in O0 O1 O2 O3 Os; do \
			awk -v suite=torture-compile -v level="$$level" \
			    -v target="$$baseline_target" \
			    'BEGIN { FS = sprintf("%c", 9) } $$3 == "compile" && \
			     index($$1, "compile/") == 1 { print suite "/" \
			     substr($$1, 9) "@" level "@" target }' \
			    '$(CGF_TORTURE_MANIFEST)' >>"$$baseline_tmp/expected.keys"; \
			awk -v suite=torture-execute -v level="$$level" \
			    -v target="$$baseline_target" \
			    'BEGIN { FS = sprintf("%c", 9) } $$3 == "run" && \
			     index($$1, "execute/") == 1 { print suite "/" \
			     substr($$1, 9) "@" level "@" target }' \
			    '$(CGF_TORTURE_MANIFEST)' >>"$$baseline_tmp/expected.keys"; \
			awk -v suite=torture-execute-ieee -v level="$$level" \
			    -v target="$$baseline_target" \
			    'BEGIN { FS = sprintf("%c", 9) } $$3 == "run-ieee" && \
			     index($$1, "execute-ieee/") == 1 { print suite "/" \
			     substr($$1, 14) "@" level "@" target }' \
			    '$(CGF_TORTURE_MANIFEST)' >>"$$baseline_tmp/expected.keys"; \
			awk -v suite=ctestsuite -v level="$$level" \
			    -v target="$$baseline_target" \
			    'BEGIN { FS = sprintf("%c", 9) } $$1 == "case" { print suite "/" \
			     $$2 "@" level "@" target }' '$(CGF_CTESTSUITE_MANIFEST)' \
			    >>"$$baseline_tmp/expected.keys"; \
		done; \
	done <"$$baseline_tmp/targets"; \
	LC_ALL=C sort "$$baseline_tmp/expected.keys" >"$$baseline_tmp/expected.sorted"; \
	if ! cmp -s "$$baseline_tmp/expected.sorted" "$$baseline_tmp/actual.keys"; then \
		echo 'torture.mk: baseline result keys do not match the complete five-level matrix' >&2; \
		comm -23 "$$baseline_tmp/expected.sorted" "$$baseline_tmp/actual.keys" | \
			sed 's/^/torture.mk: missing baseline key: /' >&2; \
		comm -13 "$$baseline_tmp/expected.sorted" "$$baseline_tmp/actual.keys" | \
			sed 's/^/torture.mk: unexpected baseline key: /' >&2; \
		exit 1; \
	fi; \
	passing_dir=$$(dirname "$$passing"); \
	report='$(CGF_TORTURE_TRIAGE)'; \
	report_dir=$$(dirname "$$report"); \
	mkdir -p "$$passing_dir" "$$report_dir"; \
	passing_dir=$$(CDPATH='' cd "$$passing_dir" && pwd -P); \
	report_dir=$$(CDPATH='' cd "$$report_dir" && pwd -P); \
	passing_final=$$passing_dir/$$(basename "$$passing"); \
	report_final=$$report_dir/$$(basename "$$report"); \
	[ "$$passing_final" != "$$report_final" ] || { \
		echo 'torture.mk: passing and triage destinations must differ' >&2; exit 1; }; \
	if [ -e "$$passing_final" ] && [ -e "$$report_final" ] && \
	   [ "$$passing_final" -ef "$$report_final" ]; then \
		echo 'torture.mk: passing and triage destinations must not alias' >&2; exit 1; \
	fi; \
	while IFS= read -r input_final; do \
		for destination_final in "$$passing_final" "$$report_final"; do \
			if [ "$$destination_final" = "$$input_final" ] || \
			   { [ -e "$$destination_final" ] && \
			     [ "$$destination_final" -ef "$$input_final" ]; }; then \
				echo "torture.mk: baseline output aliases result input: $$input_final" >&2; \
				exit 1; \
			fi; \
		done; \
	done <"$$baseline_tmp/input.paths"; \
	# The triage tool owns same-directory staging and rollback for this pair. \
	# The report publishes first; the passing ratchet is the commit point. \
	CGF_TORTURE_TRIAGE_POLICY="$(CGF_TORTURE_TRIAGE_POLICY)" \
	"$(CGF_TORTURE_TRIAGE_TOOL)" --output "$$report_final" \
		--emit-passing "$$passing_final" "$$@"

torture-import-verify:
	CGF_TORTURE_POLICY="$(CGF_TORTURE_POLICY)" \
		"$(CGF_TORTURE_IMPORT)" --verify
	CGF_CTESTSUITE_POLICY="$(CGF_CTESTSUITE_POLICY)" \
		"$(CGF_CTESTSUITE_IMPORT)" --verify
	@set -eu; \
	policy='$(CGF_TORTURE_TRIAGE_POLICY)'; \
	test -f "$$policy" && test -r "$$policy" || { \
		echo "torture.mk: triage policy is not a readable regular file: $$policy" >&2; \
		exit 1; \
	}; \
	if ! awk -v source="$$policy" 'BEGIN { FS = sprintf("%c", 9) } \
		NR == 1 { if ($$0 != "# cgf-torture-triage-policy-v1") bad = 1; next } \
		NR == 2 { \
			if ($$0 != "# columns=signal\tfingerprint\tphase\tvariant\thypothesis\tdisposition") bad = 1; \
			next \
		} \
		/^#/ { bad = 1; next } \
		{ \
			if (NF != 6) { bad = 1; next } \
			for (i = 1; i <= 6; i++) if ($$i == "" || $$i ~ /[[:cntrl:]]/) bad = 1; \
			if ($$1 != "-" && ($$1 !~ /^[0-9]+$$/ || ($$1 + 0) < 1 || \
			    ($$1 + 0) > 127 || $$1 != ($$1 + 0) "")) bad = 1; \
			if ($$3 !~ /^(pp|parse|sema|ir-verify|opt|cg|as|ld|run|ICE|policy)$$/) bad = 1; \
			if ($$4 !~ /^(all|optdiv|non-optdiv)$$/) bad = 1; \
			if ($$6 !~ /^(fix-sprint:[A-Za-z0-9][A-Za-z0-9._-]*|xfail:TORT-[0-9][0-9][0-9]|wontfix-0\.1\.0|out-of-scope)$$/) bad = 1; \
			key = $$1 FS $$2 FS $$3 FS $$4; \
			if (previous != "" && key <= previous) bad = 1; \
			previous = key \
		} \
		END { if (NR < 2 || bad) { \
			print "torture.mk: malformed triage policy: " source > "/dev/stderr"; \
			exit 1 \
		} }' "$$policy"; then \
		exit 1; \
	fi; \
	test -f "$(CGF_TORTURE_XFAIL_LEDGER)" && \
	test -r "$(CGF_TORTURE_XFAIL_LEDGER)" || { \
		echo 'torture.mk: XFAIL ledger is not a readable regular file' >&2; exit 1; }; \
	ids=$$({ \
		awk -F '\t' '!/^#/ && $$6 ~ /^xfail:TORT-[0-9][0-9][0-9]$$/ { sub(/^xfail:/, "", $$6); print $$6 }' \
			"$(CGF_TORTURE_MANIFEST)"; \
		awk -F '\t' '$$1 == "case" && $$5 ~ /^xfail:TORT-[0-9][0-9][0-9]$$/ { sub(/^xfail:/, "", $$5); print $$5 }' \
			"$(CGF_CTESTSUITE_MANIFEST)"; \
		awk -F '\t' '!/^#/ && $$6 ~ /^xfail:TORT-[0-9][0-9][0-9]$$/ { sub(/^xfail:/, "", $$6); print $$6 }' \
			"$(CGF_TORTURE_TRIAGE_POLICY)"; \
	} | LC_ALL=C sort -u); \
	for id in $$ids; do \
		awk -v id="$$id" \
			'$$0 ~ "^##[[:space:]]+" id "([[:space:]]|$$)" || \
			 $$0 ~ "^\\|[[:space:]]*" id "[[:space:]]*\\|" { found = 1 } \
			 END { exit !found }' "$(CGF_TORTURE_XFAIL_LEDGER)" || { \
			echo "torture.mk: missing XFAIL ledger entry for $$id" >&2; \
			exit 1; \
		}; \
	done

torture-meta:
	sh tests/scripts/gates/torture_runner_test.sh
	sh tests/scripts/gates/torture_triage_test.sh
	sh tests/scripts/gates/torture_provenance_test.sh
	sh tests/scripts/gates/torture_matrix_test.sh

# The import idempotence tests intentionally copy and hash both full upstream
# corpora several times.  Keep them out of the sanitizer-recursive `make test`
# path; the dedicated torture CI lane runs them once.
torture-import-meta:
	sh tests/scripts/gates/torture_import_test.sh
	sh tests/scripts/gates/ctestsuite_import_test.sh
