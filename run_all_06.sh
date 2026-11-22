#!/bin/sh
# run_all_06.sh -- every host proof of Media Center 0.6 (job U3 stage A),
# from a fresh unpack of b25c-mediacenter-0.6-src.tgz.
#   sh run_all_06.sh CLIPDIR [BASE_0.5u_MediaCenter.c]
# CLIPDIR: base.mp4 end.mp4 high.mp4 other.mp4 big.mp4 (recipe in
# mediacenter/BUILD_NOTES_02.md; the e2e reads base.mp4). Needs gcc with
# -m32 multilib, powerpc-linux-gnu-gcc, qemu-ppc, python3 + PIL, ffmpeg (clips).
set -e
T=$(cd "$(dirname "$0")" && pwd)
CLIPS=${1:?CLIPDIR}
BASE=${2:-$T/../../base/b24-mediacenter-0.5u/mediacenter/MediaCenter.c}
echo "### 1. controller (mc_ui.c): x86-64 ASan+UBSan and qemu-ppc"
sh $T/mediacenter/test06/run_ui_test.sh
echo "### 2. skin proofs (U1b run_proof, moved): key map, tiled==full, golden, ppc, PNGs, dumps"
OUT=${OUT:-$T/host_png} sh $T/mediacenter/test06/run_proof.sh | grep -E "^==|checks|FAIL"
echo "### 3. 0.5u suites (mc_core/mc_fetch changed: stutter note, progress callback)"
sh $T/mediacenter/run_tests.sh 2>&1 | grep "passed"
sh $T/mediacenter/test05t/run_test_mcn.sh 2>&1 | tail -1
sh $T/mediacenter/test05/run_mc05_unit.sh 2>&1 | tail -1
echo "### 4. MediaCenter.c 0.6 against the Toolbox stubs"
sh $T/tls18/host/syntax_mc06.sh "$BASE"
echo "### 5. whole-app streaming e2e (mc05_host, the app's fetch/stream/proxy code)"
rm -rf $T/tls18/host/build-asan
# fresh ports: back-to-back runs on 19643/18080 flaked here (sockets of the
# previous run still bound); 0.5u and 0.6 both give 59/0 on unused ports
BASE_PORT=${BASE_PORT:-22643} PROXY_PORT=${PROXY_PORT:-22080} \
  sh $T/tls18/host/run_mc05_tests.sh "$CLIPS" | tail -1
