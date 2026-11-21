-- build_05u.applescript -- build Classic Media Center 0.5u in CodeWarrior 8.3 (classic PPC).
-- Send with: agent.sh m5b 900 < build_05u.applescript
-- NEVER launches or touches QuickTime Player. Returns "N=<messages> ..." then one
-- line per message: type | file | line | text. On "error of type 2", retry up to 3 times.
-- Precondition: send_05t.sh copied 16 files FLAT into Macintosh HD:mc04:MediaCenter:.
set H to "Macintosh HD:mc04:MediaCenter:"
set t0 to current date
set o to ""
-- the project: MediaCenter.mcp, else the first .mcp in the folder
set P to H & "MediaCenter.mcp"
tell application "Finder"
	if not (exists file P) then
		set ms to (name of every file of folder H whose name ends with ".mcp")
		if (count ms) > 0 then set P to H & (item 1 of ms)
	end if
end tell
set o to o & "project:" & P & return
with timeout of 900 seconds
	tell application "CodeWarrior IDE"
		open file P
		-- https_client.c: make the project use the copy in this folder (the HC_Stream merge)
		try
			Remove Files {"https_client.c"}
			set o to o & "rm https_client.c:ok "
		on error m number n
			set o to o & "rm https_client.c:" & n & " "
		end try
		-- new sources (0.5s: mc_proxy.c mc_proxy_ot.c mc_stream.c; 0.5t: mc_notify.c).
		-- Adding a file that is already in the project is harmless.
		repeat with f in {"https_client.c", "mc_proxy.c", "mc_proxy_ot.c", "mc_stream.c", "mc_notify.c"}
			try
				Add Files {H & f}
				set o to o & "add " & f & ":ok "
			on error m number n
				set o to o & "add " & f & ":" & n & " "
			end try
		end repeat
		Remove Binaries
		set errs to Make Project with ExternalEditor
	end tell
end timeout
set o to o & return & "N=" & (count errs) & " secs=" & ((current date) - t0) & return
set i to 0
repeat with e in errs
	set i to i + 1
	if i > 80 then exit repeat
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
-- partition (0.5u): 4 MB cap pre-buffer Ptr + 61 KB notifier state -> 8192 K min / 9216 K preferred.
-- (0.5t: 4608 K / 5632 K.) 0.5s asked 4096 K min / 5120 K preferred; 0.5t adds the 512 KB
-- pre-buffer Ptr + 61 KB notifier state -> 4608 K / 5632 K. Finder sizes are
-- BYTES on 9.2.2; if this errors, set them in Get Info by hand.
try
	tell application "Finder"
		set apps to (every file of folder H whose file type is "APPL")
		repeat with a in apps
			set preferred size of a to 9216 * 1024
			set minimum size of a to 8192 * 1024
			set o to o & "size " & (name of a) & ":" & ((preferred size of a) div 1024) & "K/" & ((minimum size of a) div 1024) & "K "
		end repeat
	end tell
on error m number n
	set o to o & "size:ERR " & n & " " & m
end try
return o
