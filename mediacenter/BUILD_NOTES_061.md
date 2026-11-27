# Media Center 0.6.1 (b26c) -- G3 policy

Host-proved only (Linux: x86-64 ASan/UBSan + qemu-ppc). Nothing here has
run on a Pismo yet; the constants are first guesses from the data below,
named so the coming Pismo measurements can retune them in one place.

## What a G3 does now
- CPU facts (MediaCenter.c `IsG3`, `CpuMHz`, `SetupStreaming`): G3 =
  Gestalt 'cput' 0x0108/0x0120 (unchanged); clock = Gestalt 'pclk' (Hz,
  every PowerPC Mac OS), fallback 'mclk' (MHz, newer), else unknown ->
  `MC_G3_MHZ_UNKNOWN`. `mc_set_cpu()` hands both to mc_core. Host:
  `MC_CPU_G3=1 MC_CPU_MHZ=500`.
- Verdict (mc_core.c `mc_g3_verdict`, `mc_g3_line`, called from
  `mc_avc_verdict_at` for any H.264 that plays): MBs/s =
  ceil(w/16)*ceil(h/16)*fps; fps and B-frames from the video trak
  (`mc_timing`: mdhd + stts, ctts present = B-frames); unknown fps = 30,
  said in the text. List mark: "may stutter" or "too heavy" (orange dot).
  MPEG-4 Part 2 (mp4v) stays QuickTime-native: no budget, no AVC check.
  G4/unknown CPU: the 0.4b size rule only, unchanged.
- File choice (mc_fetch.c `mcf_play_file` + mc_core.c `mc_g3_lighter`):
  a file over its budget with a lighter rendition of the same film
  (same IA "original" stem) plays that one; LCD: "G3: playing the
  320x240 MPEG-4 version." No lighter one: it plays, LCD says "G3: too
  heavy for this Mac; expect a slideshow."
- Stream vs download (mc_stream.c `mcs_net_download`, in
  `mcs_proxy_url` right after the pre-buffer): measured KB/s < film KB/s
  x margin -> proxy_url returns -3 and TryCand downloads with the moov
  it already holds (big-download confirm as before; no dialog).
- G3 hint (mc_stream.c): no longer recommends "Download first" for a
  slideshow; Download first only cures a slow network.

## Constants and what retunes them
| constant | value | retune from |
|---|---|---|
| MC_G3_MBS_PER_MHZ_BASE | 40 MB/s per MHz | decoder log ms/call, Baseline 640x480 / 320x240 on the Pismo: 1200 or 300 MB / (ms/1000) / MHz |
| MC_G3_MBS_PER_MHZ_MAIN | 30 | the same for Main (480x360 and 640x480, no B) |
| MC_G3_BFRAME_PCT | 110 % | ms/call of a B-frame film / the same size without B |
| MC_G3_PLAYS_PCT | 75 % | paint ms per frame (Thousands vs Millions) + sound + proxy: 100 - their share of a frame period |
| MC_G3_STUTTER_PCT | 125 % | the budget % at which the Pismo looks like a slideshow (not just dropped frames) |
| MC_G3_FPS_UNKNOWN | 30 | none (only when mdhd/stts are missing) |
| MC_G3_MHZ_UNKNOWN | 400 | the Gestalt 'pclk' value the Pismo logs ("CPU type 0x0108, N MHz") |
| MC_G3_MPEG4_W/H | 320x240 | only for the LCD text when IA gives no width |
| MCS_STREAM_MARGIN_PCT | 125 % | PREBUF KB/s ("Buffered ... in N ticks") vs "during play" KB/s: Pismo 98 vs 72 KB/s says ~1.35 |
| MCS_NET_MIN_BYTES / TICKS | 128 KB / 30 ticks | none expected (guards a too-short sample) |
| MCS_G3_KBS | 64 KB/s | unchanged: when the honest CPU hint shows |
