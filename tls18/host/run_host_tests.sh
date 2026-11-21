#!/bin/sh
# run_host_tests.sh -- HOST ONLY. Proof tests B(1)-(10) for tls18.
#   sh host/run_host_tests.sh [plain|asan]
# Needs: gcc (with -m32 multilib), openssl, python3. Builds if needed.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
V=${1:-asan}
RUNNER=${RUNNER:-}   # e.g. RUNNER=qemu-ppc for the big-endian build
BIN=$HERE/build-$V/hc_test
[ -x "$BIN" ] || sh "$HERE/build_host.sh" "$V" >/dev/null 2>&1 || { echo "build failed"; exit 1; }
W=$(mktemp -d /tmp/tls18test.XXXXXX)
BASE=${BASE_PORT:-18443}
export FAKE_MAC_DIR=$W/fakemac
export ASAN_OPTIONS=detect_leaks=1:abort_on_error=0
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1

# ---- throwaway CA + certs (EC P-256, SHA-256 signatures) ----
cd $W
q() { "$@" >/dev/null 2>&1; }
q openssl ecparam -name prime256v1 -genkey -noout -out ca.key
q openssl req -x509 -new -key ca.key -sha256 -days 30 -subj "/CN=tls18 test CA" \
    -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" -out ca.pem
openssl x509 -in ca.pem -outform DER -out ca.der 2>/dev/null
mk() { # name cn san signer
  q openssl ecparam -name prime256v1 -genkey -noout -out $1.key
  q openssl req -new -key $1.key -subj "/CN=$2" -out $1.csr
  printf "subjectAltName=%s\nbasicConstraints=CA:FALSE\nextendedKeyUsage=serverAuth\nkeyUsage=digitalSignature\n" "$3" > $1.ext
  if [ "$4" = self ]; then
    q openssl x509 -req -in $1.csr -signkey $1.key -sha256 -days 30 -extfile $1.ext -out $1.pem
  else
    q openssl x509 -req -in $1.csr -CA ca.pem -CAkey ca.key -CAcreateserial -sha256 -days 30 -extfile $1.ext -out $1.pem
  fi
}
mk srv localhost "DNS:localhost,IP:127.0.0.1" ca
mk wrong wrong.example "DNS:wrong.example" ca
mk self localhost "DNS:localhost" self
# second CA: P-384 key, SHA-384 signatures (exercises sha2big / fixes411)
q openssl ecparam -name secp384r1 -genkey -noout -out ca384.key
q openssl req -x509 -new -key ca384.key -sha384 -days 30 -subj "/CN=tls18 test CA 384" \
    -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" -out ca384.pem
openssl x509 -in ca384.pem -outform DER -out ca384.der 2>/dev/null
q openssl ecparam -name prime256v1 -genkey -noout -out l384.key
q openssl req -new -key l384.key -subj "/CN=localhost" -out l384.csr
printf "subjectAltName=DNS:localhost\nextendedKeyUsage=serverAuth\n" > l384.ext
q openssl x509 -req -in l384.csr -CA ca384.pem -CAkey ca384.key -CAcreateserial -sha384 -days 30 -extfile l384.ext -out l384.pem
export HOST_TEST_CA=$W/ca.der:$W/ca384.der

python3 -I "$HERE/test_server.py" $W $BASE > $W/server.log 2>&1 &
SPID=$!
SPIDS=""
trap 'kill $SPID $SPIDS 2>/dev/null; rm -rf $W' EXIT
i=0; while ! grep -q ready $W/server.log 2>/dev/null; do sleep 0.2; i=$((i+1)); [ $i -gt 100 ] && { echo "server did not start"; cat $W/server.log; exit 1; }; done

