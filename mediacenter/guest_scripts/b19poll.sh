#!/bin/bash
# usage: b19poll.sh ID [chars]  -- process list + tail of Media Center Log
N=${2:-1500}
cat <<EOF | $HOME/agent.sh "$1" 40
tell application "Finder"
set p to name of every process
end tell
set f to "Macintosh HD:mc02:MediaCenter:Media Center Log"
set r to open for access file f
set n to get eof r
if n > $N then
set t to read r from (n - $N) to n
else
set t to read r
end if
close access r
set AppleScript's text item delimiters to ","
set ps to p as string
set AppleScript's text item delimiters to ""
return ps & return & "LOGSIZE " & n & return & t
EOF
