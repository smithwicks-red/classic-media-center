#!/bin/sh
# build_mc02.sh -- HOST ONLY. Builds mc02_host (Media Center 0.2 logic)
# on top of build_host.sh's objects.   sh host/build_mc02.sh [plain|asan|ppc]
set -e
cd "$(dirname "$0")/.."
V=${1:-asan}
[ -f host/build-$V/https_client.o ] || sh host/build_host.sh $V >/dev/null
# https_client.c may have changed (b19: redirect callback): rebuild it
OUT=host/build-$V
CC=${CC:-gcc}
INC="-Ihost/machdr -I. -Imactls/os9 -Imactls/bearssl/inc -Imactls/bearssl/src -I../mediacenter"
BASE="${ARCHFLAGS--m32} -g -O1 -include host/host_prefix.h $INC"
[ "$V" = asan ] && BASE="$BASE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"
OURW="-Wall -Wextra -Wno-unused-function -Wno-multichar -Wdeclaration-after-statement"
$CC -std=c99 $BASE $OURW -c https_client.c -o $OUT/https_client.o
$CC -std=c89 $BASE $OURW -Wno-long-long -c ../mediacenter/mc_core.c -o $OUT/mc_core.o
$CC -std=c89 $BASE $OURW -Wno-long-long -c ../mediacenter/mc_fetch.c -o $OUT/mc_fetch.o
$CC -std=c89 $BASE $OURW -c ../mediacenter/AVCReorder.c -o $OUT/AVCReorder.o   # (0.4)
$CC -std=c99 $BASE -Wall -Wno-unused-function -Wno-multichar -Wno-unused-variable -c host/mc02_host.c -o $OUT/mc02_host.o
OBJS="$(ls $OUT/bearssl_*.o) $(ls $OUT/g_*.o) $OUT/https_client.o $OUT/fake_mac.o $OUT/test_anchor.o"
$CC -std=c99 $BASE -D_GNU_SOURCE -Wall -Wno-unused-function -c host/fake_mac.c -o $OUT/fake_mac.o
$CC $BASE -o $OUT/mc02_host $OUT/mc02_host.o $OUT/mc_fetch.o $OUT/mc_core.o $OUT/AVCReorder.o $OBJS
echo "built $OUT/mc02_host"
