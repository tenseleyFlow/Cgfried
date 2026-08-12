#!/bin/sh
# Run one compiler/level/target cell of the imported Sprint 56 corpora.
set -eu

LC_ALL=C
export LC_ALL

die()
{
    echo "torture-run: $*" >&2
    exit 3
}

usage()
{
    die "usage: torture-run.sh --cc PATH --suite SUITE --level LEVEL --target TARGET --manifest FILE --output FILE --work DIR"
}

cc=
suite=
level=
target=
manifest=
output=
work=
while [ "$#" -gt 0 ]; do
    [ "$#" -ge 2 ] || usage
    case $1 in
    --cc) cc=$2 ;;
    --suite) suite=$2 ;;
    --level) level=$2 ;;
    --target) target=$2 ;;
    --manifest) manifest=$2 ;;
    --output) output=$2 ;;
    --work) work=$2 ;;
    *) usage ;;
    esac
    shift 2
done

[ -n "$cc" ] && [ -n "$suite" ] && [ -n "$level" ] &&
    [ -n "$target" ] && [ -n "$manifest" ] && [ -n "$output" ] &&
    [ -n "$work" ] || usage
case $suite in
torture-compile | torture-execute | torture-execute-ieee | ctestsuite) ;;
*) die "unsupported suite: $suite" ;;
esac
case $level in O0 | O1 | O2 | O3 | Os) ;; *) die "unsupported level: $level" ;; esac
case $target in
x86_64-linux-gnu | arm64-linux) ;;
*) die "unsupported target: $target" ;;
esac
case ${CGF_TORTURE_TIMEOUT:-30} in
'' | *[!0-9]* | 0) die "CGF_TORTURE_TIMEOUT must be a positive integer" ;;
*) timeout_s=${CGF_TORTURE_TIMEOUT:-30} ;;
esac
case ${CGF_TORTURE_KILL_AFTER:-2} in
'' | *[!0-9]* | 0) die "CGF_TORTURE_KILL_AFTER must be a positive integer" ;;
*) kill_after_s=${CGF_TORTURE_KILL_AFTER:-2} ;;
esac
case ${CGF_TORTURE_OUTPUT_LIMIT:-16777216} in
'' | *[!0-9]*) die "CGF_TORTURE_OUTPUT_LIMIT must be an integer of at least 1024 bytes" ;;
*) output_limit=${CGF_TORTURE_OUTPUT_LIMIT:-16777216} ;;
esac
[ "$output_limit" -ge 1024 ] ||
    die "CGF_TORTURE_OUTPUT_LIMIT must be an integer of at least 1024 bytes"
# Each concurrent capture drain stores half the budget and discards the rest.
# The FIFOs are drained for the command's full lifetime, so a verbose child
# cannot block after reaching the cap and no compiler/program-created file is
# subject to a process-wide file-size limit.
output_stream_limit=$((output_limit / 2))
[ -f "$manifest" ] && [ -r "$manifest" ] || die "cannot read manifest: $manifest"
command -v timeout >/dev/null 2>&1 || die "required utility not found: timeout"
if command -v sha256sum >/dev/null 2>&1; then
    sha_tool=sha256sum
elif command -v shasum >/dev/null 2>&1; then
    sha_tool=shasum
else
    die "sha256sum or shasum -a 256 is required"
fi

mkdir -p "$work/cases" || die "cannot create work directory: $work"
work=$(CDPATH='' cd "$work" && pwd -P) || die "cannot resolve work directory"
manifest_dir=$(CDPATH='' cd "$(dirname "$manifest")" && pwd -P) ||
    die "cannot resolve manifest directory"

