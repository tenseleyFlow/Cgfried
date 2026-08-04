# Conservative, statement-local constant propagation for the Sprint 47
# division-UB corpus gate.  Input has already had comments and literals
# erased, so identifiers and arithmetic operators are safe to inspect.
function compact(s)
{
    gsub(/[ \t\r\n()]/, "", s)
    return s
}

function category(expr, e, p, a)
{
    e = compact(expr)
    if (e in value_kind)
        return value_kind[e]
    if (e ~ /^(INT_MIN|LONG_MIN|LLONG_MIN|INT32_MIN|INT64_MIN)$/)
        return "min"
    if (e ~ /^\+?(0[xX]0+|0+)[uUlL]*$/)
        return "zero"
    if (e ~ /^-1[lL]*$/)
        return "negone"
    if (e ~ /^-2147483648[lL]*$/ || e ~ /^-9223372036854775808[lL]*$/ ||
        e ~ /^-2147483647-1$/ || e ~ /^-9223372036854775807-1$/)
        return "min"
    if (e ~ /^[0-9]+-[0-9]+$/) {
        p = index(e, "-")
        if (substr(e, 1, p - 1) + 0 == substr(e, p + 1) + 0)
            return "zero"
    }
    return ""
}

function identifier_before(s, end, prefix, n, words)
{
    prefix = substr(s, 1, end)
    gsub(/[^A-Za-z0-9_]/, " ", prefix)
    n = split(prefix, words, /[ \t]+/)
    while (n > 0 && words[n] == "")
        n--
    return words[n]
}

function report(what, name)
{
    printf "%d:%s '%s'\n", NR, what, name
    bad = 1
}

function inspect_divisions(stmt, rest, hit, op, lhs, rhs, cat_l, cat_r, div)
{
    rest = stmt
    while (match(rest,
                 /[\/%][ \t]*\([ \t]*[0-9]+[ \t]*-[ \t]*[0-9]+[ \t]*\)/)) {
        hit = compact(substr(rest, RSTART, RLENGTH))
        rhs = substr(hit, 2)
        if (category(rhs) == "zero")
            report("computed zero divisor", rhs)
        rest = substr(rest, RSTART + RLENGTH)
    }
    rest = stmt
    gsub(/[()]/, "", rest)
    while (match(rest, /[\/%][ \t]*[A-Za-z_][A-Za-z0-9_]*/)) {
        hit = compact(substr(rest, RSTART, RLENGTH))
        rhs = substr(hit, 2)
        if (category(rhs) == "zero")
            report("computed zero divisor", rhs)
        rest = substr(rest, RSTART + RLENGTH)
    }
    rest = stmt
    gsub(/[()]/, "", rest)
    while (match(rest,
                 /[A-Za-z_][A-Za-z0-9_]*[ \t]*[\/%][ \t]*(-[ \t]*1[lL]*|[A-Za-z_][A-Za-z0-9_]*)/)) {
        hit = compact(substr(rest, RSTART, RLENGTH))
        div = index(hit, "/")
        if (!div)
            div = index(hit, "%")
        lhs = substr(hit, 1, div - 1)
        rhs = substr(hit, div + 1)
        cat_l = category(lhs)
        cat_r = category(rhs)
        if (cat_l == "min" && cat_r == "negone")
            report("locally propagated signed minimum divided by -1", lhs)
        rest = substr(rest, RSTART + RLENGTH)
    }
    # Division-assignment has '=' between the operator and denominator.
    rest = compact(stmt)
    while (match(rest, /[\/%]=[A-Za-z_][A-Za-z0-9_]*/)) {
        hit = substr(rest, RSTART, RLENGTH)
        rhs = substr(hit, 3)
        if (category(rhs) == "zero")
            report("computed zero divisor", rhs)
        rest = substr(rest, RSTART + RLENGTH)
    }
}

function has_control_flow(stmt)
{
    return stmt ~ /(^|[^A-Za-z0-9_])(if|else|for|while|switch|do)([^A-Za-z0-9_]|$)/ ||
           stmt ~ /\?/ || stmt ~ /&&|\|\|/
}

function inspect_statement(stmt, guarded, m, chunk, rel, eq, lhs, rhs, kind)
{
    inspect_divisions(stmt)

    # Track plain declarations/assignments; a later unknown assignment kills
    # the fact only when it is textually unconditional.  Assignments in a
    # conditional/loop cannot erase a fact that still reaches a later divide.
    if (!guarded && match(stmt, /(^|[^=!<>])=[^=]/)) {
        chunk = substr(stmt, RSTART, RLENGTH)
        rel = index(chunk, "=")
        eq = RSTART + rel - 1
        lhs = identifier_before(stmt, eq - 1)
        rhs = substr(stmt, eq + 1)
        if (lhs != "") {
            kind = category(rhs)
            if (kind != "")
                value_kind[lhs] = kind
            else
                delete value_kind[lhs]
        }
    }
}

{
    # Accumulate complete statements so a multi-line `if (cond) assignment;`
    # keeps its control context.  Braced control bodies inherit that context;
    # ordinary function/aggregate braces do not.  Semicolons inside `for`
    # parentheses are not statement boundaries.
    for (column = 1; column <= length($0) + 1; column++) {
        c = column <= length($0) ? substr($0, column, 1) : "\n"
        if (c == "(") {
            paren_depth++
            statement = statement c
        } else if (c == ")") {
            if (paren_depth > 0)
                paren_depth--
            statement = statement c
        } else if (c == "{" && paren_depth == 0) {
            brace_depth++
            brace_guard[brace_depth] = guard_depth > 0 ||
                                       has_control_flow(statement)
            if (brace_guard[brace_depth])
                guard_depth++
            statement = ""
        } else if (c == "}" && paren_depth == 0) {
            statement = ""
            if (brace_depth > 0) {
                if (brace_guard[brace_depth])
                    guard_depth--
                delete brace_guard[brace_depth]
                brace_depth--
            }
        } else if (c == ";" && paren_depth == 0) {
            inspect_statement(statement,
                              guard_depth > 0 || has_control_flow(statement))
            statement = ""
        } else {
            statement = statement c
        }
    }
}

END { exit bad ? 1 : 0 }
