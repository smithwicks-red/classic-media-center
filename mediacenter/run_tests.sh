#!/bin/sh
# mc_core host tests (gcc, C89, ASan+UBSan). 0.3: first rebuilds the
# synthetic Jimmy metadata (139 KB and ~700 KB) from fixtures/jimmy_files.txt.
# 0.4: then mc_test04 (decoder level, SPS reader, Main/High gate).
cd "$(dirname "$0")" && mkdir -p fixtures/gen \
 && python3 -I fixtures/gen_meta_jimmy.py fixtures/jimmy_files.txt fixtures/gen/meta_jimmy.json \
 && python3 -I fixtures/gen_meta_jimmy.py fixtures/jimmy_files.txt fixtures/gen/meta_jimmy_big.json 700000 \
 && gcc -std=c89 -pedantic -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer mc_core.c mc_test.c -o /tmp/mc_test && /tmp/mc_test fixtures \
 && gcc -std=c89 -pedantic -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer mc_core.c mc_test04.c -o /tmp/mc_test04 && /tmp/mc_test04 fixtures
