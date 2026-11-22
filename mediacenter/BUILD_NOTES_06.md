# Classic Media Center 0.6 (job U3 stage A): CodeWarrior project changes

Start from the 0.5u project (`Macintosh HD:mc04:MediaCenter:MediaCenter.mcp`). Everything stays FLAT in
that folder, so **no access path changes**. Prefix `https_prefix.h`, "Map newlines to CR" ON, ANSI Strict
and Require Prototypes OFF, as before.

## 1. Send the sources
`guest/send_06.sh ID TREE` (= send_05t.sh + 9 files): the 16 of 0.5u (MediaCenter.c, mc_core.c/.h and
mc_fetch.c/.h changed) plus `mc_ui.c/.h`, `mc_layout.c/.h`, `mc_skin.c/.h`, `mc_demo.c/.h` and
`MC Garamond.r` (the font as Rez text). All TEXT/CWIE, CR line ends, pure ASCII.

## 2. Project files to add (`guest/build_06.applescript` does 2-4)
- `mc_ui.c`, `mc_layout.c`, `mc_skin.c`, `mc_demo.c` (portable C89, host-tested).
- The font, ONE of:
  - **`MC Garamond.rsrc`** (preferred if it reaches the folder with its resource fork: the U2 files
    `font/MC Garamond.rsrc` = raw fork 73,391 B, or `.rdump` + `.idump` for the share's fork restore).
    Set its type to `rsrc` (build_06 does) so CodeWarrior's resource mapping copies `FOND` 279 "MC Garamond"
    and `sfnt` 279 into the application.
  - **`MC Garamond.r`** (fallback, what send_06.sh sends): plain `data 'FOND' (279, ...)` / `data 'sfnt'
    (279)` hex, no #include, so CodeWarrior's Rez compiles it with no header. Same two resources.
  build_06 adds the .rsrc if it exists in the folder, else the .r.
- H264Stream.c stays #included (not a project file). The skin's Toolbox layer is inside MediaCenter.c.

## 3. Libraries: none new
New calls are InterfaceLib: QDOffscreen (NewGWorld, GetGWorldPixMap, LockPixels, GetPixBaseAddr, SetGWorld,
GetGWorld, DisposeGWorld, CopyBits), QuickDraw text/colour (TextFont/Size/Face/Mode, TextWidth, DrawText,
RGBForeColor/BackColor, ClipRect, SectRect), TruncText (Script Mgr), TextEdit (TENew, TEKey, TEClick,
TEIdle, TEActivate/Deactivate, TEUpdate, TESetSelect, TECut/Copy/Paste/Delete, TEAutoView, TESetClickLoop,
NewTEClickLoopUPP, TEGetScrapLength), Menus (NewMenu, AppendMenu, AppendResMenu, InsertMenu, DrawMenuBar,
MenuSelect, MenuKey, HiliteMenu, Enable/DisableItem, GetMenuItemText, OpenDeskAcc, SystemEdit), Windows
(NewCWindow, ShowWindow, SelectWindow, DisposeWindow), Fonts/Resources (GetFNum, GetNamedResource,
HomeResFile, CurResFile, SetResLoad), Dialogs (NewDialog, ModalDialog, NewModalFilterUPP,
SetDialogDefaultItem), Code Fragment Manager (GetSharedLibrary, FindSymbol), AEGetParamPtr, FSpCreate,
SetEOF. **AppearanceLib is NOT linked**: `IsAntiAliasedTextEnabled` is looked up by name with
GetSharedLibrary/FindSymbol (log says "unknown" if absent). Every callback goes through New...UPP
(AEEventHandlerUPP for 'MCdb'/'dump', TEClickLoopUPP, ModalFilterUPP).

## 4. Partition: unchanged, 9216 K preferred / 8192 K minimum (build_06 sets it).
The skin adds ~700 KB: GWorld 480x360x4 = 691 KB, normally in temporary memory (`useTempMem`), else the
app heap; plus McCtl ~14 KB, demo McUi ~12 KB, runs 28 KB (static). If NewGWorld fails in both, the log
says "Window: the 0.5u dialog (no memory ...)" and 0.5u's dialog runs.

## 5. Expected compiler output
0 errors. Possible warnings: none known in the new files (gcc -std=c89 -pedantic -Wall -Wextra -Werror
clean for mc_ui/mc_layout/mc_skin/mc_demo/mc_core/mc_fetch-portable parts). MediaCenter.c: the host
syntax gate (`tls18/host/syntax_mc06.sh`) has 0 errors and 0 warnings on all new/changed lines with
real prototypes for every new Toolbox call. CodeWarrior may note "possible implicit conversion" on
`(McAAProc)sym` (a data Ptr to a function pointer: correct on PPC, a TVector) and on multi-char
constants 'MCdb'/'dump'/'apvr' (as 0.5u's 'TVOD').
Things only the guest can prove (NEEDS-GUEST): plainDBox (procID 4) draws a 1-pixel outline outside the 480x360 content (1984 = floating window, no longer used);
GetFNum finds "MC Garamond" in the app's own fork; TextEdit inside a 32-bit GWorld (caret, selection).

## 6. What changed (files)
- MediaCenter.c: version 0.6 (header, user agent, first log line); "dispatcher" lines at the top of
  UIStatus, Tell, CbStatus, EnableDlgItem, GetCheck, SetCheck, GetTerms, FlashButton, ClearList,
  ListInval, SelectRow, FillResults, SetFileRow, FillFiles, SelectedRow, Busy, UpdateStop, PlayFile, DoUpdate,
  CbIdle, StreamPass, EventLoop, main (dialog code below each untouched); DoQuit returns mcs_quit's verdict;
  the new skin block (SkinOpen/Close/Flush/Paint/Menus/About/Track/..., the 'MCdb'/'dump' handler).
- mc_fetch.h/.c: `McfEnv.progress(ref, done, total, ticks)` beside the "Downloading:" status line (NULL-safe).
- mc_core.h/.c: `McFile.note` (MC_FILE_NOTE_STUTTER) + `mc_avc_last_stutter()`; mc_fetch sets the note
  with the verdict (log lines unchanged: the MARK token is the same).
- New: mc_ui.c/.h (controller), mc_demo.c/.h (scenes, font test, dump header), mc_layout/mc_skin (U1b;
  0.6: no collapse button), test06/ (mc_ui_test, run_ui_test.sh, run_proof.sh, golden.txt, mc_key_test,
  mc_skin_render, mc_mock.py, dump2png.py), tls18/host/syntax_mc06.sh + macstub06/, guest/build_06.applescript,
  guest/send_06.sh, font/ (MC Garamond .rsrc/.r/.rdump/.idump/.ttf, OFL.txt, FONTLOG.txt).

## 7. b25b review fixes (B_REVIEW.md)
1 PlayFile: LSetDrawingMode only `if (!gSkin)`. 2 field click: TEClick runs with inPort = window port, clipped to the field, then inPort back to the GWorld and W_FIELD repainted; SkinClickLoop only saves/restores the port around mcs_poll (no AddCopy/SkinCopy).
3 plainDBox always. 4 mc_layout.c: `running = ui->busy` (Esc/Cmd-. while only serving = Back; busy = Stop; never quit); mc_key_test and mc_ui_test updated (same counts).
5 SkinPaint/SkinCopy/DoUpdate skin branch/DumpScene save and restore the GWorld port. 6 About: ModalDialog replaced by its own WaitNextEvent loop (0.5u serving sleep, StreamPass every pass, updates for our window, IsDialogEvent/DialogSelect, Return/Enter/Esc/Cmd-. close); AboutFilter removed (a filter would be dead code).
NEEDS-GUEST, left alone: finding 8 (stuck caret / blink phase after a field repaint) and finding 9 (no autoscroll when dragging past the field edge: custom click loop). Also: About/menus while streaming, DragWindow still blocks polling (as 0.5u).
