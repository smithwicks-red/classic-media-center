#!/bin/sh
# run_mc02_tests.sh -- HOST ONLY. Media Center 0.2 proof: https search /
# metadata / moov, the stream-vs-download decision, download-then-play,
# reuse, cancel, server drop, disk full, bad certificate, 1956 clock.
#   sh host/run_mc02_tests.sh [asan|plain|ppc] CLIPDIR
# CLIPDIR holds base.mp4 end.mp4 high.mp4 other.mp4 big.mp4 (see BUILD_NOTES_02.md)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
V=${1:-asan}; CLIPS=$(cd "${2:?clipdir}" && pwd)
RUNNER=${RUNNER:-}
BIN=$HERE/build-$V/mc02_host
[ -x "$BIN" ] || sh "$HERE/build_mc02.sh" $V >/dev/null 2>&1 || { echo "build failed"; exit 1; }
W=$(mktemp -d /tmp/mc02test.XXXXXX)
B=${BASE_PORT:-19443}
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

run t1 httpok && has 'RESULT rc=0 mode=0' && has 'TLS 1.3' && has 'OPEN_REF' \
  && grep -aq "http://localhost:$((B+1))/download/httpok/httpok_512kb.mp4" $W/httpok_512kb.mov \
  && ! has 'Downloading over' && ok "T1 https search+metadata, http chain -> stream (reference movie)" || no T1

run t2 httpsonly && has 'RESULT rc=0 mode=1' && has 'insists on https' && has OPEN_LOCAL \
  && python3 -I $HERE/verify_local_movie.py $CLIPS/base.mp4 "$W/t2/$DL/httpsonly_512kb.mov" > $W/v2.txt \
  && ok "T2 https-only chain -> download -> valid local movie ($(grep -c '^OK' $W/v2.txt) checks)" || { no T2; cat $W/v2.txt; }

run t3 httpok download && has 'RESULT rc=0 mode=1 reused=0' && has 'Download first' \
  && python3 -I $HERE/verify_local_movie.py $CLIPS/base.mp4 "$W/t3/$DL/httpok_512kb.mov" > $W/v3.txt \
  && ok "T3 forced download (node serves http) -> valid local movie" || no T3

run t3 httpok download && has 'RESULT rc=0 mode=1 reused=1' && has 'Already downloaded' && ! has 'Downloading over' \
  && ok "T4 rerun reuses the complete file, no download" || no T4

run t5 moovend download && has 'RESULT rc=0 mode=1' && grep -q 'bytes [0-9]* onward' $W/out.txt \
  && python3 -I $HERE/verify_local_movie.py $CLIPS/end.mp4 "$W/t5/$DL/moovend_512kb.mov" > $W/v5.txt \
  && ok "T5 moov-at-end: Range probe + download -> valid local movie" || no T5

run t6 high download; has 'RESULT rc=-1 mode=-1' && has 'High' && ! has 'Downloading over' && nofiles t6 \
  && ok "T6 High profile refused before any download" || no T6

run t7 changed download; has 'RESULT rc=-1' && has 'different file' && nofiles t7 && has 'Partial file removed' \
  && ok "T7 file changed between check and download -> refused, partial removed" || no T7

run t8 cutdrop download; has 'RESULT rc=-1' && has 'Partial file removed' && nofiles t8 \
  && ok "T8 server drops mid-download -> no partial file ($(grep -o 'msg=.*' $W/out.txt | head -c 70))" || no T8

CANCEL_AT_KB=300 run t9 slow download; has 'RESULT rc=-1' && has 'msg=Stopped.' && has 'Cmd-period' && nofiles t9 \
  && ok "T9 Cmd-period during download -> stopped, no partial file" || no T9

FAKE_FREE_KB=6000 run t10 httpok download; has 'RESULT rc=-1' && has 'Not enough disk space' && ! has 'Downloading over' && nofiles t10 \
  && ok "T10 disk full -> refused before downloading ($(grep -o 'msg=.*' $W/out.txt | head -c 90))" || no T10

MC_API=https://localhost:$((B+2)) run t11 httpok; has 'RESULT rc=-1' && has 'msg=Certificate check failed' \
  && ok "T11 untrusted certificate -> $(grep -o 'msg=.*' $W/out.txt | head -c 90)" || no T11

MC_API=https://localhost:$((B+2)) run t12 httpok insecure && has 'RESULT rc=0 mode=0' \
  && ok "T12 same with insecure keyword -> works" || no T12

FAKE_MAC_SECONDS=1641000000 run t13 httpok; has 'RESULT rc=-1' && has 'clock is set to 1956' \
  && ok "T13 clock 1956 -> $(grep -o 'msg=.*' $W/out.txt | head -c 80)" || no T13

if [ "${BIG:-1}" = 1 ]; then
run t14 big download && has 'RESULT rc=0 mode=1' \
  && python3 -I $HERE/verify_local_movie.py $CLIPS/big.mp4 "$W/t14/$DL/big_512kb.mov" > $W/v14.txt \
  && ok "T14 19.5 MB download: $(grep -o 'Downloaded.*s)' $W/out.txt)" || no T14
