# Classic Media Center 0.6: unattended guest checks (stage C)

Everything below is AppleScript sent through the applet; nobody clicks, nobody types, no screenshots.
`APP` = the built application, e.g. `Macintosh HD:mc04:MediaCenter:Media Center 0.6` (if the CodeWarrior
output keeps another name, use that path; `tell application "<path>"` works by path). The dump files and
the log land beside the app. Screen at Thousands or Millions of colours (Monitors control panel), else
the app runs the 0.5u dialog (see step 7).

## 0. Raw event syntax (OS 9 AppleScript 1.6+)
```applescript
tell application "Macintosh HD:mc04:MediaCenter:Media Center 0.6"
    «event MCdbdump» 3
end tell
```
`«event MCdbdump» N` = class 'MCdb', id 'dump', direct parameter N (an integer, coerced to 'long').
Type the chevrons with Option-\ and Shift-Option-\ (MacRoman 0xC7/0xC8). A dump error comes back as an
AppleScript error number (-50 unknown N, -108 no GWorld memory, -1708 N=99 in dialog mode, or a File
Manager error); wrap in `try ... on error m number n ... end try` and record n.

## 1. Launch
```applescript
tell application "Finder" to open file "Macintosh HD:mc04:MediaCenter:Media Center 0.6"
delay 8
```
Success: the log ("Media Center Log" beside the app) starts with `Classic Media Center 0.6 --`, then
`Decoder: ...`, `CPU type ...`, and a line `Window: skin 480x360 (32-bit screen, WDEF 4, GWorld rowBytes
1920); headings "<font>" (id N; MC Garamond id M from the app); font smoothing: ...`. Expected font: "MC
Garamond" (unless Apple Garamond is installed) and "from the app". Make sure no "Media Center Query" file
is beside the app (that starts a hook run).

## 2. Demo scenes 0-9
```applescript
repeat with n from 0 to 9
    tell application "Macintosh HD:mc04:MediaCenter:Media Center 0.6" to «event MCdbdump» n
end repeat
```
Success: files `MC dump 0` ... `MC dump 9`, each 16 + 360 x rowBytes bytes (rowBytes 1920 expected:
691,216 bytes); log lines `DUMP n <scene> -> "MC dump n" err 0; heading font "MC Garamond" (id 279)`.
On the host: `python3 dump2png.py "MC dump"* -o png/` and compare with `host_png/` (pixels of the
metal/buttons/list should match; text is QuickDraw's, not the host TTF).

## 3. Font test (U2)
`«event MCdbdump» 50` -> `MC dump 50` and two log lines `FONTTEST Heading chain -> ...` /
`FONTTEST Apple Garamond 0, Hoefler Text n, Palatino n (0 = none); font smoothing: ON at N pt and up`.
Success: MC Garamond rows (14/18/20/24) drawn in the serif (not Geneva), on metal and on white.
If "MC Garamond id 0": the Font Manager did not see the app's FOND (fallback chain used: report it).

## 4. Live window
`«event MCdbdump» 99` -> `MC dump 99` = the window as it is (the search screen, empty field, grey hint,
aqua Search button). The window comes back unchanged after every dump (a later 99 shows that).

## 5. Optional: a search through the hook path is NOT needed for this stage (0.5u's hook tests still
apply: a "Media Center Query" file makes the 0.6 window run them, with the LCD showing the status).

## 6. Quit by Apple event
```applescript
tell application "Macintosh HD:mc04:MediaCenter:Media Center 0.6" to quit
```
Success: log ends with `Exit.`; the process is gone (`tell application "Finder" to get name of every
process` has no "Media Center 0.6"). Not streaming, so ONE quit suffices (while streaming the first quit
only shows the LCD warning, as in 0.5u's status line).

## 7. Optional: 256 colours
Switch the screen to 256 colours, launch: log `Window: the 0.5u dialog (screen at 8 bits ...)`;
`«event MCdbdump» 3` still works (temporary GWorld, log `[dialog mode: temporary GWorld]`);
`«event MCdbdump» 99` returns error -1708.

## 8. Read the log
Copy "Media Center Log" (CR line ends) to the share; host: `tr '\r' '\n'`. Expect no `FAILED`, one DUMP
line per dump, the Window line, the FONTTEST lines, `Exit.`.

## 0.6 review fixes (b25b): what the guest should see
- Window frame: plainDBox (procID 4) always: a 1-pixel black outline just outside the 480x360 content, no title bar
  (1984 was the 7.5 floating window with a title bar; it is no longer used). The log line says `WDEF 4`.
- Play a file: no crash/low-memory damage (the List calls are skipped in skin mode).
- Click and drag in the search field: the selection starts at the click point and follows the drag (TEClick runs in the window port).
- Esc / Cmd-. while only STREAMING (QuickTime playing, not busy) = Back, the film keeps playing. While busy (search,
  download, buffering) they still cancel/Stop. They never quit.
- Apple menu > About: Return, Enter, Esc or Cmd-. closes it; the film keeps playing while it is open (own WaitNextEvent loop).
