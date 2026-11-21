#!/bin/bash
# usage: b19savelog.sh ID "share file name"  -- copy guest Media Center Log to share
cat <<EOF | $HOME/agent.sh "$1" 60
set r to open for access file "Macintosh HD:mc02:MediaCenter:Media Center Log"
set t to read r
close access r
set w to open for access file "share:$2" with write permission
set eof w to 0
write t to w
close access w
return length of t
EOF
