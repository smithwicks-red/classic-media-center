#!/bin/sh
# run_mc04_tests.sh -- HOST ONLY. Media Center 0.4 proof through the REAL app
# logic (mc02_host = mc_fetch.c + mc_core.c + H264Stream.c + AVCReorder.c +
# https_client.c, fake Toolbox/OT) against mc02_server.py:
#   B:  every B-frame file at decoder level 1: stream (reference movie) and
#       download-then-play (local movie); copies both into RESDIR for
#       mc04_proof_b.py
#   D:  the same files at level 0: refused exactly as 0.3
#   G:  gate cases (High 10, High 4:2:2, interlaced, non-B Main/High) and
#       list marks at both levels
#   sh host/run_mc04_tests.sh [asan|plain] CLIPDIR BFILEDIR EXTRADIR RESDIR
# CLIPDIR = the 0.2 clips (base/end/high/other/big.mp4); BFILEDIR = the
# B-frame files; EXTRADIR = nb_*.mp4/.mov (no B-frames), p_*.mp4
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
V=${1:-asan}; CLIPS=$(cd "${2:?clipdir}" && pwd)
BF=$(cd "${3:?bfiledir}" && pwd); EX=$(cd "${4:?extradir}" && pwd)
mkdir -p "${5:?resdir}"; RES=$(cd "$5" && pwd)
XF=""; NAMES=""
for f in $(ls $BF) $(ls $EX); do
  [ -f $BF/$f ] && p=$BF/$f || p=$EX/$f
  XF="$XF${XF:+,}$p"; NAMES="$NAMES $f"
done
export MC02_XFILES=$XF
V=${1:-asan}; CLIPS=$(cd "${2:?clipdir}" && pwd)
RUNNER=${RUNNER:-}
BIN=$HERE/build-$V/mc02_host
[ -x "$BIN" ] || sh "$HERE/build_mc02.sh" $V >/dev/null 2>&1 || { echo "build failed"; exit 1; }
W=$(mktemp -d /tmp/mc02test.XXXXXX)
B=${BASE_PORT:-19543}
export ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
cd $W
q() { "$@" >/dev/null 2>&1; }
q openssl ecparam -name prime256v1 -genkey -noout -out ca.key
q openssl req -x509 -new -key ca.key -sha256 -days 30 -subj "/CN=mc02 test CA" \
  -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" -out ca.pem
openssl x509 -in ca.pem -outform DER -out ca.der 2>/dev/null
mk() {
  q openssl ecparam -name prime256v1 -genkey -noout -out $1.key
  q openssl req -new -key $1.key -subj "/CN=localhost" -out $1.csr
  printf "subjectAltName=DNS:localhost,IP:127.0.0.1\nbasicConstraints=CA:FALSE\nextendedKeyUsage=serverAuth\nkeyUsage=digitalSignature\n" > $1.ext
  if [ $2 = self ]; then q openssl x509 -req -in $1.csr -signkey $1.key -sha256 -days 30 -extfile $1.ext -out $1.pem
  else q openssl x509 -req -in $1.csr -CA ca.pem -CAkey ca.key -CAcreateserial -sha256 -days 30 -extfile $1.ext -out $1.pem; fi
}
mk srv ca; mk self self
export HOST_TEST_CA=$W/ca.der
python3 -I "$HERE/mc02_server.py" $W $CLIPS $B > $W/server.log 2>&1 &
SP=$!
trap 'kill $SP 2>/dev/null' EXIT
sleep 1
export MC_API=https://localhost:$B MC_DL=http://localhost:$((B+1)) MC_HTTP_PORT=$((B+1)) MC_HTTPS_PORT=$B OUT_DIR=$W
PASS=0; FAIL=0
ok() { PASS=$((PASS+1)); echo "PASS $1"; }
no() { FAIL=$((FAIL+1)); echo "FAIL $1"; tail -5 $W/out.txt; }
run() { # dir args...
  D=$1; shift; mkdir -p $W/$D; FAKE_MAC_DIR=$W/$D $RUNNER $BIN "$@" > $W/out.txt 2>$W/err.txt; RC=$?
  cp $W/out.txt $W/$D.log; grep -q 'ERROR: AddressSanitizer\|runtime error' $W/err.txt && { echo "SANITIZER in $D"; cat $W/err.txt | head -20; FAIL=$((FAIL+1)); }
  return $RC; }
has() { grep -q -- "$1" $W/out.txt; }
DL="App/Media Center Downloads"
nofiles() { [ -z "$(ls "$W/$1/$DL" 2>/dev/null)" ]; }
idx() { i=0; for n in $NAMES; do [ "$n" = "$1" ] && { echo $i; return; }; i=$((i+1)); done; }