PASS=0; FAIL=0
H="https://localhost"
run() { # name expect-grep... ; args after --
  name=$1; shift; exp=""
  while [ "$1" != "--" ]; do exp="$exp $1"; shift; done; shift
  out=$( [ -n "${STACK_KB:-}" ] && ulimit -s $STACK_KB; timeout 90 $RUNNER "$BIN" "$@" 2>$W/stderr.txt); rc=$?
  ok=1
  for e in $exp; do echo "$out" | grep -q -- "$e" || ok=0; done
  if grep -q 'ERROR: AddressSanitizer\|runtime error\|LeakSanitizer' $W/stderr.txt; then ok=0; echo "  sanitizer:"; grep -m3 'ERROR\|runtime error' $W/stderr.txt; fi
  [ $rc -eq 124 ] && ok=0
  if [ $ok = 1 ]; then PASS=$((PASS+1)); echo "PASS $name"; else FAIL=$((FAIL+1)); echo "FAIL $name (rc=$rc)"; fi
  echo "     $out" | cut -c1-260
}
T0=$(date +%s)
run "B1  TLS1.3 GET"                 'err=0' 'status=200' 'tls=0304' -- "$H:$BASE/small"
run "B2  TLS1.2-only server"         'err=0' 'status=200' 'tls=0303' -- "$H:$((BASE+1))/small"
run "B2b client forced 1.2"          'err=0' 'tls=0303'              -- "$H:$BASE/small" --tls12
run "B3a wrong host rejected (1.3)"  'err=5(' 'br=56'                -- "$H:$((BASE+2))/small"
run "B3b wrong host rejected (1.2)"  'err=5(' 'br=56'                -- "$H:$((BASE+2))/small" --tls12
run "B3c untrusted rejected (1.3)"   'err=5(' 'br=62'                -- "$H:$((BASE+3))/small"
run "B3d untrusted rejected (1.2)"   'err=5(' 'br=62'                -- "$H:$((BASE+3))/small" --tls12
run "B4a insecure accepts (1.3)"     'err=0' 'status=200' 'ign=56'   -- "$H:$((BASE+2))/small" --insecure
run "B4b insecure accepts (1.2)"     'err=0' 'status=200' 'ign=62' 'tls=0303' -- "$H:$((BASE+3))/small" --insecure --tls12
run "B4c insecure, valid chain"      'err=0' 'ign=0'                 -- "$H:$BASE/small" --insecure
run "B5  chunked body"               'err=0' 'chunked=1' 'bytes=5014' -- "$H:$BASE/chunked"
run "B6  20MB to file"               'err=0' 'bytes=20971520'        -- "$H:$BASE/big" --out $W/big.out
if [ -f $W/big.out ] && [ "$(sha256sum < $W/big.out | cut -d' ' -f1)" = "$(cat $W/big.sha256)" ]; then
  PASS=$((PASS+1)); echo "PASS B6  sha256 matches"; else FAIL=$((FAIL+1)); echo "FAIL B6  sha256 mismatch"; fi
run "B7a http->https->relative"      'err=0' 'status=200' 'redirects=2' 'tls=0304' -- "http://localhost:$((BASE+4))/go"
run "B7b redirect loop stops"        'err=11(' 'redirects=5'         -- "$H:$BASE/loop"
run "B8  Range 206"                  'err=0' 'status=206' 'bytes=100' 'crange=bytes' -- "$H:$BASE/range" --range 300-399 --out $W/range.out
dd if=$W/range.bin bs=1 skip=300 count=100 of=$W/range.part 2>/dev/null
if cmp -s $W/range.out $W/range.part; then
  PASS=$((PASS+1)); echo "PASS B8  range bytes match"; else FAIL=$((FAIL+1)); echo "FAIL B8  range bytes differ"; fi
FAKE_MAC_SECONDS=2840140800 run "B9  clock 1994 -> clock error" 'err=6(' -- "$H:$BASE/small"
run "B10a server cuts mid-body"      'err=10(' 'bytes=5000'          -- "$H:$BASE/cut"
run "B10b plain http cut mid-body"   'err=10('                       -- "http://localhost:$((BASE+4))/cut"
run "B10c stalled body -> timeout"   'err=7('                        -- "$H:$BASE/hang" --timeout 120
run "B--  404 -> status error"       'err=8(' 'status=404'           -- "$H:$BASE/nope"
run "B--  connect refused"           'err=3('                        -- "https://localhost:$((BASE+9))/"
run "B--  DNS failure"               'err=3('                        -- "https://no-such-host.invalid/"
run "B--  plain http GET"            'err=0' 'status=200' 'tls=0000' -- "http://localhost:$((BASE+4))/small"
# ---- openssl s_server: TLS 1.3 AES-256-GCM-SHA384 (SHA-384 key schedule),
#      RSA-2048 leaf, body framed by connection close (HTTP/1.0 -www).
#      Note: macTLS never offers TLS_AES_256_GCM_SHA384 (0x1302) by design,
#      and a 1.3 client meeting a 1.2-only server RECONNECTS (2 accepts). ----
q openssl req -new -newkey rsa:2048 -nodes -keyout rsa.key -subj "/CN=localhost" -out rsa.csr
printf "subjectAltName=DNS:localhost\nextendedKeyUsage=serverAuth\n" > rsa.ext
q openssl x509 -req -in rsa.csr -CA ca.pem -CAkey ca.key -CAcreateserial -sha256 -days 30 -extfile rsa.ext -out rsa.pem
openssl s_server -quiet -accept $((BASE+5)) -cert rsa.pem -key rsa.key -www -naccept 1 \
    -ciphersuites TLS_AES_128_GCM_SHA256 > /dev/null 2>&1 &
