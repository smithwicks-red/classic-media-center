#!/bin/sh
# build_mc05.sh -- HOST ONLY (J5). Builds mc05_host (Media Center 0.5s logic:
# mc_fetch + mc_core + H264Stream + AVCReorder + mc_stream + mc_proxy_ot +
# mc_proxy + https_client with HC_Stream) on top of build_host.sh's objects.
#   CC="gcc -idirafter /tmp/hostinc" sh host/build_mc05.sh [plain|asan]
set -e
cd "$(dirname "$0")/.."
V=${1:-asan}
[ -f host/build-$V/https_client.o ] || sh host/build_host.sh $V >/dev/null
OUT=host/build-$V
CC=${CC:-gcc}
MC=../mediacenter; PX=../proxy/src
INC="-Ihost/machdr -I. -Imactls/os9 -Imactls/bearssl/inc -Imactls/bearssl/src -I$MC -I$PX"
BASE="${ARCHFLAGS--m32} -g -O1 -include host/host_prefix.h $INC"
[ "$V" = asan ] && BASE="$BASE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"
OURW="-Wall -Wextra -Wno-unused-function -Wno-multichar -Wdeclaration-after-statement"
$CC -std=c99 $BASE $OURW -c https_client.c -o $OUT/https_client.o
$CC -std=c89 $BASE $OURW -Wno-long-long -c $MC/mc_core.c -o $OUT/mc_core.o
$CC -std=c89 $BASE $OURW -Wno-long-long -c $MC/mc_fetch.c -o $OUT/mc_fetch.o
$CC -std=c89 $BASE $OURW -c $MC/AVCReorder.c -o $OUT/AVCReorder.o
$CC -std=c89 $BASE $OURW -c $PX/mc_proxy.c -o $OUT/mc_proxy.o
$CC -std=c89 $BASE $OURW -c $MC/mc_proxy_ot.c -o $OUT/mc_proxy_ot.o
$CC -std=c89 $BASE $OURW -pedantic -c $MC/mc_notify.c -o $OUT/mc_notify.o
$CC -std=c89 $BASE $OURW -c $MC/mc_stream.c -o $OUT/mc_stream.o
$CC -std=c99 $BASE -D_GNU_SOURCE -Wall -Wno-unused-function -c host/fake_mac.c -o $OUT/fake_mac.o
$CC -std=c99 $BASE -Wall -Wno-unused-function -Wno-multichar -Wno-unused-variable -c host/mc05_host.c -o $OUT/mc05_host.o
OBJS="$(ls $OUT/bearssl_*.o) $(ls $OUT/g_*.o) $OUT/https_client.o $OUT/fake_mac.o $OUT/test_anchor.o"
$CC $BASE -o $OUT/mc05_host $OUT/mc05_host.o $OUT/mc_fetch.o $OUT/mc_core.o $OUT/AVCReorder.o \
    $OUT/mc_stream.o $OUT/mc_proxy_ot.o $OUT/mc_notify.o $OUT/mc_proxy.o $OBJS
echo "built $OUT/mc05_host"
