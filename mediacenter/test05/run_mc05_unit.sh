#!/bin/sh
# HOST ONLY (J5): mc_stream.c policy tests, C89 -pedantic + ASan/UBSan, then
# big-endian under qemu-ppc (if powerpc-linux-gnu-gcc is there).
cd "$(dirname "$0")"
gcc -std=c89 -pedantic -Wall -Wextra -g -fsanitize=address,undefined -fno-sanitize-recover=undefined \
  -I.. -I../../proxy/src mc05_unit.c ../mc_stream.c ../../proxy/src/mc_proxy.c -o /tmp/mc05_unit && /tmp/mc05_unit | tail -1
command -v powerpc-linux-gnu-gcc >/dev/null && powerpc-linux-gnu-gcc -std=c89 -O1 -static -I.. -I../../proxy/src \
  mc05_unit.c ../mc_stream.c ../../proxy/src/mc_proxy.c -o /tmp/mc05_unit_ppc && qemu-ppc /tmp/mc05_unit_ppc | tail -1