SPIDS="$SPIDS $!"
openssl s_server -quiet -accept $((BASE+6)) -cert rsa.pem -key rsa.key -www -naccept 2 \
    -tls1_2 -cipher ECDHE-RSA-AES128-GCM-SHA256 > /dev/null 2>&1 &
SPIDS="$SPIDS $!"
openssl s_server -quiet -accept $((BASE+7)) -cert l384.pem -key l384.key -www -naccept 2 > /dev/null 2>&1 &
SPIDS="$SPIDS $!"
sleep 0.5
run "B3e SHA-384-signed chain verifies" 'err=0' 'status=200' -- "$H:$((BASE+7))/"
run "B3f same, TLS 1.2"                 'err=0' 'tls=0303'   -- "$H:$((BASE+7))/" --tls12
run "B1b s_server 1.3 AES128 RSA, EOF body" 'err=0' 'status=200' 'suite=1301' -- "$H:$((BASE+5))/"
run "B2c s_server 1.2 ECDHE-RSA"         'err=0' 'status=200' 'suite=c02f' -- "$H:$((BASE+6))/"

# ---- the Mac app itself (HTTPSGet.c) against the fake Toolbox ----
mkdir -p $FAKE_MAC_DIR/App
printf "# test list\r$H:$BASE/small\rinsecure $H:$((BASE+2))/small\rsave Big.bin $H:$BASE/big\rrange 10-19 $H:$BASE/range\rtls12\r$H:$((BASE+3))/small\r" > "$FAKE_MAC_DIR/App/HTTPS Get URL"
timeout 90 $RUNNER $HERE/build-$V/httpsget_host 2>$W/stderr.txt; rc=$?
LOG="$FAKE_MAC_DIR/App/HTTPS Get Log"
if [ $rc = 0 ] && grep -q "SHA-384 KAT: PASS" "$LOG" && [ "$(tr '\r' '\n' < "$LOG" | grep -c 'result: 0 (ok)')" = 4 ] \
   && grep -q 'result: 5 (certificate rejected)' "$LOG" && grep -q 'would have said 56' "$LOG" \
   && [ "$(sha256sum < "$FAKE_MAC_DIR/App/Big.bin" | cut -d' ' -f1)" = "$(cat $W/big.sha256)" ] \
   && ! grep -q 'Sanitizer\|runtime error' $W/stderr.txt; then
  PASS=$((PASS+1)); echo "PASS HTTPSGet app run (log + saved file)"
else FAIL=$((FAIL+1)); echo "FAIL HTTPSGet app run rc=$rc"; head -5 $W/stderr.txt; fi
tr "\r" "\n" < "$LOG" > $HERE/sample_HTTPSGet_Log.txt 2>/dev/null
# constant-memory check (plain build only; ASan inflates RSS)
if [ "$V" = plain ] && command -v python3 >/dev/null; then
  r1=$(python3 -c "import resource,subprocess,sys;subprocess.run(sys.argv[1:],stdout=subprocess.DEVNULL);print(resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss)" "$BIN" "$H:$BASE/small")
  r2=$(python3 -c "import resource,subprocess,sys;subprocess.run(sys.argv[1:],stdout=subprocess.DEVNULL);print(resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss)" "$BIN" "$H:$BASE/big" --out $W/big2.out)
  echo "INFO max RSS: small GET ${r1} KB, 20 MB GET ${r2} KB"
  if [ $((r2 - r1)) -lt 2048 ]; then PASS=$((PASS+1)); echo "PASS B6  constant memory"; else FAIL=$((FAIL+1)); echo "FAIL B6  memory grew"; fi
fi
T1=$(date +%s)
echo "RESULT: $PASS passed, $FAIL failed, $((T1-T0)) s"
[ $FAIL = 0 ]
