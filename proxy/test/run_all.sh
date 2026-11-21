#!/bin/sh
# HOST ONLY. Reproduces every h1 proof against the MERGED tls18 (J2: HC_Stream
# is part of https_client.c/.h). Needs gcc -m32 multilib, qemu-ppc,
# powerpc-linux-gnu-gcc, python3, openssl. TLS objects come from a COPY of
# ../tls18 built with its own build_host.sh (asan).
#   (J5) layout: src/proxy/{src,test,logs}; TLS copy in $TLS (default /tmp/j5tls)
set -e
H=$(cd "$(dirname "$0")/.." && pwd)
TLS=${TLS:-/tmp/j5tls}
[ -d $TLS ] || cp -r $H/../tls18 $TLS
mkdir -p /tmp/hostinc; ln -sf /usr/include/x86_64-linux-gnu/asm /tmp/hostinc/asm
[ -f $TLS/host/build-asan/fake_mac.o ] || (cd $TLS && CC="gcc -idirafter /tmp/hostinc" sh host/build_host.sh asan)
cd $H
mkdir -p logs
{ gcc -std=c89 -pedantic -Wall -Wextra -c src/mc_proxy.c -o /tmp/mcp.o; gcc -m32 -std=c89 -pedantic -Wall -Wextra -c src/mc_proxy.c -o /tmp/mcp32.o; } 2>&1 | tee logs/c89_pedantic.txt
gcc -std=c99 -g -O1 -Wall -fsanitize=address,undefined -fno-sanitize-recover=undefined -Isrc test/mcp_unit.c src/mc_proxy.c -o /tmp/j5_mcp_unit
/tmp/j5_mcp_unit | tee logs/unit_asan.txt | tail -1
powerpc-linux-gnu-gcc -std=c99 -O1 -static -Isrc test/mcp_unit.c src/mc_proxy.c -o /tmp/j5_mcp_unit_ppc
qemu-ppc /tmp/j5_mcp_unit_ppc > logs/unit_ppc.txt; tail -1 logs/unit_ppc.txt
gcc -m32 -std=c99 -g -O1 -Wall -fsanitize=address,undefined -fno-sanitize-recover=undefined -Isrc test/r1_sm.c src/mc_proxy.c -o /tmp/j5_r1_sm32
/tmp/j5_r1_sm32 > logs/r1_sm32.txt; tail -1 logs/r1_sm32.txt
powerpc-linux-gnu-gcc -std=c99 -O1 -static -Isrc test/r1_sm.c src/mc_proxy.c -o /tmp/j5_r1_sm_ppc
qemu-ppc /tmp/j5_r1_sm_ppc > logs/r1_sm_ppc.txt; tail -1 logs/r1_sm_ppc.txt
# (J5) /a/ query strings ignored (QuickTime caches by URL; ?t=2 must hit the same film)
gcc -std=c99 -g -O1 -Wall -fsanitize=address,undefined -fno-sanitize-recover=undefined -Isrc test/mcp_query_test.c src/mc_proxy.c -o /tmp/j5_mcp_query
/tmp/j5_mcp_query | tee logs/query_test.txt | tail -1
sh test/mpwnl_check.sh | tee logs/mpwnl_check.txt | tail -1
OUT=$TLS/host/build-asan
INC="-I$TLS/host/machdr -I$TLS -I$TLS/mactls/os9 -I$TLS/mactls/bearssl/inc -I$TLS/mactls/bearssl/src -I$H/src"
B="-m32 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"
gcc $B -std=c89 -pedantic -Wall -c src/mc_proxy.c -o /tmp/j5_mc_proxy32.o
gcc -idirafter /tmp/hostinc $B -std=c99 -Wall -Isrc -Itest -c test/mcp_host.c -o /tmp/j5_mcp_host.o
gcc $B -o /tmp/j5_mcp_host /tmp/j5_mcp_host.o /tmp/j5_mc_proxy32.o $OUT/https_client.o $OUT/bearssl_*.o $OUT/g_*.o $OUT/fake_mac.o $OUT/test_anchor.o
for i in 1 2 3; do R1_PORTSHIFT=${R1_PORTSHIFT:-4000} python3 -I test/run_e2e.py /tmp/j5_mcp_host /tmp/j5e2e$i logs/e2e_proxy_log_run$i.txt > logs/e2e_results_run$i.txt 2>&1 || true; tail -1 logs/e2e_results_run$i.txt; done
R1_PORTSHIFT=${R1_PORTSHIFT:-4000} MCP_EXTRA="--buf 4096" python3 -I test/run_e2e.py /tmp/j5_mcp_host /tmp/j5e2e4 logs/e2e_proxy_log_buf4k.txt > logs/e2e_results_buf4k.txt 2>&1 || true; tail -1 logs/e2e_results_buf4k.txt
