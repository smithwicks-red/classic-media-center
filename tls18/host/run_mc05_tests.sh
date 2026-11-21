#!/bin/sh
# run_mc05_tests.sh -- HOST ONLY (J5). Builds mc05_host (asan) and runs the
# streaming end-to-end proof.   sh host/run_mc05_tests.sh CLIPDIR [WORKDIR]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
CLIPS=$(cd "${1:?clipdir}" && pwd)
W=${2:-$(mktemp -d /tmp/mc05test.XXXXXX)}
mkdir -p /tmp/hostinc; ln -sf /usr/include/x86_64-linux-gnu/asm /tmp/hostinc/asm
(cd $HERE/.. && CC="gcc -idirafter /tmp/hostinc" sh host/build_mc05.sh asan >/dev/null 2>&1) || { echo "build failed"; exit 1; }
python3 -I $HERE/mc05_e2e.py $HERE/build-asan/mc05_host $CLIPS $W ${BASE_PORT:-19643} ${PROXY_PORT:-18080}
