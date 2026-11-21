#!/bin/bash
# usage: agent.sh ID TIMEOUT_SECONDS  (AppleScript on stdin)
S="$HOME/mnt/h264-macos9-share"
ID="$1"; T="${2:-60}"
BODY=$(cat)
printf '%s\n%s\n--END\n' "$ID" "$BODY" | iconv -c -f UTF-8 -t MACINTOSH | tr '\n' '\r' > "$S/agentcmd.txt"
out() { tr '\r' '\n' < "$S/agentout.txt" | iconv -c -f MACINTOSH -t UTF-8; }
for i in $(seq 1 $((T*2))); do
  L1=$(tr '\r' '\n' < "$S/agentout.txt" | sed -n 1p)
  L2=$(tr '\r' '\n' < "$S/agentout.txt" | sed -n 2p)
  if [ "$L1" = "$ID" ] && { [ "$L2" = "OK" ] || [ "$L2" = "ERR" ]; }; then sleep 1.5; out; exit 0; fi
  sleep 0.5
done
echo "TIMEOUT waiting for $ID"; out; exit 3
