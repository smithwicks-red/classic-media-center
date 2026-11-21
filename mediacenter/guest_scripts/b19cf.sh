#!/bin/bash
# usage: b18cf.sh ID MAXERR relpath...  compile listed files, print errors
ID=$1; MX=$2; shift 2
L=$(for f in "$@"; do printf '"Macintosh HD:mc02:MediaCenter:%s",' "$(echo $f | tr / :)"; done | sed 's/,$//')
cat <<EOF | $HOME/agent.sh "$ID" 170 | tail -n +3 | sed 's/Macintosh HD:Applications (Mac OS 9):Metrowerks CodeWarrior 8.0:Metrowerks CodeWarrior:/{CW}/; s/Macintosh HD:mc02:MediaCenter:/{T}/'
set t0 to current date
with timeout of 160 seconds
tell application "CodeWarrior IDE"
set errs to Compile Files {$L} with ExternalEditor
end tell
end timeout
set o to "N=" & (count errs) & " secs=" & ((current date) - t0) & return
set i to 0
repeat with e in errs
set i to i + 1
if i > $MX then exit repeat
set s to "?"
set ln to "?"
set fn to "?"
set t to "?"
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
set o to o & t & "|" & fn & "|" & ln & "|" & s & return
end repeat
return o
EOF
