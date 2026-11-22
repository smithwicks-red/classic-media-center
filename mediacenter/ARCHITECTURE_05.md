# Media Center 0.5 ("QuickTime 6 look"): architecture

Refined from MEDIA_CENTER_GUI_PLAN.md against UI_INVENTORY.md (same folder). Decisions from smithwicks-red (20 Nov):
**Aqua only** (one palette; the colours live in ONE table, `mc_skin.c: kPal`, so Graphite later = one table + a flag),
**fixed 480x360**, heading serif **= Garamond-style, bundled as "MC Garamond"** (see 12; the Hoefler variants are dropped),
**rectangular window** (rounded corners = a later phase), H264 Stream reskin **deferred** until 0.5 is done.

U1b (20 Nov, after smithwicks-red's comments): Esc / Cmd-. never quit (4); new key map (4); per-file "may stutter" note (4b);
Quit while streaming = LCD warning + second Quit (10); **Stop also ends streaming** (10).

## 1. Module split
| module | portable? | contents |
|---|---|---|
| `mc_skin.c/.h` | C89, no Toolbox | pixel painter on an `McCanvas` (xRGB, big-endian byte order, written byte by byte; any rowBytes, so it paints straight into a GWorld's PixMap): metal (a pure function of x,y: no cache), bevels, anti-aliased rounded rects, gel buttons (normal/pressed/disabled x primary/plain), round window widgets, field well, list well + rows + selection bar + slim scroller, LCD well + progress bar, Aqua checkbox, status glyphs. Clips every primitive to a dirty rect. |
| `mc_layout.c/.h` | C89, no Toolbox | the UI model `McUi` (screen, enabled/pressed/checked flags, list rows, scroll, focus, LCD lines, progress), the widget table with rects, `mc_layout_sync` (= today's UpdateButtons + Busy rules), hit-testing, list scrolling/keyboard maths, dirty-rect bookkeeping, and **text runs**. `mc_ui_render(ui, canvas, clip)` = layout + skin for one rect. |
| `MediaCenter.c` (Mac shell) | Toolbox | window + GWorld, CopyBits, QuickDraw text from the runs, TextEdit field, mouse tracking, menus, 256-colour fallback (the 0.4 dialog code kept). The network/moov half is untouched. |
| `mc_skin_render.c` + `mc_mock.py` | host only | builds sample `McUi`s for every screen, writes raw buffers + run lists; Python draws the runs with TTFs and writes PNGs. |

## 2. Widget table at 480x360 (l,t,r,b; `mc_layout.c: kRects`)
| id | kind | rect | notes |
|---|---|---|---|
| W_TITLE | title strip | 0,0,480,24 | serif title, embossed; whole metal is a drag area |
| W_CLOSE | round button | 9,6,21,18 | = Quit (replaces the Quit button; while streaming: warning + second Quit) |
| W_SHADE | round button | 25,6,37,18 | collapse (WindowShade-like, phase 1 optional) |
| W_FIELD | search field | 12,31,382,53 | white rounded well; TextEdit lives inside 22,35,372,50 |
| W_SEARCH | gel button | 390,30,468,54 | primary (aqua) on the Search screen |
| W_HEADING | serif heading | 14,58,466,76 | NEW: "Results for "star trek"" / item title; right: small count |
| W_LIST | list well | 12,77,468,239 | inset white; rows 15,79..453 x 16 px = **10 rows**; scroller 455,79,466,237 |
| W_LCD | LCD well | 12,245,468,291 | line 1 (headline, 11 px), line 2 (detail, 9-10 px), progress bar 22,281,446,286, right-hand figure (KB/s) |
| W_DLFIRST | checkbox | 14,298,124,312 | "Download first" |
| W_INSECURE | checkbox | 132,298,352,312 | "Skip certificate checks (unsafe)" |
| W_BACK | gel button | 12,321,80,345 | |
| W_MORE | gel button | 86,321,186,345 | "More results" |
| W_STOP | gel button | 300,321,372,345 | enabled while busy OR serving (also ends streaming, 10) |
| W_PLAY | gel button | 384,321,468,345 | primary (aqua) on Results/Files |

Screens are not new state: `screen = f(gListMode, gBusy, list count, confirm)`: Search (empty), Results, Files,
Working (any of them with busy=1: every control drawn disabled except Stop, as CbIdle already ignores them), plus the
LCD "message" mode (warning glyph; used for errors and the big-download confirm). No modal alerts (project rule).

## 3. Text-run interface (`mc_layout.h`)
```c
typedef struct { short l, t, r, b; } McRect;
typedef struct McRun {
    char  text[128];     /* MacRoman/ASCII */
    McRect box;          /* clip + truncation width */
    short base;          /* baseline y */
    unsigned char role;  /* MC_F_TITLE, MC_F_HEAD (serif), MC_F_UI, MC_F_UIBOLD, MC_F_SMALL, MC_F_LCD */
    unsigned char size;  /* px = pt at 72 dpi */
    unsigned char align; /* MC_AL_LEFT / CENTER / RIGHT */
    unsigned char trunc; /* MC_TR_END / MC_TR_MIDDLE (TruncText smTruncEnd/Middle) */
    unsigned char rgb[3];
    unsigned char emboss;/* 1 = draw a white copy 1 px lower first (engraved metal text) */
} McRun;
int mc_ui_runs(const McUi *ui, const McRect *clip, McRun *out, int max);
```
Mac: role -> font number found once at start (`GetFNum`: "MC Garamond" (bundled, see 12), then "Apple Garamond", "Garamond",
"Palatino", "New York" as fallbacks for HEAD/TITLE; Geneva for SMALL/LCD; Charcoal(system font) for UI/UIBOLD),
`TextFont/TextSize/TextFace`, `RGBForeColor`, `TruncText`, `MoveTo`, `DrawText`, with the GWorld as port.
Host: the same list as TSV -> PIL.

## 4. Events and redraw rules
- **Model first, pixels later:** every handler changes `McUi`; `mc_ui_dirty(ui, widget)` unions the widget rect into
  `ui->dirty`. One function `SkinFlush()` renders `ui->dirty` into the GWorld (skin, then text runs clipped to it),
  CopyBits that rect to the window, clears it. Called at the end of each event and from `CbIdle`.
- Update events: `BeginUpdate`; CopyBits of the visRgn bounds from the GWorld (already up to date); `TEUpdate`.
- Metal is a pure function of (x,y) (hash of row + smoothed streaks + vertical gradient), so any rect re-renders
  identically: no background cache (saves 691 KB).
- Mouse: `FindWindow`; inContent -> `mc_ui_hit(x,y)` -> widget/row/scroller part. Buttons: pressed while
  `StillDown` and inside (re-render only that button), fire on release inside. List: select row; double-click (`GetDblTime`)
  = Play; focus moves to the list. Scroller: arrows/track/thumb drag. Nothing hit = metal = `DragWindow`.
  Close = Quit (`mc_ui_quit_request`: while streaming the first click shows the LCD warning, the second quits); shade = collapse to the title strip (`SizeWindow` to 480x24, render strip only).
- **Keys** (portable, `mc_layout.c: mc_ui_key`; host test `src/mc_key_test.c`, 76 checks). `mc_ui_key(ui, charCode, mods)` returns an
  `MC_ACT_*` the shell carries out (and `mc_ui_act_button` names the button to FlashButton for 8 ticks):

  | key | does |
  |---|---|
  | **Return** | field has focus: **Search**; list has focus: **Play** |
  | **Enter**, **Cmd-P** | **Play**, from anywhere (ignored when Play is disabled) |
  | **Tab** | moves focus field <-> list (only if there are rows); the aqua button follows (Search <-> Play) |
  | Up / Down, Page Up / Page Down | move the selection (a page = 9 rows), also when the field has focus |
  | Home / End | list focus: first / last row; field focus: line start / end (given to the field) |
  | Left / Right | the field (ignored with list focus) |
  | **Esc**, **Cmd-.** | **cancel / back only, never quit**: busy or serving -> Stop (MC_ACT_STOP); else the big-download question showing -> cancel it; else on the Files screen -> Back; else nothing |
  | Cmd-[ | Back (Files screen) |
  | Cmd-X/C/V/A | cut/copy/paste/select-all in the field |
  | **Cmd-Q**, the **close button**, the Quit Apple event | **Quit** (`mc_ui_quit_request`, see 10 for the streaming case) |
  | any other character | typing: focus jumps to the field, the key goes to TextEdit |

  While busy only Stop (Esc / Cmd-. / the button) and Quit work; every other key is swallowed. Any key other than a second
  Quit forgets a pending quit warning.
- Per-file note (4b): `McRow.note` (`MC_NOTE_STUTTER`). The row's dot and mark are *derived* (`mc_ui_row_dot`, `mc_ui_row_mark`):
  a STUTTER note shows an **orange dot + "may stutter"** (same slot as the other marks) unless the file is already
  "will not play" / "may not play?". The shell fills `note` from the verdict it keeps per file (`McFile.note` in `mc_core`).
- Busy: `Busy(1)` sets `ui->busy` (everything but Stop disabled, watch cursor), `CbIdle` serves updates, drags, and Stop
  via `mc_ui_hit` == W_STOP (with pressed feedback).
- Menus (NEW, there are none today): Apple (About), File (Quit Cmd-Q), Edit (Cut/Copy/Paste/Select All for the field).

## 5. TextEdit in a well
One real TE record (`TENew`) in the window port, viewRect/destRect = field interior 22,35,372,50 (inside the rounded
caps and below the 2-px inner shadow). Before any TE call: `SetPort(window)`, `RGBBackColor(white)`, `RGBForeColor(black)`,
`ClipRect(interior)`; afterwards restore. TE then erases to white and never touches the metal. After a SkinFlush that
covers the field, call `TEUpdate(interior)` (the GWorld holds an empty white field; TE text is drawn on top, on the
window). Caret blink: `TEIdle` on null events. Placeholder ("Search the Internet Archive for films") is a grey text run
drawn only when the TE is empty and not focused. Cut/Copy/Paste via TECut/TECopy/TEPaste + scrap, as 0.4.

## 6. 256 colours
At launch: main device `pixelSize >= 16` AND the GWorld allocates -> skinned UI; otherwise the 0.4 dialog path, kept
verbatim behind `gSkin == 0` (the dispatcher functions below pick the path). Depth changed while running: keep the
skin, CopyBits with `ditherCopy` (ugly but correct); a relaunch gives the Platinum UI.

## 7. Memory
- GWorld 480x360x32 = **691,200 B**, `NewGWorld(.., useTempMem)` first, app heap second, else the 0.4 path.
- No metal cache; text runs on the stack (40 x ~150 B); `McUi` ~ 12 KB (64 rows x 160 chars + flags).
- Today 3072K min / 4096K preferred: keep min, raise **preferred to 4608K** so the GWorld can live in the heap when temp
  memory is short.
- CPU: full-window render ~170k px; metal ~25 ops/px: estimated 30-60 ms on a 400 MHz G3, so full redraws only on
  open/resume; an LCD update (20k px) ~5 ms.

## 8. MediaCenter.c: what changes
- **Unchanged:** Tell, CbLog, CbAlloc, CbRelease, CbRebuild, CbOpenRef, CbOpenLocal, DecoderLevel, DoSearch, PlayItem,
  OpenItem (except a heading update), DoBack, DoPlay, DoSearchBusy, ReadOptions, AEQuit, AEIgnore, InstallAEHandlers,
  ReadQueryFile, HookRun; AddItem, MakeDialog, MakeList, DrawDefaultRing (now the 256-colour path only).
- **Become dispatchers (skin or dialog):** UIStatus, EnableDlgItem, GetCheck, SetCheck, GetTerms, FlashButton,
  ClearList, ListInval, SelectRow, FillResults, SetFileRow, FillFiles, SelectedRow, Busy, UpdateButtons.
- **Rewritten for the skin:** DoUpdate, DoKey, DoMouse, EventLoop, CbIdle (hit-test Stop, drag on metal, flush),
  CbStatus (LCD headline), SetupEnv (+progress callback), main (depth check, window, menus, GWorld, TE).
- **New:** SkinOpen/SkinClose, SkinFlush, SkinDrawRuns, SkinTrackButton, SkinListClick, SkinScroll, SetupMenus/DoMenu,
  CbProgress, DepthOK.
- **Outside MediaCenter.c (small, host-testable):** `mc_fetch.h/.c`: one McfEnv field
  `void (*progress)(void *ref, unsigned long done, unsigned long total, unsigned long ticks)` called beside the existing
  status line (mc_fetch.c:808-816), NULL-safe, so the LCD gets % and KB/s without parsing text. Optional `mc_core`: keep the
  verdict per file (`McFile.note`, e.g. MC_NOTE_STUTTER) so a checked row can read "may stutter" (today only a sentence).
- **Status text split:** `Tell`/`CbStatus` -> LCD line 1 (headline); `Say` log lines -> LCD line 2 (detail, small
  grey, truncated middle). Today both overwrite one line.

## 9. Jobs
| job | what | estimate |
|---|---|---|
| U1 | this: inventory, architecture, host mock-ups, proofs | done in this session |
| U1b | look revisions after smithwicks-red's comments (cheap: constants) | 30-60k tokens per round |
| U2 (host part DONE, 20 Nov) | bundled Garamond: EB Garamond 12 Regular (OFL) -> fontforge: subset to MacRoman, condense ~85%, rename (e.g. "MC Garamond"), TrueType outlines -> 'sfnt' + 'FOND' resources in the app (Rez on the host, or a suitcase moved with ResEdit on the guest); ship OFL.txt. App +60-120 KB. Unknowns: FOND generation for OS 9, smoothing at 14-18 pt | 1 session, 80-150k tokens, 1 VM build |
| U3 | Mac shell + CodeWarrior build in the VM + hook-run regression (log unchanged) + screenshots at Thousands/Millions/256 | 1.5-2 sessions, 250-350k tokens |
| U4 | Mini G4 (a tester): look, drag, TE caret, QuickTime hand-off and back, Pismo speed | 1 short session + a tester's time |
| later | rounded corners (WDEF or region), H264 Stream reskin | after 0.5 |

## 10. What the https streaming work will need from the UI (generic)
- LCD "streaming" mode: headline "Streaming via secure proxy" (or the agent's wording), detail line with the upstream
  host, the progress bar used as a **buffer gauge** (buffering %, before QuickTime starts), right-hand figure KB/s.
  Model fields already planned: `lcd_head`, `lcd_detail`, `progress` (0-1000 or -1 = none), `rate_kbs`, `glyph`
  (none/busy/warn/stream). A streaming job only calls `mc_ui_set_progress()` / `mc_ui_set_lcd()`.
- **Stop also ends streaming.** Stop is enabled whenever `ui->busy || ui->serving` (`mc_ui_sync`); Esc / Cmd-. do the same
  (MC_ACT_STOP). The shell's Stop handler (button or key) must, **in this order**:
  1. call `mc_ui_note_stop(ui)`, set gCancel (ends a running fetch/buffering loop);
  2. if streaming (`ui->serving`): **first ask QuickTime Player to close the streaming movie** with an Apple event
     (`aevt`/`clos` to QuickTime Player, `'MooV'`/`'TVOD'`; target by creator signature, via the Process Manager if it
     is running; for the front document: `core`/`clos` on the window/document), then **wait about 2 s** (keep serving
     updates and the local server's reads meanwhile: the idle loop keeps running), and only then
  3. close the proxy connections / stop the local server (`gEnv.stop_server` or similar, NULL when no server), clear
     `ui->serving`, `mc_ui_sync`.
  **Why this order:** cutting the data off while QuickTime is still playing the movie makes QuickTime Player raise a
  modal "the movie cannot be completely downloaded" dialog (project rule: nothing modal on our side, and this one is
  QuickTime's, so we must never provoke it). If QuickTime Player is not running (or the Apple event is refused), skip
  straight to the 2 s wait, then close.
- **Quit while streaming = an LCD warning + a second Quit, never a modal alert.** `mc_ui_quit_request` (Cmd-Q, close
  button, Quit Apple event): if `ui->serving` the first request sets the LCD to "Still streaming. Quit again to stop and
  quit." (warning glyph, `ui->quit_pending`) and returns 0; the second returns 1, and the shell then runs the Stop
  sequence above before leaving. Any other key / click clears the warning (the shell restores its status line when
  `quit_pending` drops). Idle (not serving) quits at once.
- The idle loop must keep serving while QuickTime Player is frontmost (suspended app): the UI side only promises not to
  block (no long redraws in the background; skip SkinFlush while suspended except the LCD).

## 11. Host mock-up (job U1, done): files, fonts, proofs
- Code: `src/mc_skin.c/.h` (painter, ~700 lines), `src/mc_layout.c/.h` (model, table, hit-test, runs),
  `src/mc_skin_render.c` (10 sample scenes), `src/mc_key_test.c` (76 key-map checks), `src/mc_mock.py` (text + PNGs), `src/run_proof.sh`, `src/golden.txt`.
  No floating point anywhere (anti-aliasing in 1/16-px integer units, integer sqrt), so no FPU/fmadd differences.
- Rules the mock-up settled: the aqua (Return) button is Search while the field has focus, Play while the list has it,
  none when the window is inactive; while busy everything but Stop is disabled, the list stays readable and only its
  selection turns grey.
- Host fonts: headings = the generated MC Garamond TTF (12); Charcoal/Geneva = DejaVu Sans Condensed (bold for LCD figures). All anti-aliased (on the Mac, Geneva 9-10
  will be crisp bitmap text, a touch sharper than the PNGs).
- Proofs (`src/run_proof.sh`): `-std=c89 -pedantic -Wall -Wextra -Werror` clean; dirty-rect re-render in 37x29 tiles is
  byte-identical to a full render (all scenes); two runs identical and equal to golden.txt (re-blessed deliberately in U1b: orange dot, the may-stutter note, the new quit-warning scene, serving flag in the streaming scene); 32-bit big-endian PowerPC
  build under qemu-ppc gives the same CRCs and byte-identical .xrgb/.runs files, and the key-map test passes there too.

## 12. U1b / U2 (20 Nov): the bundled heading font
- **MC Garamond** = EB Garamond 12 Regular (SIL OFL 1.1, Georg A. Duffner / Siva Kalyan), fontforge: MacRoman subset (216
  glyphs + .notdef/.null/CR), GSUB/GPOS dropped, outlines scaled 86% horizontally (the mock-up's condense), TrueType
  quadratic outlines, 2048 em, auto-hinted, **renamed** (family "MC Garamond"; no original name in any name field, as the
  OFL requires of a modified version). Files in `font/`: `MCGaramond-Regular.ttf` (72 KB), `MC Garamond.rsrc` (the raw resource
  fork: `FOND` 279 "MC Garamond" + `sfnt` 279, 73,391 bytes), `.rdump/.idump` ('FFIL'/'DMOV') for the share, `MC Garamond.r` (Rez
  source: `Rez` it into the app, or copy the two resources with ResEdit), `OFL.txt`, `FONTLOG.txt`, `build_mcgaramond.py`
  + `finish_font.py` (reproducible).
- App cost: ~73 KB resources. To verify in U3: that Font Manager finds "MC Garamond" by `GetFNum` when it lives in the
  application's own resource fork (otherwise install the suitcase in the Fonts folder), that FOND ID 279 does not clash, and
  how the Apple TrueType scaler smooths it at 14-18 px.
- The mock-up PNGs now draw headings with this very TTF (`src/mc_mock.py`).