fi

# ---------------- 0.3: search all movies, file list, hook keywords ----------------
cnt() { grep -c -- "$1" $W/out.txt; }
run t15 "jimmy and the federation gang" list && has 'RESULT rc=0 mode=-1' \
  && grep -q '^title:(jimmy AND and AND the AND federation AND gang) AND mediatype:movies$' $W/queries.txt \
  && has 'FILES 30 JimmyAndTheFederationGang' && [ "$(grep -c '^FILE [0-9]* hint:plays-now mb=' $W/out.txt)" = 30 ] \
  && grep -q '^FILE 1 hint:plays-now mb=376.4 fmt=h.264 name=1x01-TheCage.mp4 label=1x01 - The Cage$' $W/out.txt \
  && grep -q '^FILE 10 .*name=1x10-' $W/out.txt && grep -q '^FILE 30 .*label=1x30 - Operation Annihilate!$' $W/out.txt \
  && ! has OPEN_ && ! has MARK && ok "T15 list: AND query, 700 KB metadata -> 30 files, natural order, hint:plays-now" || no T15

run t16 '"Jimmy and the Federation Gang"' list && has 'RESULT rc=0' \
  && grep -q '^title:("Jimmy and the Federation Gang") AND mediatype:movies$' $W/queries.txt \
  && ok "T16 quoted phrase kept as a phrase" || no T16

run t17 "jimmy and the federation gang" file 2 && has 'RESULT rc=0 mode=0' && has 'MARK 2 fact:plays-now' \
  && has 'Trying 1x02-WhereNoManHasGoneBefore.mp4' && has OPEN_REF && ! has 'Downloading over' \
  && ok "T17 file 2 -> Baseline check -> MARK fact:plays-now -> streaming hand-off" || no T17

run t18 jimmyhttps file 2; has 'RESULT rc=-1 mode=-1 reused=0 bytes=0 confirm=1' && has 'MARK 2 fact:plays-now' \
  && has 'CONFIRM needed: 298.7 MB, about 12 min at 430 KB/s' && has 'confirm' && ! has 'Downloading over' && nofiles t18 \
  && ok "T18 https-only 299 MB file without confirm -> refused before download ($(grep -o 'msg=.*' $W/out.txt | head -c 60)...)" || no T18

run t18 jimmyhttps file 2 download; has 'RESULT rc=-1' && has 'confirm=1' && ! has 'Downloading over' && nofiles t18 \
  && ok "T19 'download' alone does not allow a big download" || no T19

MC_BIG_KB=1000 run t20 httpsonly; has 'RESULT rc=-1' && has 'confirm=1' && has 'CONFIRM needed: 1.8 MB' && nofiles t20 \
  && ok "T20 0.2 path (play hit 1) also asks before a big forced download" || no T20

MC_BIG_KB=1000 run t21 httpsonly confirm && has 'RESULT rc=0 mode=1' && has OPEN_LOCAL \
  && python3 -I $HERE/verify_local_movie.py $CLIPS/base.mp4 "$W/t21/$DL/httpsonly_512kb.mov" > $W/v21.txt \
  && ok "T21 same with confirm -> downloads, valid local movie" || no T21

run t22 iahigh list && has 'FILES 2 iahigh' && has 'FILE 1 hint:needs-newer-decoder mb=1.8 fmt=h.264 IA name=clip.ia.mp4 label=Clip (IA)' \
  && has 'FILE 2 hint:needs-newer-decoder mb=1.8 fmt=MPEG4 name=clip.mp4 label=clip' \
  && ok "T22 IA/MPEG4 files listed with hint:needs-newer-decoder" || no T22

run t23 iahigh file 1 download confirm; has 'RESULT rc=-1 mode=-1' && has 'MARK 1 fact:needs-newer-decoder' && has 'High' \
  && ! has OPEN_ && ! has 'Downloading over' && nofiles t23 \
  && ok "T23 .ia.mp4 (High) refused 'needs newer decoder', never handed to QuickTime, even with download+confirm" || no T23

run t24 notitle && has 'RESULT rc=0 mode=0' && has 'searching every field' && has 'scope=any' \
  && grep -q '^(notitle) AND mediatype:movies$' $W/queries.txt \
  && ok "T24 nothing in titles -> every-field search -> plays" || no T24

run t25 "jimmy and the federation gang" file 31; has 'RESULT rc=-1' && has 'There is no file 31: this item lists 30.' && ! has OPEN_ \
  && ok "T25 file N out of range -> clear message" || no T25

run t26 "popeye president" && has 'RESULT rc=0 mode=0' && has 'scope=title' && has OPEN_REF \
  && ok "T26 an old 0.2-style query still plays hit 1 the 0.2 way" || no T26
echo "mc02: $PASS passed, $FAIL failed   (logs in $W)"
[ $FAIL = 0 ]
