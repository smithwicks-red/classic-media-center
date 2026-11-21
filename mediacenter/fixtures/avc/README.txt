(0.4) moov boxes of tiny libx264 clips for mc_test04.c (real SPS bytes).
Made with /usr/local/bin/ffmpeg -f lavfi -i testsrc2=size=320x240:rate=25:duration=0.4
-c:v libx264 -profile:v P -pix_fmt F [extra] X.mp4, then getmoov.py X.mp4 X.moov:
base (baseline), main, main_bf0 (-bf 0), fakeint (main, -x264-params
fake-interlaced=1:pic-struct=1), high, high_mov (-f mov), high_cqm (custom
cqm4iy/cqm8iy: x264 puts them in the PPS), hd720 (1280x720), big1080 (1920x1080),
high10 (yuv420p10le), high422 (yuv422p), high444 (yuv444p), interlaced
(-flags +ildct+ilme -x264-params interlaced=1).
hand_sps: python3 mksps.py 4194304 (SPS with scaling matrices, poc type 1,
one emulation-prevention byte, MBAFF; ffmpeg trace_headers reads it the same),
then swapsps.py high.moov sps.bin hand_sps.moov.
