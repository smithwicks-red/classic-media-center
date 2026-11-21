#!/bin/sh
# run_test_mcn.sh -- HOST ONLY (J5b): mc_notify.c state machine + hand-off into mc_proxy.
cd "$(dirname "$0")"
I="-I.. -I../../proxy/src"
S="../mc_notify.c ../../proxy/src/mc_proxy.c"
gcc -std=c89 -pedantic -Wall -Wextra -Werror $I -c ../mc_notify.c -o /tmp/mcn_c89.o && echo "mc_notify.c: c89 -pedantic -Wall -Wextra clean"
gcc -std=c89 -pedantic -Wall -Wextra -Wno-overlength-strings -g -O0 -fsanitize=address,undefined -fno-sanitize-recover=undefined $I test_mcn.c $S -o /tmp/test_mcn_asan && /tmp/test_mcn_asan
gcc -std=c89 -O2 -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wno-overlength-strings $I test_mcn.c $S -o /tmp/test_mcn_o2 && /tmp/test_mcn_o2
