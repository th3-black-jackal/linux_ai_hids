#!/usr/bin/env bash
# Tier 1 -- run the cross-built armhf binary on an x86 host via qemu-user.
#
# Validates: cross-compile toolchain, armhf ABI, and that float32 inference on
# ARM yields the SAME predictions as x86. Does NOT validate collectors -- the
# kernel underneath is still the x86_64 host, so perf tracepoints and the *32
# syscall names are the host's, not the target's. That is Tier 2.
#
# Prereqs (Debian/Ubuntu host):
#   apt-get install g++-arm-linux-gnueabihf qemu-user-static libc6-dev-armhf-cross
#
# Usage:
#   scripts/tier1-qemu-user.sh <model.bin> <csv-or-dir> [--raw]
set -euo pipefail

MODEL="${1:?usage: tier1-qemu-user.sh <model.bin> <csv-or-dir> [--raw]}"
DATA="${2:?need a CSV file or dataset directory}"
RAW="${3:-}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SYSROOT=/usr/arm-linux-gnueabihf
QEMU="qemu-arm-static -L ${SYSROOT}/"

cd "$ROOT"

echo "== cross-building armhf =="
# Wipe first: a toolchain file only applies on the initial configure of a build
# dir, so reusing one first configured natively would silently build x86.
rm -rf build-armhf
cmake -B build-armhf -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-armhf.cmake \
      -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-armhf -j >/dev/null
file build-armhf/hids_replay | sed 's/^/  /'

echo "== armhf report (under qemu-user) =="
$QEMU ./build-armhf/hids_replay "$MODEL" "$DATA" $RAW

echo "== agreement check: armhf(qemu) vs oracle =="
$QEMU ./build-armhf/hids_replay "$MODEL" "$DATA" $RAW --dump \
    | awk '{print $1,$3}' > /tmp/arm_preds.txt
python3 tools/bin_oracle.py "$MODEL" "$DATA" $RAW --dump \
    | awk '{print $1,$3}' > /tmp/orc_preds.txt

if diff -q /tmp/arm_preds.txt /tmp/orc_preds.txt >/dev/null; then
    echo "  PASS: armhf predictions identical to reference ($(wc -l < /tmp/arm_preds.txt) rows)"
else
    echo "  FAIL: divergence (first rows):"
    diff /tmp/arm_preds.txt /tmp/orc_preds.txt | head
    exit 1
fi
