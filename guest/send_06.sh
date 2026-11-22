#!/bin/bash
# send_06.sh ID TREE -- copy the Media Center 0.6 sources to the guest project folder
# (send_05t.sh + the 0.6 files: mc_ui, mc_layout, mc_skin, mc_demo (.c/.h) and the
# font as Rez text "MC Garamond.r"; the binary "MC Garamond.rsrc" needs its resource
# fork restored on the guest by other means, see BUILD_NOTES_06.md).
#   TREE = the unpacked b25c-mediacenter-0.6 tree (has mediacenter/, proxy/src/, tls18/).
#   LF masters -> share:mc5t_src:<name> (CR line ends, MacRoman; NO subfolder on the
#   share volume besides this one flat folder) -> Macintosh HD:mc04:MediaCenter:<name>,
#   type TEXT, creator CWIE. Everything lands FLAT in the project folder (mc_proxy.c/.h
#   and https_client.c/.h included), so no access path has to change.
#   Needs AGENT= (path of f20/guest scripts/agent.sh); SHARE= defaults as in Q2.
AGENT=${AGENT:?set AGENT to the path of f20/guest scripts/agent.sh}
S=${SHARE:-$HOME/mnt/h264-macos9-share}; ID=${1:?id}; T=${2:?tree}; M=$S/mc5t_manifest.txt
mkdir -p "$S/mc5t_src"; : > "$M"
FILES="mediacenter/MediaCenter.c mediacenter/H264Stream.c mediacenter/mc_core.c mediacenter/mc_core.h
mediacenter/mc_fetch.c mediacenter/mc_fetch.h mediacenter/mc_stream.c mediacenter/mc_stream.h
mediacenter/mc_proxy_ot.c mediacenter/mc_proxy_ot.h mediacenter/mc_notify.c mediacenter/mc_notify.h
proxy/src/mc_proxy.c proxy/src/mc_proxy.h tls18/https_client.c tls18/https_client.h
mediacenter/mc_ui.c mediacenter/mc_ui.h mediacenter/mc_layout.c mediacenter/mc_layout.h
mediacenter/mc_skin.c mediacenter/mc_skin.h mediacenter/mc_demo.c mediacenter/mc_demo.h"
for f in $FILES; do
  b=$(basename "$f")
  [ -f "$T/$f" ] || { echo "missing $T/$f"; exit 1; }
  iconv -c -f UTF-8 -t MACINTOSH//TRANSLIT "$T/$f" | tr '\n' '\r' > "$S/mc5t_src/$b"
  printf '%s\t%s\t%s\r' "$b" "$(stat -c %s "$S/mc5t_src/$b")" "$b" >> "$M"
done
# (0.6) the font as Rez text (its name has a space: outside the loop)
iconv -c -f UTF-8 -t MACINTOSH//TRANSLIT "$T/font/MC Garamond.r" | tr '\n' '\r' > "$S/mc5t_src/MC Garamond.r"
printf '%s\t%s\t%s\r' "MC Garamond.r" "$(stat -c %s "$S/mc5t_src/MC Garamond.r")" "MC Garamond.r" >> "$M"
echo "staged $(ls "$S/mc5t_src" | wc -l) files in $S/mc5t_src"
cat <<'EOS' | "$AGENT" "$ID" 170
set H to "Macintosh HD:mc04:MediaCenter:"
set t0 to current date
set man to read file "share:mc5t_manifest.txt"
set AppleScript's text item delimiters to return
set ls to text items of man
set AppleScript's text item delimiters to tab
set done to 0
repeat with i from 1 to (count ls)
set ln to item i of ls
if ln is not "" then
set p to text item 1 of ln
set n to (text item 2 of ln) as integer
set q to text item 3 of ln
set r to open for access file ("share:mc5t_src:" & p)
set w to open for access file (H & q) with write permission
set eof w to 0
set rem to n
repeat while rem > 0
if rem > 4000 then
set k to 4000
else
set k to rem
end if
write (read r for k) to w
set rem to rem - k
end repeat
close access r
close access w
tell application "Finder"
set file type of file (H & q) to "TEXT"
set creator type of file (H & q) to "CWIE"
end tell
set done to done + 1
end if
end repeat
set AppleScript's text item delimiters to ""
return {done, (current date) - t0}
EOS
