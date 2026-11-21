#!/bin/sh
# build_host.sh -- HOST ONLY. Builds hc_test (and httpsget_host) from the
# vendored macTLS subset + our files + the fake OT/Toolbox layer.
#   sh build_host.sh [asan]     (output in ./build-<variant>)
set -e
cd "$(dirname "$0")/.."
V=${1:-plain}
OUT=host/build-$V
mkdir -p $OUT
CC=${CC:-gcc}
INC="-Ihost/machdr -I. -Imactls/os9 -Imactls/bearssl/inc -Imactls/bearssl/src"
BASE="${ARCHFLAGS--m32} -std=c99 -g -O1 -include host/host_prefix.h $INC"
[ "$V" = asan ] && BASE="$BASE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"
OURW="-Wall -Wextra -Wno-unused-function -Wno-multichar -Wdeclaration-after-statement"
GLUE="ostls_async ostls_log ostls_b3_anchors ostls_time ostls_entropy ostls_tls13_handshake ostls_tls13_record ostls_tls13_keysched ostls_ticket_cache ostls_tls13_selftest ostls_noverify_x509"
OBJS=""
if [ ! -f $OUT/.bearssl_done ]; then
  for f in $(cat bearssl_files.txt); do
    o=$OUT/$(echo $f | tr / _).o
    EXTRA=""
    # sha2big.c's CW8 path (fixes411) splits uint64 through a union that
    # is only right on 32-bit BIG-endian; on x86 use the stock path. The
    # CW8 path itself is exercised under qemu-ppc (run_ppc_kat.sh).
    case "$f" in *sha2big.c) [ -z "$KEEP_MWERKS_SHA" ] && EXTRA="-DHOST_STOCK_SHA2BIG";; esac
    $CC $BASE $EXTRA -w -c mactls/$f -o $o
  done
  touch $OUT/.bearssl_done
fi
OBJS="$(ls $OUT/bearssl_*.o)"
for g in $GLUE; do
  X=""
  # host tests may override the anchor table (host/test_anchor.c)
  [ $g = ostls_b3_anchors ] && X="-DOSTLS_B3_GetAnchors=OSTLS_B3_GetAnchors_real"
  $CC $BASE $X -w -fstack-usage -c mactls/os9/$g.c -o $OUT/g_$g.o
  OBJS="$OBJS $OUT/g_$g.o"
done
$CC $BASE $OURW -fstack-usage -c https_client.c -o $OUT/https_client.o
$CC $BASE -D_GNU_SOURCE -Wall -Wno-unused-function -c host/fake_mac.c -o $OUT/fake_mac.o
$CC $BASE -Wall -Wno-unused-function -c host/test_anchor.c -o $OUT/test_anchor.o
$CC $BASE -D_GNU_SOURCE -Wall -Wno-unused-function -c host/host_main.c -o $OUT/host_main.o
$CC $BASE $OURW -c HTTPSGet.c -o $OUT/HTTPSGet.o
COMMON="$OBJS $OUT/https_client.o $OUT/fake_mac.o $OUT/test_anchor.o"
$CC $BASE -o $OUT/hc_test $OUT/host_main.o $COMMON
$CC $BASE -o $OUT/httpsget_host $OUT/HTTPSGet.o $COMMON
echo "built $OUT/hc_test $OUT/httpsget_host"
