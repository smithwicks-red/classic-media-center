#!/bin/bash
# usage: b18make.sh ID [rb]   -- sends Make; result via b18wait.sh
RB=""; [ "$2" = rb ] && RB="Remove Binaries"
cat <<EOF | $HOME/agent.sh "$1" 5 >/dev/null
set t0 to current date
with timeout of 5000 seconds
tell application "CodeWarrior IDE"
$RB
set errs to Make Project with ExternalEditor
end tell
end timeout
set o to "N=" & (count errs) & " secs=" & ((current date) - t0) & return
set i to 0
repeat with e in errs
set i to i + 1
if i > 60 then exit repeat
set t to "?"
set s to "?"
set fn to "?"
set ln to "?"
try
set t to («class ErrT» of e) as string
end try
try
set s to «class ErrS» of e
end try
try
set fn to («class file» of e) as string
end try
try
set ln to («class ErrL» of e) as string
end try
set o to o & t & " | " & fn & " | " & ln & " | " & s & return
end repeat
return o
EOF
echo sent
