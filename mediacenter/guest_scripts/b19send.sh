#!/bin/bash
# usage: b19send.sh ID file...   (paths relative to ~/b19w; LF master -> CR MacRoman,
#   share:b19_src:<basename> -> Macintosh HD:mc02:MediaCenter:<basename>, TEXT/CWIE)
ID=$1; shift; S=$HOME/mnt/h264-macos9-share; M=$S/b19_manifest.txt; mkdir -p $S/b19_src; : > $M
for f in "$@"; do b=$(basename $f); iconv -c -f UTF-8 -t MACINTOSH//TRANSLIT "$HOME/b19w/$f" | tr '\n' '\r' > "$S/b19_src/$b"; printf '%s\t%s\r' "$b" $(stat -c %s "$S/b19_src/$b") >> $M; done
cat <<EOF | $HOME/agent.sh "$ID" 170
set t0 to current date
set man to read file "share:b19_manifest.txt"
set AppleScript's text item delimiters to return
set ls to text items of man
set AppleScript's text item delimiters to tab
set done to 0
set bytes to 0
repeat with i from 1 to (count ls)
set ln to item i of ls
if ln is not "" then
set p to text item 1 of ln
set n to (text item 2 of ln) as integer
set r to open for access file ("share:b19_src:" & p)
set w to open for access file ("Macintosh HD:mc02:MediaCenter:" & p) with write permission
set eof w to 0
set rem to n
repeat while rem > 0
if rem > 6000 then
set k to 6000
else
set k to rem
end if
set s to read r for k
write s to w
set rem to rem - k
end repeat
close access r
close access w
tell application "Finder"
set file type of file ("Macintosh HD:mc02:MediaCenter:" & p) to "TEXT"
set creator type of file ("Macintosh HD:mc02:MediaCenter:" & p) to "CWIE"
end tell
set done to done + 1
set bytes to bytes + n
end if
end repeat
set AppleScript's text item delimiters to ""
return {done, bytes, (current date) - t0}
EOF
