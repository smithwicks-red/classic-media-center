#!/bin/sh
# syntax_mc06.sh -- HOST ONLY (0.6). gcc -fsyntax-only over the Mac-only
# MediaCenter.c (+ H264Stream.c, mc_ui.h, mc_demo.h) against host/machdr +
# host/macstub05 + host/macstub06 (types and prototypes only; nothing
# linked). Every Toolbox call the 0.6 code adds has a real prototype in
# macstub06/mc06_syntax.h, so a wrong argument type is a warning here.
# Pass = 0 errors AND 0 warnings on any line that is new or changed
# since 0.5u (lines found with difflib against the 0.5u MediaCenter.c,
# path $BASE, default: the copy beside this tree).
#   sh host/syntax_mc06.sh [path/to/0.5u/MediaCenter.c]
cd "$(dirname "$0")/.."
BASE=${1:-../../../base/b24-mediacenter-0.5u/mediacenter/MediaCenter.c}
gcc -m32 -std=c89 -fsyntax-only -Wall -Wextra -Wno-unused-parameter -Wno-multichar -Wno-unknown-pragmas \
  -Wno-sign-compare -Wno-missing-field-initializers \
  -include host/host_prefix.h -Ihost/machdr -include host/macstub06/mc06_syntax.h \
  -Ihost/macstub06 -Ihost/macstub05 -I. -Imactls/os9 -Imactls/bearssl/inc -I../mediacenter -I../proxy/src \
  ../mediacenter/MediaCenter.c 2>&1 | grep -E "error|warning" > /tmp/mc06_syntax.txt
python3 -I - "$BASE" ../mediacenter/MediaCenter.c /tmp/mc06_syntax.txt <<'PY'
import difflib, re, sys
old = open(sys.argv[1], encoding="latin-1").read().splitlines()
new = open(sys.argv[2], encoding="latin-1").read().splitlines()
changed = set()
for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, old, new, autojunk=False).get_opcodes():
    if tag != "equal":
        changed.update(range(j1 + 1, j2 + 1))
errs = newwarn = oldwarn = 0
for line in open(sys.argv[3]):
    m = re.match(r".*?([A-Za-z0-9_]+\.[ch]):(\d+):\d+: (error|warning)", line)
    if not m: continue
    if m.group(3) == "error":
        errs += 1; print("  " + line.strip()); continue
    if m.group(1) == "MediaCenter.c" and int(m.group(2)) in changed:
        newwarn += 1; print("  NEW: " + line.strip())
    else:
        oldwarn += 1
print("MediaCenter.c 0.6 syntax: %d errors; %d warnings on the %d new/changed lines; "
      "%d warnings elsewhere (stub gaps in 0.4/0.5u code and H264Stream.c)" % (errs, newwarn, len(changed), oldwarn))
sys.exit(1 if errs or newwarn else 0)
PY