resolve_executable()
{
    re_value=$1
    case $re_value in
    */*)
        re_dir=$(CDPATH='' cd "$(dirname "$re_value")" && pwd -P) || return 1
        re_path=$re_dir/$(basename "$re_value")
        ;;
    *)
        re_path=$(command -v "$re_value" 2>/dev/null) || return 1
        case $re_path in
        /*) ;;
        *)
            re_dir=$(CDPATH='' cd "$(dirname "$re_path")" && pwd -P) || return 1
            re_path=$re_dir/$(basename "$re_path")
            ;;
        esac
        ;;
    esac
    [ -x "$re_path" ] || return 1
    printf '%s\n' "$re_path"
}

run_wrapper=
if [ -n "${CGF_TORTURE_RUN:-}" ]; then
    run_wrapper=$(resolve_executable "$CGF_TORTURE_RUN") ||
        die "execution wrapper is not executable: $CGF_TORTURE_RUN"
fi

capture_stream()
{
    cs_fifo=$1
    cs_log=$2
    cs_marker=$3
    cs_extra=$cs_marker.byte
    (
        head -c "$output_stream_limit" > "$cs_log"
        dd bs=1 count=1 of="$cs_extra" 2>/dev/null
        if [ -s "$cs_extra" ]; then
            : > "$cs_marker"
            cat >/dev/null
        fi
        rm -f "$cs_extra"
    ) < "$cs_fifo"
}

capture_begin()
{
    cb_out=$1
    cb_err=$2
    capture_serial=$((capture_serial + 1))
    capture_out_fifo=$work/.capture-$capture_serial.stdout.fifo
    capture_err_fifo=$work/.capture-$capture_serial.stderr.fifo
    capture_out_marker=$work/.capture-$capture_serial.stdout.overflow
    capture_err_marker=$work/.capture-$capture_serial.stderr.overflow
    rm -f "$capture_out_fifo" "$capture_err_fifo" \
        "$capture_out_marker" "$capture_err_marker"
    mkfifo "$capture_out_fifo" "$capture_err_fifo" ||
        die "cannot create output-capture FIFOs"
    capture_stream "$capture_out_fifo" "$cb_out" "$capture_out_marker" &
    capture_out_pid=$!
    capture_stream "$capture_err_fifo" "$cb_err" "$capture_err_marker" &
    capture_err_pid=$!
}

capture_finish()
{
    cf_status=0
    wait "$capture_out_pid" || cf_status=1
    wait "$capture_err_pid" || cf_status=1
    [ "$cf_status" -eq 0 ] || die "output-capture drain failed"
    capture_overflow=0
    if [ -f "$capture_out_marker" ] || [ -f "$capture_err_marker" ]; then
        capture_overflow=1
    fi
    rm -f "$capture_out_fifo" "$capture_err_fifo" \
        "$capture_out_marker" "$capture_err_marker"
}

capture_serial=0
probe=$work/target-probe.out
probe_err=$work/target-probe.err
probe_status=0
capture_begin "$probe" "$probe_err"
(
    CGF_AS=${CGF_TORTURE_AS:-0} timeout --verbose --kill-after="${kill_after_s}s" \
        "$timeout_s" "$cc" "--target=$target" -dumpmachine
) >"$capture_out_fifo" 2>"$capture_err_fifo" || probe_status=$?
capture_finish
if [ "$capture_overflow" -eq 1 ]; then
    die "compiler target probe output limit exceeded"
fi
[ "$probe_status" -eq 0 ] || die "compiler target probe failed with exit $probe_status"
IFS= read -r actual_target < "$probe" || actual_target=
[ "$actual_target" = "$target" ] ||
    die "compiler target mismatch: requested $target, got ${actual_target:-<empty>}"

tab=$(printf '\t')
rows=$work/results.unsorted.tsv
: > "$rows"
seen=$work/keys.seen
: > "$seen"

safe_path()
{
    case $1 in
    '' | /* | ../* | */../* | */.. | .. | *[!A-Za-z0-9_./+-]*) return 1 ;;
    *) return 0 ;;
    esac
}

sha256_file()
{
    case $sha_tool in
    sha256sum) sha256sum "$1" | awk '{print $1}' ;;
    shasum) shasum -a 256 "$1" | awk '{print $1}' ;;
    esac
}

sha256_stdin()
{
    case $sha_tool in
    sha256sum) sha256sum | awk '{print $1}' ;;
    shasum) shasum -a 256 | awk '{print $1}' ;;
    esac
}

