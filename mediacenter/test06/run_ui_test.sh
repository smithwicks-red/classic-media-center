#!/bin/sh
# Media Center 0.6: the portable controller's host test (mc_ui_test.c):
# x86-64 with ASan+UBSan, then 32-bit big-endian PowerPC under qemu-ppc;
# the two transcripts must be byte-identical.
set -e
cd "$(dirname "$0")"
CF="-std=c89 -pedantic -Wall -Wextra -Werror -O1 -I.."
SRC="mc_ui_test.c ../mc_ui.c ../mc_layout.c ../mc_skin.c ../mc_core.c"
gcc $CF -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -o /tmp/mc_ui_test $SRC
/tmp/mc_ui_test > /tmp/mc_ui_x86.txt || { grep -v "^ok" /tmp/mc_ui_x86.txt; exit 1; }
tail -1 /tmp/mc_ui_x86.txt | sed 's/$/ (x86-64, ASan+UBSan)/'
powerpc-linux-gnu-gcc $CF -static -o /tmp/mc_ui_test.ppc $SRC
qemu-ppc /tmp/mc_ui_test.ppc > /tmp/mc_ui_ppc.txt || { grep -v "^ok" /tmp/mc_ui_ppc.txt; exit 1; }
tail -1 /tmp/mc_ui_ppc.txt | sed 's/$/ (qemu-ppc, big-endian)/'
cmp /tmp/mc_ui_x86.txt /tmp/mc_ui_ppc.txt && echo "transcripts identical ($(wc -l < /tmp/mc_ui_x86.txt) lines)"
