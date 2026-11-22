# UI inventory: Classic Media Center 0.4b (b22), as the code stands

Source read: `/home/claude/s7/src04b/src/mediacenter/MediaCenter.c` (1036 lines), `H264Stream.c` (Say, hand-off),
`mc_fetch.c/.h`, `mc_core.c/.h`, BUILD_NOTES*.md. All line numbers are MediaCenter.c unless named.
Items marked **PLAN WRONG** contradict MEDIA_CENTER_GUI_PLAN.md (which was written from memory).

## 1. The window
- ONE Dialog Manager dialog, built in code: a DITL assembled byte by byte by `AddItem` (200-222), `MakeDialog` (224-255).
- Size **520 x 400** (`kDlgW/kDlgH`, 105-106), centred horizontally, top at 1/3 of the free height + 20, never above y=44
  (247-251). **PLAN WRONG** about "~480x360 today": it is 520x400; 0.5 shrinks it.
- Window type `movableDBoxProc` (253): a Platinum movable modal box WITH a title bar "Classic Media Center" (252).
  No close box, no zoom, no collapse. Dragged by its title bar (`inDrag`, 851-853).
- No colour assumptions anywhere: every drawing call is 1-bit-safe QuickDraw (EraseRect, FrameRect, TETextBox,
  FrameRoundRect). Platinum colours come from the Appearance Manager via the standard controls. No GWorld, no RGBForeColor.
  So 0.4 runs identically at 256 colours, thousands and millions.

