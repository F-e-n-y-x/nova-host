#!/bin/bash
# Live remote text context test on a spare headless Xvfb only (never :0): openbox, a private D-Bus
# Uses the isolated test runner tc-test.sh from the Nova-Nebula work folder (temp HOME, config checksum).
# session (its own at-spi bus) and a GTK test app that moves its own focus. No input is injected.
D=/DATA/blue/Projects/Nova-Nebula/nova/work
T=$(mktemp -d $D/tclive.XXXX) && [ -n "$T" ] && [ -d "$T" ] || exit 1
case "${T:-}" in $D/tclive.*) ;; *) echo "unsafe temp dir" >&2; exit 1 ;; esac
mkdir -p $T/run $T/home $T/session; chmod 700 $T/run
N=97; while [ -e /tmp/.X11-unix/X$N ] || [ -e /tmp/.X$N-lock ]; do N=$((N+1)); done
Xvfb :$N -nolisten tcp -screen 0 1280x720x24 >$T/xvfb.log 2>&1 &
XP=$!
cleanup() { echo quit:9 > $T/cmd; sleep 1; kill $APP $WM $XP 2>/dev/null; [ -n "$BUSPID" ] && kill $BUSPID 2>/dev/null; cp $T/report.txt $D/tc-live-report.txt 2>/dev/null; rm -rf "$T"; }
trap cleanup EXIT
sleep 1
export DISPLAY=:$N XDG_RUNTIME_DIR=$T/run HOME=$T/home
unset XAUTHORITY WAYLAND_DISPLAY AT_SPI_BUS_ADDRESS NO_AT_BRIDGE
export GIO_USE_VFS=local GIO_USE_VOLUME_MONITOR=unix GTK_USE_PORTAL=0
# A private session bus, like nova-vd-session's dbus-run-session.
eval "$(dbus-launch --sh-syntax)"; BUSPID=$DBUS_SESSION_BUS_PID
openbox >$T/wm.log 2>&1 & WM=$!
sleep 1
TC_DIR=$T python3 $(dirname "$(readlink -f "$0")")/text_context_field_app.py >$T/app.log 2>&1 & APP=$!
for i in $(seq 1 50); do [ -f $T/geometry.json ] && break; sleep 0.2; done
echo "geometry: $(cat $T/geometry.json)"
printf '{"DBUS_SESSION_BUS_ADDRESS": "%s"}' "$DBUS_SESSION_BUS_ADDRESS" > $T/session/app-env.json
echo "AT_SPI_BUS on :$N: $(xprop -root AT_SPI_BUS 2>&1)"
GTEST_BRIEF=--gtest_brief=0 $D/tc-test.sh 'TextContextLive.DesktopDisplayOwnBus' DISPLAY=:$N DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS XDG_RUNTIME_DIR=$T/run NOVA_TC_LIVE_DIR=$T; r1=$?
# Virtual display layout: no session bus in Nova's environment; found through app-env.json.
GTEST_BRIEF=--gtest_brief=0 $D/tc-test.sh 'TextContextLive.VirtualDisplaySessionBus' DISPLAY=:$N XDG_RUNTIME_DIR=$T/run NOVA_TC_LIVE_DIR=$T NOVA_TC_SESSION_DIR=$T/session; r2=$?
echo "app log:"; cat $T/app.log
echo "desktop rc=$r1 virtual-display rc=$r2"
exit $(( r1 || r2 ))
