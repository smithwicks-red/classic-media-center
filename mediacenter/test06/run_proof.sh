#!/bin/sh
# Media Center 0.6 skin: host proofs (U1b's run_proof.sh, moved to
# mediacenter/test06; sources now in mediacenter/).
#  1. gcc -std=c89 -pedantic -Wall -Wextra -Werror build (x86-64)
#  2. self-test: tiled (dirty-rect) re-render == full render, every scene
#  3. render twice -> identical; compare with golden.txt
#  4. big-endian: the same source cross-built for 32-bit PowerPC, run under
#     qemu-ppc: crcs must equal golden.txt, .xrgb/.runs byte-identical
#  5. key-map test (host, and under qemu-ppc)
#  6. PNGs (Python + TTF text) -> $OUT (default ../../out/host_png)
#  7. "MC dump N" files (-d) -> dump2png.py -> PNG pixels == .xrgb pixels
set -e
cd "$(dirname "$0")"
OUT=${OUT:-../../out/host_png}
RAW=${RAW:-/tmp/mc06_raw}
CF="-std=c89 -pedantic -Wall -Wextra -Werror -O2 -I.."
SRC="mc_skin_render.c ../mc_demo.c ../mc_layout.c ../mc_skin.c"
gcc $CF -o /tmp/mc_skin_render $SRC
echo "== 1. x86-64 build clean"
gcc $CF -o /tmp/mc_key_test mc_key_test.c ../mc_layout.c ../mc_skin.c
/tmp/mc_key_test
echo "== 1b. key-map test passes (x86-64)"
/tmp/mc_skin_render -t > /tmp/mc_t.txt
echo "== 2. tiled == full: ok ($(wc -l < /tmp/mc_t.txt) scenes)"
mkdir -p $RAW
/tmp/mc_skin_render $RAW > /tmp/mc_a.txt
/tmp/mc_skin_render $RAW > /tmp/mc_b.txt
cmp /tmp/mc_a.txt /tmp/mc_b.txt || { echo "FAIL 3a"; exit 1; }; echo "== 3a. two renders identical"
if [ "$1" = "--bless" ] || [ ! -f golden.txt ]; then cp /tmp/mc_a.txt golden.txt; echo "   golden.txt written"; fi
cmp /tmp/mc_a.txt golden.txt || { echo "FAIL 3b: differs from golden.txt"; exit 1; }; echo "== 3b. matches golden.txt"
if command -v powerpc-linux-gnu-gcc >/dev/null && command -v qemu-ppc >/dev/null; then
    powerpc-linux-gnu-gcc $CF -static -o /tmp/mc_skin_render.ppc $SRC
    file /tmp/mc_skin_render.ppc | sed 's/^/   /'
    qemu-ppc /tmp/mc_skin_render.ppc -t > /tmp/mc_ppc.txt
    cmp /tmp/mc_ppc.txt golden.txt || { echo "FAIL 4: ppc differs from golden.txt"; exit 1; }; echo "== 4. qemu-ppc (big-endian) crcs == golden.txt, tiled == full there too"
    rm -rf /tmp/mc_ppc_raw; mkdir -p /tmp/mc_ppc_raw
    qemu-ppc /tmp/mc_skin_render.ppc /tmp/mc_ppc_raw > /dev/null
    for f in $RAW/*.xrgb $RAW/*.runs; do cmp "$f" "/tmp/mc_ppc_raw/$(basename "$f")"; done
    echo "   every .xrgb/.runs file byte-identical between x86-64 and ppc"
    powerpc-linux-gnu-gcc $CF -static -o /tmp/mc_key_test.ppc mc_key_test.c ../mc_layout.c ../mc_skin.c
    qemu-ppc /tmp/mc_key_test.ppc || { echo "FAIL 4b"; exit 1; }; echo "== 4b. key-map test passes under qemu-ppc"
else
    echo "== 4. SKIPPED: no powerpc-linux-gnu-gcc / qemu-ppc"
fi
python3 -I mc_mock.py $RAW $OUT
echo "== 6. PNGs written to $OUT"
rm -rf /tmp/mc06_dumps; mkdir -p /tmp/mc06_dumps
qemu-ppc /tmp/mc_skin_render.ppc -d /tmp/mc06_dumps > /dev/null 2>&1 || /tmp/mc_skin_render -d /tmp/mc06_dumps > /dev/null
python3 -I dump2png.py --check $RAW /tmp/mc06_dumps/MC\ dump\ *
echo "== 7. dump2png: every \"MC dump N\" (rowBytes 1984, big-endian writer) == its .xrgb"
