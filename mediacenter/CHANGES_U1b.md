# CHANGES_U1b (20 Nov 2026)

Work copy of u1; nothing else touched. Proofs: `src/run_proof.sh --bless` (golden.txt re-written deliberately).

## Part A: model + mock-up
1. Aqua only: unchanged.
2. Headings/title are drawn with the generated MC Garamond TTF; all Hoefler outputs and the compare sheet removed;
   PNG names are now `<scene>.png` / `<scene>@2x.png` (no `_garamond` suffix).
3. Esc / Cmd-. = cancel/back only, never quit (`mc_ui_key`: Stop while busy/serving, cancel the big-download question, Back on Files).
   Quit = Cmd-Q / close button / Quit Apple event. ARCHITECTURE_05.md 4 has the new key table; UI_INVENTORY.md notes the supersession.
4. New keys in `mc_layout.c` (`mc_ui_key`, `mc_ui_act_button`): Tab field<->list; arrows / PgUp / PgDn / Home / End move the selection
   (Home/End go to the field when it has focus); Return = Search in the field, Play in the list; Enter and Cmd-P = Play anywhere;
   typing from the list moves focus to the field. Host test `src/mc_key_test.c`: 76 checks (x86-64 and qemu-ppc).
5. "may stutter": `McRow.note` (`MC_NOTE_STUTTER`); dot and mark derived (`mc_ui_row_dot/mark`): orange dot (palette entry changed
   from amber to orange) + "may stutter" in the mark column; never overrides a "will not play" row. Sample row 1x03 uses it.
6. Close button = Quit. While `ui->serving` the first Quit (button or Cmd-Q) shows an LCD warning ("Still streaming. Quit again to stop and quit."),
   the second quits (`mc_ui_quit_request`, `ui->quit_pending`). New scene `6b_quit_warning`.
7. Stop enabled while `busy || serving` (already in sync; now tested). ARCHITECTURE_05.md 10: the shell must first ask QuickTime Player (Apple
   event) to close the streaming movie, wait ~2 s, then close the connections (else QuickTime's modal "cannot be completely downloaded" dialog).
   Scene 6_streaming now sets `serving`.
New model fields: `McUi.confirm`, `.quit_pending`, `McRow.note`. Scenes: 10 (was 9). Golden: see `src/golden.txt`.

## Part B: font (host part of U2), `font/`
See `font/FONTLOG.txt`. MC Garamond = EB Garamond 12 Regular, MacRoman subset (216 glyphs), 86% condensed, TrueType, renamed, OFL kept.
`MC Garamond.rsrc` = resource fork with `FOND` 279 "MC Garamond" (677 B) + `sfnt` 279 (72,368 B), 73,391 B total. `.rdump` round-trips to the
identical fork through the kit's `macresources_min`. `.idump` = `FFILDMOV`. Copy to the guest as `NAME` (empty data fork) + `NAME.rdump` + `NAME.idump`.
Not verified on a real Font Manager (U3 does that: GetFNum from the app's own fork, ID clash, smoothing at 14-18 px). Italic not made.
