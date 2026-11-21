#!/bin/sh
# run_mc04_builder_tests.sh -- HOST ONLY (0.4). Builds the BuildReferenceMovie
# harness (mediacenter/mc04_build_hosttest.c) twice, ASan+UBSan: against the
# 0.3 sources (build03) and the 0.4 sources (build04, MC_DECODER_LEVEL), then
#   c: non-B files: 0.4 at level 1 == 0.3, byte for byte (url moov, download
#      moov + layout); plus the real download walker (mc02_host --walk of the
#      0.3 rig vs this rig at level 1)
#   e: rewriter edge files at level 1: no sanitizer report; each output either
#      == 0.3's, or rewritten (log says so) with tables equal to the Python
#      reference and url moov == download moov except 'dref'
# usage: sh host/run_mc04_builder_tests.sh DIR03 RIG03 OUTDIR "NONB FILES" "EDGE FILES"
#   DIR03 = the 0.3 mediacenter/ dir; RIG03 = the 0.3 mc02_host binary
set -u
HERE=$(cd "$(dirname "$0")" && pwd); MC=$(cd "$HERE/../../mediacenter" && pwd)
D03=$(cd "$1" && pwd); RIG03=$2; mkdir -p "$3"; O=$(cd "$3" && pwd)
CC=${CC:-gcc}; SAN="-g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"
mkdir -p $O/h03 $O/h04; cp $MC/mc04_build_hosttest.c $O/h03/; cp $MC/mc04_build_hosttest.c $O/h04/
$CC -std=c89 -w $SAN -I$D03 $O/h03/mc04_build_hosttest.c $D03/mc_core.c -o $O/build03 || exit 1
$CC -std=c89 -w $SAN -DMC04 -I$MC $O/h04/mc04_build_hosttest.c $MC/mc_core.c $MC/AVCReorder.c -o $O/build04 || exit 1
export ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
URL=http://localhost:19544/download/x/x.mp4
P=0; F=0; ok() { P=$((P+1)); echo "PASS $1"; }; no() { F=$((F+1)); echo "FAIL $1"; }
san() { grep -q 'AddressSanitizer\|runtime error' $O/err; }
for f in $4; do b=$(basename $f)
  for u in $URL -; do
    $O/build03 $f $u $O/c03 e > /dev/null 2>&1; r3=$?
    MC_DECODER_LEVEL=1 $O/build04 $f $u $O/c04 e > $O/log 2> $O/err; r4=$?
    [ $r3 = 0 ] && [ $r4 = 0 ] && ! san && ! grep -q B-frame $O/log && cmp -s $O/c03 $O/c04 \
      && ok "c $b $( [ $u = - ] && echo download || echo stream) moov: identical to 0.3 ($(stat -c %s $O/c04) bytes)" || no "c $b $u"
  done
  $RIG03 --walk $f $O/w03 > /dev/null 2>&1; w3=$?
  MC_DECODER_LEVEL=1 $HERE/build-asan/mc02_host --walk $f $O/w04 > /dev/null 2> $O/err; w4=$?
  [ $w3 = 0 ] && [ $w4 = 0 ] && ! san && cmp -s $O/w03 $O/w04 && ok "c $b walker (download file): identical to 0.3" || no "c $b walker w3=$w3 w4=$w4"
done
: > $O/edge.jsonl
for f in $5; do b=$(basename $f); res=""
  $O/build03 $f $URL $O/u03 > /dev/null 2>&1; $O/build03 $f - $O/d03 e > /dev/null 2>&1
  MC_DECODER_LEVEL=1 $O/build04 $f $URL $O/u04 > $O/log 2> $O/err; r1=$?; san && { no "e $b sanitizer"; continue; }
  MC_DECODER_LEVEL=1 $O/build04 $f - $O/d04 e >> $O/log 2> $O/err; r2=$?; san && { no "e $b sanitizer"; continue; }
  v=$(grep -o 'VERDICT.*' $O/log | head -1 | cut -c1-90)
  if cmp -s $O/u03 $O/u04 && cmp -s $O/d03 $O/d04 && [ $r1 = $r2 ]; then
    ok "e $b: exactly as 0.3 ($(grep -o 'B-frame rewrite not done ([^)]*)' $O/log | head -1 || true)) [$v]"
  elif grep -q 'B-frames: reorder delay' $O/log; then
    j=$(python3 $HERE/mc04_edge_check.py $f $O/u04 $O/d04 /home/claude/s5/rw/edge/A_$b); echo "$j" >> $O/edge.jsonl
    echo "$j" | grep -q '"url_eq_dl_but_dref": true' && echo "$j" | grep -q '"tables_equal": true' \
      && ok "e $b: rewritten, $(grep -o 'reorder delay N = [0-9]*' $O/log | head -1), tables == Python reference [$v]" || no "e $b: $j"
  else no "e $b: differs from 0.3 without a rewrite"; fi
done
echo "mc04 builder: $P passed, $F failed"; [ $F = 0 ]