# ---------------- B: B-frame files, level 1, stream + download ----------------
for f in $(ls $BF); do
  x=xb$(idx $f)
  MC_DECODER_LEVEL=1 run s_$x $x file 1; R=$?
  if [ $R = 0 ] && has 'RESULT rc=0 mode=0' && has 'MARK 1 fact:plays-now' && has 'B-frames: reorder delay N' \
     && has OPEN_REF && [ -f $W/$x.mov ]; then cp $W/$x.mov $RES/$f.stream.mov
     ok "B stream   $f: $(grep -o 'reorder delay N = [0-9]*' $W/out.txt) $(grep -o '^  [A-Z][a-z]* H.264.*' $W/out.txt | head -1)"
  else no "B stream $f"; fi
  MC_DECODER_LEVEL=1 MC_BIG_KB=1000000 run d_$x $x file 1 download confirm; R=$?
  L="$W/d_$x/$DL/$x.mov"
  if [ $R = 0 ] && has 'RESULT rc=0 mode=1' && has 'B-frames: reorder delay N' && has OPEN_LOCAL && [ -f "$L" ]; then
     cp "$L" $RES/$f.local.mov; ok "B download $f: $(grep -o 'Downloaded.*s)' $W/out.txt | head -1)"
  else no "B download $f"; fi
  # D: level 0 -> exactly the 0.3 refusal
  MC_DECODER_LEVEL=0 run z_$x $x file 1 download confirm
  has 'RESULT rc=-1 mode=-1' && has 'MARK 1 fact:needs-newer-decoder' && has 'this decoder plays Baseline only.' \
     && ! has OPEN_ && ! has 'Downloading over' && ! has 'B-frames' && nofiles z_$x \
     && ok "D level 0  $f: $(grep -o 'This video is .*' $W/out.txt | head -1)" || no "D level 0 $f"
done

# ---------------- G: gate cases ----------------
gate() { # level file expect-rc grep...
  lv=$1; f=$2; rc=$3; shift 3; x=xb$(idx $f)
  MC_DECODER_LEVEL=$lv run g${lv}_$x $x file 1
  has "RESULT rc=$rc" || { no "G L$lv $f rc"; return; }
  for g in "$@"; do has "$g" || { no "G L$lv $f '$g'"; return; }; done
  ok "G L$lv $f -> $(grep -o 'MARK 1 [a-z:-]*' $W/out.txt) | $(grep -o '^  [A-Z][^.]*\.' $W/out.txt | head -1)"
  [ $rc = 0 ] && cp $W/$x.mov $RES/$f.L$lv.stream.mov
}
gate 1 p_high10.mp4 -1 'MARK 1 fact:will-not-play' '10-bit (High 10 profile); this decoder plays 8-bit video only.'
gate 1 p_high422.mp4 -1 'MARK 1 fact:will-not-play' '4:2:2 colour (High 4:2:2 profile); this decoder plays 4:2:0 colour only.'
gate 1 p_interlaced.mp4 0 'MARK 1 fact:plays-now' 'interlaced: untested' 'B-frames: reorder delay N' OPEN_REF
gate 0 p_interlaced.mp4 -1 'MARK 1 fact:needs-newer-decoder' 'plays Baseline only.'
for f in $(ls $EX | grep '^nb_'); do
  case $f in nb_base*) gate 0 $f 0 'MARK 1 fact:plays-now' OPEN_REF ;;
             *) gate 0 $f -1 'MARK 1 fact:needs-newer-decoder' 'plays Baseline only.' ;; esac
  gate 1 $f 0 'MARK 1 fact:plays-now' OPEN_REF
  MC_DECODER_LEVEL=1 run gn_$f xb$(idx $f) file 1; has 'B-frames' && no "G $f: non-B file was rewritten" || ok "G $f: no B-frame rewrite"
done
x=xb$(idx nb_high.mp4)
MC_DECODER_LEVEL=1 run l1 $x list && has "FILE 1 hint:plays-now mb=" && has "fmt=h.264 IA" && ok "G list level 1: h.264 IA -> hint:plays-now" || no "G list L1"
MC_DECODER_LEVEL=0 run l0 $x list && has "FILE 1 hint:needs-newer-decoder mb=" && ok "G list level 0: h.264 IA -> hint:needs-newer-decoder (0.3)" || no "G list L0"
echo "mc04: $PASS passed, $FAIL failed   (logs in $W)"
[ $FAIL = 0 ]
