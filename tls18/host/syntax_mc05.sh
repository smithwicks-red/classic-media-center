#!/bin/sh
# syntax_mc05.sh -- HOST ONLY (J5). gcc -fsyntax-only over the Mac-only
# MediaCenter.c (+ H264Stream.c) against host/machdr + host/macstub05
# (types only; nothing linked). Implicit Toolbox calls are allowed (C89);
# the new 0.5s code must add no warning.  sh host/syntax_mc05.sh
cd "$(dirname "$0")/.."
gcc -m32 -std=c89 -fsyntax-only -Wall -Wno-multichar -Wno-unknown-pragmas \
  -include host/host_prefix.h -Ihost/machdr -include host/macstub05/mc05_syntax.h \
  -Ihost/macstub05 -I. -Imactls/os9 -Imactls/bearssl/inc -I../mediacenter -I../proxy/src \
  ../mediacenter/MediaCenter.c 2>&1 | grep -E "error|warning" > /tmp/mc05_syntax.txt
E=$(grep -c "error" /tmp/mc05_syntax.txt)
N=$(grep -E "mcn_|mcs_|mcpo_|QTClose|CbQtClose|IsG3|UpdateStop|DoStopStreaming|DoQuit|StreamPass|SetupStreaming|kMC_" /tmp/mc05_syntax.txt | wc -l)
echo "MediaCenter.c syntax: $E errors; warnings touching 0.5s code: $N (other warnings = stub gaps in 0.4 code: $(grep -c warning /tmp/mc05_syntax.txt))"
[ "$E" = 0 ] && [ "$N" = 0 ]