check_hash()
{
    hash_path=$1
    hash_want=$2
    case $hash_want in *[!0-9a-f]* | '') die "invalid SHA-256 for $hash_path" ;; esac
    [ "${#hash_want}" -eq 64 ] || die "invalid SHA-256 for $hash_path"
    [ -f "$hash_path" ] || die "manifest asset does not exist: $hash_path"
    hash_got=$(sha256_file "$hash_path") || die "cannot hash: $hash_path"
    [ "$hash_got" = "$hash_want" ] || die "SHA-256 mismatch: $hash_path"
}

normalize_text()
{
    # Normalize only presence/absence of the final newline. Additional trailing
    # blank lines are output bytes and must remain significant.
    cat "$1"
    [ -s "$1" ] || return 0
    nt_last=$(tail -c 1 "$1" | od -An -tu1 | tr -d ' ')
    [ "$nt_last" = 10 ] || printf '\n'
}

sanitize()
{
    sed_source=$1
    sed_work=$2
    awk -v source="$sed_source" -v work="$sed_work" '
        {
            s = $0
            if (source != "")
                while ((at = index(s, source)) != 0)
                    s = substr(s, 1, at - 1) "<source>" substr(s, at + length(source))
            if (work != "")
                while ((at = index(s, work)) != 0)
                    s = substr(s, 1, at - 1) "<work>" substr(s, at + length(work))
            print s
        }
    ' |
        sed \
        -e 's|/[A-Za-z0-9_.+~-][^:[:space:]]*|<path>|g' \
        -e "s/:[0-9][0-9]*:[0-9][0-9]*\([:-]\)/:<loc>\1/g" \
        -e "s/:[0-9][0-9]*\([:-]\)/:<loc>\1/g" \
        -e 's/(\.[A-Za-z0-9_.][A-Za-z0-9_.]*+0x[0-9A-Fa-f][0-9A-Fa-f]*)/(<section>+<offset>)/g' \
        -e 's/"[A-Za-z_][A-Za-z0-9_]*"/<id>/g' |
        awk '
            {
                rest = $0
                out = ""
                while ((open = index(rest, "`")) != 0) {
                    after = substr(rest, open + 1)
                    ending = index(after, "\047")
                    if (ending == 0) break
                    token = substr(after, 1, ending - 1)
                    out = out substr(rest, 1, open - 1)
                    if (token ~ /^[A-Za-z_][A-Za-z0-9_]*$/)
                        out = out "<id>"
                    else
                        out = out "`" token "\047"
                    rest = substr(after, ending + 1)
                }
                print out rest
            }
        ' |
        awk '
            {
                rest = $0
                out = ""
                keywords = " auto break case char const continue default do double else enum extern float for goto if inline int long register restrict return short signed sizeof static struct switch typedef union unsigned void volatile while _Alignas _Alignof _Atomic _Bool _Complex _Generic _Imaginary _Noreturn _Static_assert _Thread_local asm __asm __asm__ typeof __typeof __typeof__ __auto_type __extension__ "
                while ((open = index(rest, "\047")) != 0) {
                    after = substr(rest, open + 1)
                    ending = index(after, "\047")
                    if (ending == 0) break
                    token = substr(after, 1, ending - 1)
                    out = out substr(rest, 1, open - 1)
                    if (token ~ /^[A-Za-z_][A-Za-z0-9_]*$/ &&
                        index(keywords, " " token " ") == 0)
                        out = out "<id>"
                    else
                        out = out "\047" token "\047"
                    rest = substr(after, ending + 1)
                }
                print out rest
            }
        ' |
        tr '\001-\037\177' ' ' |
        sed -e 's/[[:space:]][[:space:]]*/ /g' \
            -e 's/^ //' -e 's/ $//'
}

normalized_diagnostic()
{
    nd_log=$1
    nd_source=$2
    nd_line=$(first_diagnostic_line "$nd_log")
    [ -n "$nd_line" ] || return 0
    printf '%s\n' "$nd_line" | sanitize "$nd_source" "$work"
}

