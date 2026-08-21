function bad(message) {
    printf "check-closeouts: %s:%d: %s\n", shown, NR, message > "/dev/stderr"
    invalid = 1
}

NR == 1 {
    prefix = "# Closeout: Phase " expected " — "
    if (index($0, prefix) != 1 || length($0) == length(prefix))
        bad("invalid phase heading")
}

/^Date:/ {
    dates++
    date_line = NR
    value = $0
    sub(/^Date:[[:space:]]*/, "", value)
    if (value !~ /^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]$/)
        bad("Date must use YYYY-MM-DD")
}

/^Baseline commit:/ {
    baselines++
    baseline_line = NR
    value = $0
    sub(/^Baseline commit:[[:space:]]*/, "", value)
    if (value ~ /^`[0-9a-f]+`$/) {
        sub(/^`/, "", value)
        sub(/`$/, "", value)
    }
    if (value !~ /^[0-9a-f]+$/ || length(value) < 7 || length(value) > 40)
        bad("Baseline commit must be a 7-40 digit lowercase hex hash")
}

/^Reviewer:/ {
    reviewers++
    reviewer_line = NR
    value = $0
    sub(/^Reviewer:[[:space:]]*/, "", value)
    if (value == "") bad("Reviewer must name the signer")
}

$0 == "## DoD items (from the phase" sprintf("%c", 39) "s sprint files, every numbered item)" {
    dod_sections++
    dod_line = NR
    section = "dod"
    next
}

/^## Open audit findings against this phase$/ {
    finding_sections++
    findings_line = NR
    section = "findings"
    next
}

/^## Verdict$/ {
    verdict_sections++
    verdict_line = NR
    section = "verdict"
    next
}

/^- \[/ {
    if ($0 ~ /^- \[x\] /) {
        checked++
        if ($0 !~ / — EVIDENCE: [^[:space:]].*$/)
            bad("checked item requires nonempty EVIDENCE")
        if (section == "dod" && $0 !~ /^- \[x\] S[0-9]+\.[0-9]+ /)
            bad("checked DoD item requires a sprint.item identifier")
        if (section == "dod") dod_items++
    } else if ($0 ~ /^- \[ \] /) {
        unchecked++
        if ($0 !~ / — VERDICT: (blocking( — [^[:space:]].*)?|waived: [^[:space:]].*)$/)
            bad("unchecked DoD item requires VERDICT: blocking or waived justification")
        if (section == "dod" && $0 !~ /^- \[ \] S[0-9]+\.[0-9]+ /)
            bad("unchecked DoD item requires a sprint.item identifier")
        if ($0 ~ /-[CH]-[0-9]+/ && $0 ~ / — VERDICT: waived:/)
            bad("Critical or High audit finding may not be waived")
        if (section == "dod") dod_items++
    } else {
        bad("checkbox must be exactly [x] or [ ]")
    }
}

section == "findings" && $0 !~ /^[[:space:]]*$/ { finding_content++ }
section == "verdict" && $0 !~ /^[[:space:]]*$/ { verdict_content++ }

{ final = $0 }

END {
    if (dates != 1) bad("expected exactly one Date field")
    if (baselines != 1) bad("expected exactly one Baseline commit field")
    if (reviewers != 1) bad("expected exactly one Reviewer field")
    if (dod_sections != 1) bad("missing or duplicate DoD section")
    if (finding_sections != 1) bad("missing or duplicate open-findings section")
    if (verdict_sections != 1) bad("missing or duplicate Verdict section")
    if (!(date_line < baseline_line && baseline_line < reviewer_line &&
          reviewer_line < dod_line && dod_line < findings_line &&
          findings_line < verdict_line))
        bad("template fields and sections are out of order")
    if (dod_items == 0) bad("DoD section contains no items")
    if (finding_content == 0) bad("open-findings section is empty")
    if (verdict_content < 2) bad("Verdict section requires one explanatory sentence")
    if (final !~ /^(READY|NOT READY)$/)
        bad("final line must be exactly READY or NOT READY")
    else if (final == "NOT READY")
        bad("phase is NOT READY")
    exit invalid ? 1 : 0
}
