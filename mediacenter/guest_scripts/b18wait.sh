#!/bin/bash
# usage: b18wait.sh ID SECONDS  -- poll agentout for ID done
S="$HOME/mnt/h264-macos9-share"; ID="$1"; T="${2:-170}"
for i in $(seq 1 $((T/2))); do
  L1=$(tr '\r' '\n' < "$S/agentout.txt" | sed -n 1p); L2=$(tr '\r' '\n' < "$S/agentout.txt" | sed -n 2p)
  if [ "$L1" = "$ID" ] && { [ "$L2" = "OK" ] || [ "$L2" = "ERR" ]; }; then sleep 1.5; tr '\r' '\n' < "$S/agentout.txt" | iconv -c -f MACINTOSH -t UTF-8; exit 0; fi
  sleep 2
done
echo "STILL WAITING $ID ($L1 $L2)"; exit 3