first_diagnostic_line()
{
    awk '
        NF && first == "" { first = $0 }
        NF && ($0 ~ /^[^[:space:]]+:[0-9]+(:[0-9]+)?:[[:space:]]*((fatal|internal compiler)[[:space:]]+)?error:/ ||
            $0 ~ /(^|[[:space:]])internal compiler error:/ ||
            $0 ~ /(^|:[[:space:]])undefined reference to/ ||
            $0 ~ /(^|:[[:space:]])multiple definition of/ ||
            $0 ~ /(^|:[[:space:]])cannot find -l/ ||
            $0 ~ /(^|\/)(cc|gcc|g\+\+|clang|cgf|cgfried|ld|collect2|as):[[:space:]]*(fatal[[:space:]]+)?error:/) {
            print
            found = 1
            exit
        }
        NF && generic == "" && $0 ~ /^(fatal[[:space:]]+)?error:/ { generic = $0 }
        END {
            if (!found && generic != "") print generic
            else if (!found && first != "") print first
        }' "$1" |
        sed -n '1p'
}

linker_diagnostic_class()
{
    ldc_log=$1
    if grep -Ei 'multiple definition of|first defined here' "$ldc_log" >/dev/null 2>&1; then
        printf '%s\n' tentative-global
    elif grep -Ei "undefined reference to .(__builtin_)?alloca('|[^A-Za-z0-9_])|relocation.*alloca" \
        "$ldc_log" >/dev/null 2>&1; then
        printf '%s\n' missing-alloca
    elif grep -Ei "undefined reference to .(acos|acosh|asin|asinh|atan|atan2|atanh|cbrt|ceil|copysign|cos|cosh|erf|erfc|exp|exp2|expm1|fabs|fdim|floor|fma|fmax|fmin|fmod|frexp|hypot|ilogb|ldexp|lgamma|llrint|llround|log|log10|log1p|log2|logb|lrint|lround|modf|nearbyint|nextafter|nexttoward|pow|remainder|remquo|rint|round|scalbln|scalbn|sin|sinh|sqrt|tan|tanh|tgamma|trunc)(f|l)?('|@@|[^A-Za-z0-9_])|cannot find -lm|DSO missing from command line.*libm" \
        "$ldc_log" >/dev/null 2>&1; then
        printf '%s\n' missing-libm
    elif grep -Ei "undefined reference to .link_error('|@@|[^A-Za-z0-9_])" \
        "$ldc_log" >/dev/null 2>&1; then
        printf '%s\n' deliberate-link-error
    elif grep -Ei "undefined reference to .(main|_main)('|@@|[^A-Za-z0-9_])|entry symbol.*not found" \
        "$ldc_log" >/dev/null 2>&1; then
        printf '%s\n' missing-entrypoint-support
    elif grep -Ei 'undefined reference to' "$ldc_log" >/dev/null 2>&1; then
        printf '%s\n' undefined-symbol
    else
        printf '%s\n' linker-other
    fi
}

diagnostic_class()
{
    dc_line=$(first_diagnostic_line "$1")
    [ -n "$dc_line" ] || return 0
    pretriage_class "$dc_line"
}

pretriage_class()
{
    case $1 in
    *asm\ goto*) printf '%s\n' asm-goto ;;
    *nested\ function*) printf '%s\n' nested-functions ;;
    *computed\ goto* | *'address of label'* | *'&&label'*) printf '%s\n' computed-goto ;;
    *vector_size* | *'mode attribute'* | *'attribute mode'*) printf '%s\n' vector-mode-attribute ;;
    *_Complex* | *'complex type'*) printf '%s\n' complex ;;
    *'is not a builtin this compiler implements'*) printf '%s\n' gcc-builtin ;;
    *) printf '%s\n' '' ;;
    esac
}

decorate_detail()
{
    dd_detail=$1
    dd_prefix=
    if [ "${case_show_xfail:-0}" -eq 1 ]; then
        dd_prefix=$case_xfail_id
    fi
    if [ "${case_tags:--}" != - ]; then
        dd_prefix="${dd_prefix}${dd_prefix:+ }[tags=$case_tags]"
    fi
    if [ "$dd_detail" = - ] || [ -z "$dd_detail" ]; then
        printf '%s\n' "${dd_prefix:--}"
    elif [ -n "$dd_prefix" ]; then
        printf '%s %s\n' "$dd_prefix" "$dd_detail"
    else
        printf '%s\n' "$dd_detail"
    fi
}

fingerprint_text()
{
    [ -n "$1" ] || { printf '%s\n' -; return; }
    printf '%s\n' "$1" | sha256_stdin
}

