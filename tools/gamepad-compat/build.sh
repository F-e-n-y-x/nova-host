#!/bin/bash
# Build the probe (Windows, freestanding: clang + lld-link, no Windows SDK) and the test pad
# (Linux, against a libvirtualhid source tree with Nova's patches applied).
# Usage: build.sh <out-dir> <libvirtualhid-source>
set -eu
out=$1; lvh=$2; here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"
llvm-dlltool -m i386:x86-64 -d "$here/kernel32.def" -l "$out/kernel32.lib"
clang --target=x86_64-pc-windows-msvc -O1 -ffreestanding -fno-builtin -fno-stack-protector -mno-stack-arg-probe \
  -c "$here/xiprobe.c" -o "$out/xiprobe.obj"
lld-link /subsystem:console /entry:start /nodefaultlib /out:"$out/xiprobe.exe" "$out/xiprobe.obj" "$out/kernel32.lib"
cmake -S "$here" -B "$out/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DLVH_SOURCE="$lvh"
cmake --build "$out/build"