## 2. Items (DITL order = item numbers, 92-103 and 233-245; rects are top,left,bottom,right)
| # | const | kind | rect | text / role |
|---|---|---|---|---|
| 1 | kItemSearch | button (default) | 33,420,53,500 | "Search". Default ring drawn by hand, `DrawDefaultRing` (162-173): PenSize 3, inset -4, FrameRoundRect 16 |
| 2 | kItemQuit | button | 368,420,388,500 | "Quit" |
| 3 | kItemLabel | statText | 10,20,26,400 | "Search the Internet Archive for films:" |
| 4 | kItemTerms | editText | 35,23,51,400 | the search words (the dialog's TextEdit record) |
| 5 | kItemStatus | statText | 292,20,330,500 | the status line: 38 px = 2-3 lines of TETextBox (133-150) |
| 6 | kItemList | userItem, disabled | 68,21,284,499 | List Manager list, 1 column, vertical scroll bar inside (257-274), frame drawn in DoUpdate (187-189) |
| 7 | kItemPlay | button | 368,320,388,400 | "Play" |
| 8 | kItemMore | button | 368,20,388,130 | "More results" |
| 9 | kItemDlFirst | checkbox | 338,20,356,150 | "Download first" -> gEnv.force_download (304-308) |
| 10 | kItemInsecure | checkbox | 338,170,356,420 | "Skip certificate checks (unsafe)" -> gEnv.insecure |
| 11 | kItemStop | button | 368,230,388,300 | "Stop", enabled only while busy (679-695) |
| 12 | kItemBack | button | 368,140,388,220 | "Back" (0.3) |

**PLAN WRONG / missing from the plan's sketch:** the plan shows only Back, Download first, Play and Search. Today there
are also **Quit, More results, Stop**, and **two checkboxes** ("Download first" is a checkbox, not a button; "Skip
certificate checks (unsafe)" is not in the plan at all). The label "Search the Internet Archive for films:" also exists.

## 3. The list (List Manager, 257-274, 530-597)
- `LNew` 1 column, `selFlags = lOnlyOne`, cell size default (system font height, 16 px at Charcoal 12), scroll bar
  inside the userItem (`rView.right -= 15`).
- **One list, two modes** (`gListMode`, 121): 0 = search results, 1 = the files of item `gItem`.
  - Results row = the hit's title only (`FillResults`, 558-575). Up to `MC_MAXHITS` = 25 per page (mc_core.h:31).
  - File row = ONE pre-formatted string from `mc_file_row` (mc_core.c:946-957):
    `1x02 - Where No Man Has Gone Before   [h.264, 298.7 MB]  plays now?` (label cut at 60 chars).
- Marks (`mc_file_mark`, mc_core.c:918-926), 4 at each decoder level:
  - level 0 (h264bsd): `plays now?` / `needs newer decoder?` (guesses), `PLAYS NOW` / `needs newer decoder` (checked);
  - level 1 (decoder 17 + AVCDecLib): `plays now?` / `may not play?` (guesses), `PLAYS NOW` / `will not play` (checked).
  **PLAN WRONG:** there is **no "may stutter" mark** in the list. "Large for this Mac: it may stutter." is only a
  sentence in the status line/log after a check (mc_core.c:1394-1446 verdict; mc_test04.c:209-236). The row itself
  says `PLAYS NOW`. Showing "may stutter" as a mark needs mc_core to keep the verdict per file (small change).
  **PLAN WRONG:** there are **no durations** ("6:01", "50:12"): McFile (mc_core.h:193-202) holds name, title,
  format, size, width, height, hint, state. Rows show format + size in MB.
- Selection: click (`LClick`, 859-863), double-click = Play. **No arrow-key/Page key handling for the list**: keys go
  to `DialogSelect` (839), i.e. to the search field's TextEdit. **PLAN WRONG** ("arrows and Page Up/Down in the list,
  as today"): that would be new in 0.5.
- More than 64 files: "(first 64 shown)" in the message (721-723).

## 4. Screens / states (there is no "screen" variable; it is gListMode + gBusy + list contents)
| state | how reached | list | enabled buttons (`UpdateButtons` 697-703, `Busy` 679-695) | status text |
|---|---|---|---|---|
| Start / search (empty) | launch | empty | Search, Quit, checkboxes (Play/More/Stop/Back disabled, 1013-1016) | "Type a word or two (e.g. popeye) and press Return." (1022) |
| Results | Search / More | hits | Play if count>0; More if a full page (25) and mode 0; Back off | "N items found; showing a-b. Pick one and click Play to see its videos." (645-649) |
| Item files | Play/double-click/Enter/Cmd-P on a hit (`OpenItem` 706-725) | files with marks | Play; Back; More off | "N video file(s). Pick one, click Play. "plays now?" is a guess until you do." |
| Working (search, open item, moov check, download) | any of the above | unchanged | **nothing is redrawn as disabled** except Stop is enabled; clicks other than Stop are ignored by `CbIdle` (379-399); watch cursor | every log line (see 6) |
| Confirm big download | Play on a file > 32 MB (`MCF_BIG_BYTES`) | unchanged; `gConfirmRow` set (749) | as Files | "This film is 298 MB: about 12 minutes to download (at 430 KB/s). Click Play again to download it." (mc_fetch.c:1212-1218) + SysBeep |
| Hand-off done | play ok (743-748) | row re-marked (740) | as Files | "Opened in QuickTime Player. Press Play there." |
| Error | any failure | unchanged | as before | the explanation (`mcf_explain`, mc_fetch.c ~1040-1100) + SysBeep |

**PLAN WRONG:** there is **no message/error screen or alert**. Every message is the status line + `SysBeep`. (By
project rule nothing may raise a modal dialog in hook runs; keep it that way.) "Working/Download" is not a screen either:
it is a modal loop inside the fetch code whose only UI is `CbIdle` (356-405).

## 5. User actions and keys
- Mouse (`DoMouse` 842-879): drag title bar; click list (LClick; dbl-click = DoPlay); Search -> `DoSearchBusy(0)`;
  More -> `DoSearchBusy(1)`; Play -> `DoPlay` (results: OpenItem; files: PlayFile); Back -> `DoBack`; Quit; checkboxes
  toggle. Inactive window: first click only brings it forward (855).
- Keys (`DoKey` 815-840):
  - **Return = Search** (always, even with a list selection), **Enter = Play**, Cmd-P = Play, Cmd-[ = Back.
  - **Esc = Quit** (flashes Quit) and **Cmd-. = Quit** when idle; Cmd-Q = Quit.
  - Cmd-X/C/V/A = cut/copy/paste/select-all in the search field (via the scrap).
  - Everything else -> DialogSelect -> the search field.
  - While busy (`CbIdle`): Cmd-. or Esc = cancel; a click inside Stop's rect = cancel; drag works; updates and Apple
    events are served; nothing else.
  **PLAN WRONG:** "Esc and Cmd-. = cancel/back" is true only while busy; when idle they QUIT. "Return = default (Play)"
  is wrong: Return = Search, Enter = Play.
- `FlashButton` (323-333) hilites a button for 8 ticks for keyboard equivalents.
- Confirm-by-second-Play: `gConfirmRow` (123, 732-733, 749); any other Play forgets the question.

## 6. Status and progress paths (all text, one line)
- `UIStatus(line)` (133-150) erases the status rect and `TETextBox`es the string.
- Called from: `Say()` in H264Stream.c:219-251 for **every log line** (newlines folded, 250 chars), `Tell()` (153-156,
  which is Say), `CbStatus` (348-352, mc_fetch's status-only lines), `CbLog` (342-346, via Say).
  So the user sees raw log lines too ("GET https://...", "Trying 1x02-... (h.264, 305914 KB)").
- Progress is **text only**: `"Downloading: %lu of %lu KB (%d%%). Cmd-period stops."` every 30 ticks
  (mc_fetch.c:808-816). **There is no progress bar and no KB/s figure** during a download (KB/s appears once, at
  the end, in the log: mc_fetch.c:1013). **PLAN WRONG** ("redraw the status well from the existing progress
  callback"): there is no numeric progress callback; 0.5 needs one (or must parse the string).
- `gEnv.message` holds the last user-facing explanation (mc_fetch.h:66).

## 7. QuickTime hand-off
- Stream: `CbOpenRef` -> `SaveAndOpenMovie` (H264Stream.c:1728+): reference movie saved on the **Desktop**
  ("name.mov", never overwritten: "name 2.mov" ...), then opened via the Finder.
- Download: `CbOpenLocal` (423-436) -> `OpenWithFinder` (H264Stream.c:1686+): an 'odoc' Apple event to the Finder
  ('MACS') for the file in "Media Center Downloads" beside the app.
- QuickTime Player then comes to the front; Media Center gets a suspend (osEvt, 914-926).

## 8. Menus
- `InitMenus()` is called (981) but **there is no menu bar at all**: no MBAR, no Apple menu, no File/Edit. Quit is
  the button, Cmd-Q, Esc, Cmd-. or the Quit Apple event (790-813). **PLAN WRONG** ("the menu bar should still offer
  Quit"): it never did. 0.5 should add a minimal one (Apple/File/Edit) because the Quit button goes away.

## 9. Other facts that matter to the redesign
- Memory: partition 3072K min / 4096K preferred (MC03_GUEST_NOTES). Globals: McSearch ~6 KB, McMeta ~26 KB, URLs 7 KB.
- Activation: `LActivate` + DialogSelect on activate/resume (902-926); TEFromScrap on resume.
- Idle: `WaitNextEvent(.., 15, ..)` then `DialogSelect` on null events to blink the caret (889-892).
- Hook mode (`Media Center Query` file): the dialog is shown, `HookRun` (960-973) sets the checkboxes and runs
  `mcf_hook_run` with no event loop; the guest test scripts depend on this and on the log text, not on pixels.
- The network/moov half (H264Stream.c include, mc_fetch, mc_core, DecoderLevel, SetupEnv callbacks except
  status/idle) is UI-free and stays as is.

---
U1b note (20 Nov): the 0.4 behaviours above marked "Esc = Quit", "Cmd-. = Quit when idle" are **superseded**: in 0.5 Esc / Cmd-.
are cancel / back only and never quit; see ARCHITECTURE_05.md section 4 for the full 0.5 key map.