fingerprint_file()
{
    ff_diag=$(normalized_diagnostic "$1" "$2")
    fingerprint_text "$ff_diag"
}

validate_flags()
{
    vf_flags=$1
    [ "$vf_flags" = - ] && return 0
    case $vf_flags in *"$tab"* | *'  '* | ' '* | *' ') return 1 ;; esac
    for vf_flag in $vf_flags; do
        case $vf_flag in
        *[!ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.,=+-]*) return 1 ;;
        esac
        case $vf_flag in
        -f[A-Za-z0-9_+-]* | -m[A-Za-z0-9_.,=+-]* | -W[A-Za-z0-9_.,=+-]* | \
        -std=c89 | -std=c90 | -std=c99 | -std=c11 | -std=c17 | \
        -std=gnu89 | -std=gnu90 | -std=gnu99 | -std=gnu11 | -std=gnu17 | \
        -D[A-Za-z_][A-Za-z0-9_]* | -D[A-Za-z_][A-Za-z0-9_]*=[A-Za-z0-9_.,+-]* | \
        -U[A-Za-z_][A-Za-z0-9_]*) ;;
        *) return 1 ;;
        esac
    done
}

validate_tags()
{
    case $1 in
    -) return 0 ;;
    '' | *[!ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_,+-]*) return 1 ;;
    *) return 0 ;;
    esac
}

timeout_marker_present()
{
    grep -F 'timeout: sending signal ' "$1" >/dev/null 2>&1
}

is_timeout_status()
{
    case $1 in 124 | 137) return 0 ;; *) return 1 ;; esac
}

compiler_phase_probe()
{
    cpp_kind=$1
    cpp_out=$case_dir/phase-$cpp_kind.stdout
    cpp_err=$case_dir/phase-$cpp_kind.stderr
    set -- "$cc" "--target=$target" -std=gnu17 "-$level"
    if [ "$flags" != - ]; then
        for cpp_flag in $flags; do set -- "$@" "$cpp_flag"; done
    fi
    case $cpp_kind in
    pp) set -- "$@" -E "$source" ;;
    parse) set -- "$@" --dump-ast "$source" ;;
    sema) set -- "$@" -fsyntax-only "$source" ;;
    esac
    cpp_status=0
    capture_begin "$cpp_out" "$cpp_err"
    (
        set +e
        CGF_AS=${CGF_TORTURE_AS:-0} timeout --verbose \
            --kill-after="${kill_after_s}s" "$timeout_s" "$@"
        timed_status=$?
        exit "$timed_status"
    ) >"$capture_out_fifo" 2>"$capture_err_fifo" || cpp_status=$?
    capture_finish
    # A successful phase probe stays successful even when its diagnostic dump
    # exceeds the capture budget. The drain remains bounded; overflow is not a
    # compiler-stage failure signal.
    [ "$cpp_status" -eq 0 ]
}

classify_compiler_phase()
{
    if ! compiler_phase_probe pp; then
        printf '%s\n' pp
    elif ! compiler_phase_probe parse; then
        printf '%s\n' parse
    elif ! compiler_phase_probe sema; then
        printf '%s\n' sema
    else
        printf '%s\n' cg
    fi
}

requirement_supported()
{
    rs_value=$1
    [ "$rs_value" = - ] && return 0
    if [ "${CGF_TORTURE_CAPABILITIES+x}" = x ]; then
        rs_caps=$CGF_TORTURE_CAPABILITIES
    else
        rs_caps=int32plus,ptr32plus,size20plus,size32plus,int32,longlong64,double64,double64plus,stdint_types,c99_runtime,indirect_calls,run_expensive_tests,fileio,mmap,pthread,fpic,nonpic,non_strict_prototype,named_sections,scheduling
    fi
    old_ifs=$IFS
    IFS=,
    # Intentional splitting: requirements are a comma-separated closed enum.
    # shellcheck disable=SC2086
    set -- $rs_value
    IFS=$old_ifs
    for rs_token in "$@"; do
        case $rs_token in
        '' | label_values | trampolines | indirect_jumps | skip | skip:* | \
        unsupported | unsupported:*) return 1 ;;
        esac
        case ,$rs_caps, in *,"$rs_token",*) ;; *) return 1 ;; esac
    done
    return 0
}

