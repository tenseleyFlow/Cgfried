#!/bin/sh
# Install the fleet nightly runner in the current user's scheduler. Never root.
set -eu

LC_ALL=C
export LC_ALL

prog=install-fleet-perf-schedule
root=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
nightly=${CGF_FLEET_NIGHTLY:-$root/scripts/fleet-nightly.sh}
uname_cmd=${CGF_FLEET_UNAME_CMD:-uname}
systemctl_cmd=${CGF_FLEET_SYSTEMCTL_CMD:-systemctl}
launchctl_cmd=${CGF_FLEET_LAUNCHCTL_CMD:-launchctl}
id_cmd=${CGF_FLEET_ID_CMD:-id}
dry_run=${CGF_FLEET_INSTALL_DRY_RUN:-0}
push=${CGF_FLEET_PUSH:-0}
host=${1:-${CGF_FLEET_HOST:-}}
checkout=${CGF_FLEET_CHECKOUT:-${XDG_STATE_HOME:-$HOME/.local/state}/cgfried-fleet/trunk}

die()
{
    echo "$prog: $*" >&2
    exit 3
}

[ -n "$host" ] || die "usage: $0 kasumi|hasu|nomad-1"
case $host in
kasumi) hour=01; minute=15 ;;
hasu) hour=01; minute=35 ;;
nomad-1) hour=01; minute=55 ;;
*) die "unsupported fleet host '$host'" ;;
esac
case $dry_run:$push in
0:0 | 0:1 | 1:0 | 1:1) ;;
*) die "CGF_FLEET_INSTALL_DRY_RUN and CGF_FLEET_PUSH must be 0 or 1" ;;
esac
[ -x "$nightly" ] || die "nightly runner is not executable: $nightly"
command -v "$uname_cmd" >/dev/null 2>&1 || die "uname command not found: $uname_cmd"
system=$($uname_cmd -s 2>/dev/null || echo unknown)
machine=$($uname_cmd -m 2>/dev/null || echo unknown)
case $host:$system:$machine in
kasumi:Linux:x86_64 | hasu:Linux:x86_64 | nomad-1:Darwin:arm64 | nomad-1:Darwin:aarch64) ;;
*) die "$host scheduler topology mismatch: got $system $machine" ;;
esac

# Scheduler formats have different escaping rules. Fleet deployment paths are
# deliberately machine-owned and whitespace-free; reject ambiguity up front.
case $nightly:$checkout in
*[!A-Za-z0-9_./:@+-]*) die "nightly and checkout paths must be scheduler-safe (no whitespace or markup)" ;;
esac

render_systemd_service()
{
    cat <<EOF
[Unit]
Description=Cgfried nightly performance measurement ($host)
After=network-online.target

[Service]
Type=oneshot
Environment=CGF_FLEET_HOST=$host
Environment=CGF_FLEET_CHECKOUT=$checkout
Environment=CGF_FLEET_PUSH=$push
Environment=PATH=$HOME/.cargo/bin:/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin
ExecStart=$nightly
EOF
}

render_systemd_timer()
{
    cat <<EOF
[Unit]
Description=Schedule Cgfried nightly performance measurement ($host)

[Timer]
OnCalendar=*-*-* $hour:$minute:00 UTC
Persistent=true
RandomizedDelaySec=0
Unit=cgfried-fleet-perf.service

[Install]
WantedBy=timers.target
EOF
}

render_launch_agent()
{
    cat <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>com.tenseleyflow.cgfried-fleet-perf</string>
  <key>ProgramArguments</key>
  <array><string>$nightly</string></array>
  <key>EnvironmentVariables</key>
  <dict>
    <key>CGF_FLEET_HOST</key><string>$host</string>
    <key>CGF_FLEET_CHECKOUT</key><string>$checkout</string>
    <key>CGF_FLEET_PUSH</key><string>$push</string>
    <key>PATH</key><string>$HOME/.cargo/bin:/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin</string>
  </dict>
  <key>StartCalendarInterval</key>
  <dict><key>Hour</key><integer>${hour#0}</integer><key>Minute</key><integer>${minute#0}</integer></dict>
  <key>StandardOutPath</key><string>${checkout%/*}/fleet-nightly.stdout.log</string>
  <key>StandardErrorPath</key><string>${checkout%/*}/fleet-nightly.stderr.log</string>
</dict>
</plist>
EOF
}

case $system in
Linux)
    unit_dir=${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user
    service=$unit_dir/cgfried-fleet-perf.service
    timer=$unit_dir/cgfried-fleet-perf.timer
    if [ "$dry_run" -eq 1 ]; then
        echo "### $service"
        render_systemd_service
        echo "### $timer"
        render_systemd_timer
        echo "DRY_RUN: $systemctl_cmd --user daemon-reload"
        echo "DRY_RUN: $systemctl_cmd --user enable --now cgfried-fleet-perf.timer"
        exit 0
    fi
    command -v "$systemctl_cmd" >/dev/null 2>&1 || die "systemctl command not found: $systemctl_cmd"
    mkdir -p "$unit_dir"
    render_systemd_service >"$service"
    render_systemd_timer >"$timer"
    "$systemctl_cmd" --user daemon-reload
    "$systemctl_cmd" --user enable --now cgfried-fleet-perf.timer
    echo "$prog: installed user timer $timer ($host at $hour:$minute UTC)"
    ;;
Darwin)
    agent_dir=$HOME/Library/LaunchAgents
    agent=$agent_dir/com.tenseleyflow.cgfried-fleet-perf.plist
    if [ "$dry_run" -eq 1 ]; then
        echo "### $agent"
        render_launch_agent
        echo "DRY_RUN: $launchctl_cmd bootstrap gui/UID $agent"
        exit 0
    fi
    command -v "$launchctl_cmd" >/dev/null 2>&1 || die "launchctl command not found: $launchctl_cmd"
    command -v "$id_cmd" >/dev/null 2>&1 || die "id command not found: $id_cmd"
    mkdir -p "$agent_dir" "${checkout%/*}"
    render_launch_agent >"$agent"
    uid=$($id_cmd -u)
    "$launchctl_cmd" bootout "gui/$uid" "$agent" >/dev/null 2>&1 || true
    "$launchctl_cmd" bootstrap "gui/$uid" "$agent"
    echo "$prog: installed user LaunchAgent $agent ($host at $hour:$minute local time)"
    ;;
*) die "unsupported scheduler platform '$system'" ;;
esac
