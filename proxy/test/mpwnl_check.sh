#!/bin/sh
# Permanent check (R1 #5): CodeWarrior's "Map newlines to CR" swaps the
# meaning of the escapes \n and \r. Simulate it on mc_proxy.c (every \n
# escape -> \015, every \r -> \012), rebuild, and require the unit suite
# and r1_sm to pass unchanged. Also require that no \n / \r escape is left.
#   sh test/mpwnl_check.sh [source]   (default src/mc_proxy.c)
set -u
H=$(cd "$(dirname "$0")/.." && pwd)
SRC=${1:-$H/src/mc_proxy.c}
W=$(mktemp -d /tmp/mpwnl.XXXXXX)
trap 'rm -rf $W' EXIT
LEFT=$(grep -c '\\[nr]' "$SRC")
printf "newline escapes (backslash n / r) left in source: %s\n" "$LEFT"
sed -e 's/\\n/@@LF@@/g; s/\\r/\\012/g; s/@@LF@@/\\015/g' "$SRC" > $W/mc_proxy.c
cmp -s "$SRC" $W/mc_proxy.c && echo "swapped copy identical to the source (no newline escapes)" || echo "swapped copy DIFFERS from the source"
gcc -std=c99 -O1 -I$H/src $H/test/mcp_unit.c $W/mc_proxy.c -o $W/unit || exit 1
gcc -m32 -std=c99 -O1 -I$H/src $H/test/r1_sm.c $W/mc_proxy.c -o $W/sm || exit 1
U=$($W/unit | tail -1); S=$($W/sm | tail -1)
echo "swapped build: $U"
echo "swapped build: $S"
case "$U$S" in *" 0 failed"*" 0 failed"*) [ "$LEFT" = 0 ] && { echo "MPWNL PASS"; exit 0; } ;; esac
echo "MPWNL FAIL"; exit 1