emit_row()
{
    er_key=$1 er_file=$2 er_outcome=$3 er_signal=$4 er_fp=$5 er_phase=$6
    shift 6
    er_detail=$(printf '%s\n' "$*" | sanitize "$source" "$work")
    er_detail=$(decorate_detail "$er_detail")
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$er_key" "$suite" "$er_file" "$level" "$target" "$er_outcome" \
        "$er_signal" "$er_fp" "$er_phase" "${er_detail:--}" >> "$rows"
}

run_case()
{
    file=$1 source=$2 expected=$3 flags=$4 disposition=$5 reason=$6 requirement=$7 mode=$8 case_tags=${9:--}
    case_xfail_id=
    case_show_xfail=0
    safe_path "$file" || die "unsafe case path: $file"
    safe_path "$source" || die "unsafe source path: $source"
    source=$manifest_dir/$source
    [ -f "$source" ] || die "case source does not exist: $source"
    validate_tags "$case_tags" || die "unsafe or malformed tags for $file: $case_tags"
    if [ "$expected" != - ] && [ "$expected" != -- ]; then
        safe_path "$expected" || die "unsafe expected-output path: $expected"
        expected=$manifest_dir/$expected
        [ -f "$expected" ] || die "expected output does not exist: $expected"
    fi
    validate_flags "$flags" || die "unsafe or unsupported flags for $file: $flags"

    key=$suite/$file@$level@$target
    if grep -F -x "$key" "$seen" >/dev/null 2>&1; then
        die "duplicate selected case key: $key"
    fi
    printf '%s\n' "$key" >> "$seen"
    case_name=$(printf '%s\n' "$key" | sed 's/[^A-Za-z0-9_.-]/_/g')
    case_dir=$work/cases/$case_name
    mkdir -p "$case_dir"

    case $disposition in
    - | pass | run) xfail=0 ;;
    xfail:TORT-[0-9][0-9][0-9]) xfail=1; case_xfail_id=$disposition ;;
    skip | skip:*)
        emit_row "$key" "$file" SKIP - - policy "${reason:--}"
        return
        ;;
    *) die "invalid disposition for $file: $disposition" ;;
    esac
    if ! requirement_supported "$requirement"; then
        if [ "$reason" = - ] || [ -z "$reason" ]; then
            reason=skip-unless:$requirement
        fi
        emit_row "$key" "$file" SKIP - - policy "$reason"
        return
    fi

    compile_out=$case_dir/compile.stdout
    compile_err=$case_dir/compile.stderr
    binary=$case_dir/program
    set -- "$cc" "--target=$target" -std=gnu17 "-$level"
    if [ "$suite" = torture-execute-ieee ]; then set -- "$@" -frounding-math; fi
    if [ "$flags" != - ]; then
        for flag in $flags; do set -- "$@" "$flag"; done
    fi
    if [ "$mode" = compile ]; then
        set -- "$@" -c "$source" -o "$case_dir/program.o"
    else
        set -- "$@" "$source" -o "$binary"
    fi
    compile_status=0
    capture_begin "$compile_out" "$compile_err"
    (
        set +e
        CGF_AS=${CGF_TORTURE_AS:-0} timeout --verbose \
            --kill-after="${kill_after_s}s" "$timeout_s" "$@"
        timed_status=$?
        exit "$timed_status"
    ) >"$capture_out_fifo" 2>"$capture_err_fifo" || compile_status=$?
    capture_finish
    compile_output_overflow=$capture_overflow
    if [ "$compile_output_overflow" -eq 1 ]; then
        [ "$compile_status" -ne 0 ] || compile_status=1
    fi
    if [ "$compile_status" -ne 0 ]; then
        signal=-
        phase=cg
        outcome=COMPILE_FAIL
        detail="compiler exited $compile_status"
        if [ "$compile_output_overflow" -eq 1 ]; then
            detail='output limit exceeded'
        elif is_timeout_status "$compile_status" &&
            timeout_marker_present "$compile_err"; then
            outcome=TIMEOUT
            detail='compiler timed out'
        else
            case $compile_status in
            4) outcome=ICE; phase=ICE; detail='compiler exited 4' ;;
            124)
                phase=$(classify_compiler_phase)
                ;;
            2) phase=ld; detail='linker exited 2' ;;
            1)
                phase=$(classify_compiler_phase)
                if grep -Ei 'IR verif|optimizer|codegen|assembler' "$compile_err" >/dev/null 2>&1; then
                    phase=$(sed -n 's/.*\(preprocess\|parse\|semantic\|IR verif[^: ]*\|optimizer\|codegen\|assembler\).*/\1/p' "$compile_err" | sed -n '1p')
                    case $phase in preprocess) phase=pp ;; semantic) phase=sema ;; 'IR verif'*) phase=ir-verify ;; optimizer) phase=opt ;; codegen) phase=cg ;; assembler) phase=as ;; esac
                fi
                ;;
            12[89] | 13[0-9] | 14[0-9] | 15[0-9] | 16[0-9] | 17[0-9] | 18[0-9] | 19[0-2])
                outcome=SIGNAL; signal=$((compile_status - 128)); detail="compiler killed by signal $signal"
                ;;
            esac
        fi
        diag=
        diag_class=
        if [ "$compile_output_overflow" -eq 0 ]; then
            diag=$(normalized_diagnostic "$compile_err" "$source")
            diag_class=$(diagnostic_class "$compile_err")
            if [ "$phase" = ld ] && [ -n "$diag" ]; then
                ld_class=$(linker_diagnostic_class "$compile_err")
                diag="[ld-class=$ld_class] $diag"
            fi
        fi
        if [ -n "$diag" ]; then
            if [ -n "$diag_class" ]; then
                detail="[class=$diag_class] $diag"
            else
                detail=$diag
            fi
        fi
        if [ "$xfail" -ne 0 ]; then outcome=XFAIL; signal=-; case_show_xfail=1; fi
        if [ -n "$diag" ]; then
            fp=$(fingerprint_text "$diag")
        else
            fp=-
        fi
        emit_row "$key" "$file" "$outcome" "$signal" "$fp" "$phase" "$detail"
        return
    fi

    if [ "$mode" = compile ]; then
        emit_row "$key" "$file" PASS - - cg -
        return
    fi

    run_out=$case_dir/run.stdout
    run_err=$case_dir/run.stderr
    run_status=0
    wrapper_used=0
    if [ -n "$run_wrapper" ]; then
        wrapper_used=1
        capture_begin "$run_out" "$run_err"
        (
            set +e
            cd "$case_dir" || exit 126
            timeout --verbose --kill-after="${kill_after_s}s" \
                "$timeout_s" "$run_wrapper" "$binary"
            timed_status=$?
            exit "$timed_status"
        ) >"$capture_out_fifo" 2>"$capture_err_fifo" || run_status=$?
        capture_finish
    else
        capture_begin "$run_out" "$run_err"
        (
            set +e
            cd "$case_dir" || exit 126
            timeout --verbose --kill-after="${kill_after_s}s" \
                "$timeout_s" "$binary"
            timed_status=$?
            exit "$timed_status"
        ) >"$capture_out_fifo" 2>"$capture_err_fifo" || run_status=$?
        capture_finish
    fi
    outcome=PASS signal=- phase=run detail=- fp=-
    run_output_overflow=$capture_overflow
    if [ "$run_output_overflow" -eq 1 ]; then
        outcome=OUTPUT_FAIL
        detail='output limit exceeded'
    elif is_timeout_status "$run_status" && timeout_marker_present "$run_err"; then
        outcome=TIMEOUT
        detail='program timed out'
    else
        case $run_status in
        0) ;;
        124)
            outcome=WRONG_EXIT; detail='program exited 124'
            ;;
        125)
            if [ "$wrapper_used" -eq 1 ]; then
                outcome=SKIP; phase=policy; detail='execution wrapper unavailable'
            else
                outcome=WRONG_EXIT; detail='program exited 125'
            fi
            ;;
        12[89] | 13[0-9] | 14[0-9] | 15[0-9] | 16[0-9] | 17[0-9] | 18[0-9] | 19[0-2])
            outcome=SIGNAL; signal=$((run_status - 128)); detail="program killed by signal $signal"
            ;;
        *) outcome=WRONG_EXIT; detail="program exited $run_status" ;;
        esac
    fi
    if [ "$outcome" = PASS ] && [ "$expected" != - ] && [ "$expected" != -- ]; then
        normalize_text "$run_out" > "$case_dir/run.stdout.normalized"
        normalize_text "$expected" > "$case_dir/expected.normalized"
        if ! cmp -s "$case_dir/run.stdout.normalized" "$case_dir/expected.normalized"; then
            outcome=OUTPUT_FAIL
            # [class=...] is reserved for the closed set of known-inapplicable
            # language-extension classes that triage can pre-disposition.
            # An oracle mismatch is a repairable failure, not a wontfix class.
            detail='stdout differs from expected output'
            expected_fp=$(sha256_file "$case_dir/expected.normalized")
            actual_fp=$(sha256_file "$case_dir/run.stdout.normalized")
            fp=$(fingerprint_text "[class=stdout-mismatch] expected=$expected_fp actual=$actual_fp")
        fi
    fi
    if [ "$outcome" != PASS ] && [ "$outcome" != SKIP ]; then
        if [ "$fp" = - ]; then
            fp=$(fingerprint_file "$run_err" "$source")
        fi
        if [ "$xfail" -ne 0 ]; then outcome=XFAIL; signal=-; case_show_xfail=1; fi
    fi
    emit_row "$key" "$file" "$outcome" "$signal" "$fp" "$phase" "$detail"
}

