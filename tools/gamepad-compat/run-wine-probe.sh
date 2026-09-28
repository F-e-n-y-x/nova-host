#!/bin/bash
# Manual compatibility check: how Wine/Proton's XInput sees a Nova virtual pad.
# Runs xiprobe.exe under GE-Proton (umu) in its own prefix on its own Xvfb display, then creates
# ONE test-only libvirtualhid pad (padscript) that plays each input alone, and logs what XInput
# reports per slot. Note: the test pad is a real uhid device, so every program on the machine can
# see it while it runs; run it when nobody is streaming or playing.
#
# Build: see build.sh. Usage (as root; Proton runs as ayush):
#   run-wine-probe.sh <label> <padscript-binary> <xseries|xone|x360|ds5|ds4> [--silent]
#   XENV="PROTON_SONY_HIDRAW_XINPUT=1" run-wine-probe.sh ...   extra Proton environment
#   WDBG="-all,+hid" run-wine-probe.sh ...                       Wine debug channels
set -u
label=$1; pad=$2; profile=$3; extra=${4:-}
T=${PADTEST_DIR:?set PADTEST_DIR to a scratch directory holding xiprobe.exe}
out=$T/out/$label; rm -rf "$out"; mkdir -p "$out"; chown ayush:ayush "$out"
cp $T/xiprobe.exe "$out/"; chown ayush:ayush "$out/xiprobe.exe"
PFX=$T/prefix; mkdir -p $PFX; chown ayush:ayush $PFX
DISP=:57
su ayush -c "Xvfb $DISP -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1 & echo \$! > $out/xvfb.pid"
sleep 2
WDBG=${WDBG:-}; su ayush -c "cd '$out' && env -i WDBG=$WDBG HOME=/home/ayush USER=ayush PATH=/usr/bin:/bin DISPLAY=$DISP XDG_RUNTIME_DIR=/run/user/1000 \
  WINEPREFIX=$PFX PROTONPATH=${PROTONPATH_DIR:-/home/ayush/.local/share/Steam/compatibilitytools.d/GE-Proton11-7-x86_64} GAMEID=umu-default STORE=none \
  UMU_RUNTIME_UPDATE=0 ${XENV:-} PROTON_LOG=1 PROTON_LOG_DIR='$out' WINEDEBUG=${WDBG:--all,warn+hid} \
  timeout 150 umu-run ./xiprobe.exe > '$out/umu.log' 2>&1 &"
for i in $(seq 1 120); do grep -q "xiprobe ready" "$out/xiprobe.log" 2>/dev/null && break; sleep 1; done
grep -q "xiprobe ready" "$out/xiprobe.log" 2>/dev/null || { echo "probe never became ready"; tail -20 "$out/umu.log"; }
$pad $profile $extra 6000 > "$out/pad.log" 2>&1
sleep 5
pkill -u ayush -f xiprobe.exe; sleep 2
su ayush -c "env WINEPREFIX=$PFX ${PROTONPATH_DIR:-/home/ayush/.local/share/Steam/compatibilitytools.d/GE-Proton11-7-x86_64}/files/bin/wineserver -k" 2>/dev/null
kill $(cat $out/xvfb.pid) 2>/dev/null
echo "== $label"; cat "$out/pad.log" | head -3; cat "$out/xiprobe.log" | head -60
