#!/bin/sh
set -eu

receipt=
target=
while [ "$#" -gt 0 ]; do
    [ "$#" -ge 2 ] || exit 2
    case $1 in
        --receipt) receipt=$2 ;;
        --target) target=$2 ;;
        --driver | --compiler | --runner | --torture-manifest | --ctestsuite-manifest) ;;
        *) exit 2 ;;
    esac
    shift 2
done

[ -f "$receipt" ] && [ -n "$target" ] || exit 2
[ "${FAKE_PROVENANCE_FAIL:-0}" != 1 ] || exit 23
case $target in x86_64-linux-gnu | arm64-linux) ;; *) exit 2 ;; esac

state=a
if [ -n "${FAKE_PROVENANCE_STATE_FILE:-}" ]; then
    state=$(sed -n '1p' "$FAKE_PROVENANCE_STATE_FILE")
fi
case $state in
    a) harness=2222222222222222222222222222222222222222222222222222222222222222 ;;
    b) harness=9999999999999999999999999999999999999999999999999999999999999999 ;;
    *) exit 2 ;;
esac

echo '# source-revision=1111111111111111111111111111111111111111'
echo '# compiler-source-sha256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
echo "# harness-sha256=$harness"
echo '# torture-manifest-sha256=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'
echo '# ctestsuite-manifest-sha256=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc'
echo "# target=$target"
echo '# compiler-binary-sha256=dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd'
echo '# compiler-driver-sha256=eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee'