if [ "$suite" = ctestsuite ]; then
    while IFS="$tab" read -r kind a b c d e f extra; do
        case $kind in '' | \#*) continue ;; esac
        [ -z "${extra:-}" ] || die "malformed ctestsuite manifest row: $kind"
        case $kind in
        file)
            [ -n "$a" ] && [ -n "$b" ] && [ -z "${c:-}${d:-}${e:-}${f:-}" ] ||
                die "malformed ctestsuite file row"
            safe_path "$a" || die "unsafe ctestsuite asset path"
            check_hash "$manifest_dir/$a" "$b"
            ;;
        case)
            [ -n "$a" ] && [ -n "$b" ] && [ -n "$c" ] && [ -n "$d" ] &&
                [ -n "$e" ] && [ -z "${f:-}" ] || die "malformed ctestsuite case row"
            run_case "$a" "$a" "$b" - "$d" "$e" - execute "$c"
            ;;
        asset) continue ;;
        *) die "unknown ctestsuite manifest row: $kind" ;;
        esac
    done < "$manifest"
else
    while IFS="$tab" read -r path hash mode requirement flags disposition reason extra; do
        case $path in '' | \#*) continue ;; esac
        [ -z "${extra:-}" ] || die "malformed torture manifest row: $path"
        safe_path "$path" || die "unsafe torture path: $path"
        case $suite:$mode in
        torture-compile:compile)
            check_hash "$manifest_dir/$path" "$hash"
            case $path in compile/*) file=${path#compile/} ;; *) die "compile asset is outside compile/: $path" ;; esac
            run_case "$file" "$path" - "$flags" "$disposition" "$reason" "$requirement" compile
            ;;
        torture-execute:run)
            check_hash "$manifest_dir/$path" "$hash"
            case $path in execute/*) file=${path#execute/} ;; *) die "run asset is outside execute/: $path" ;; esac
            run_case "$file" "$path" - "$flags" "$disposition" "$reason" "$requirement" execute
            ;;
        torture-execute-ieee:run-ieee)
            check_hash "$manifest_dir/$path" "$hash"
            case $path in execute-ieee/*) file=${path#execute-ieee/} ;; *) die "run-ieee asset is outside execute-ieee/: $path" ;; esac
            run_case "$file" "$path" - "$flags" "$disposition" "$reason" "$requirement" execute
            ;;
        *) continue ;;
        esac
    done < "$manifest"
fi

{
    echo '# cgf-torture-results-v1'
    printf '# columns=key\tsuite\tfile\tlevel\ttarget\toutcome\tsignal\tfingerprint\tphase\tdetail\n'
    sort "$rows"
} > "$output" || die "cannot write output: $output"
