#!/bin/sh
set -eu
cd "$(dirname "$0")"
# Keep downloaded GPL source/license alongside binary; no system installation.
commit=62719249ac34633338be54bc74beddd0e7003d38
archive=captdriver.tar.gz
curl -fL --proto '=https' "https://github.com/mounaiban/captdriver/archive/$commit.tar.gz" -o "$archive"
printf '%s  %s\n' cb5977b0a5360431520c4770ca0bec7f414712717fd720d6feecf0cf71feffb8 "$archive" | sha256sum -c -
tar -xzf "$archive"
source_dir="captdriver-$commit"
# Disable upstream decoder tracing; retain modified source for GPL compliance.
sed -i '/^#include<stdio.h>/a #define fprintf(...) ((void)0)' "$source_dir/tests/hiscoa-decompress.c"
# Upstream emits an extra padding word when already 32-bit aligned.
# Restore alignment guard so decoder consumes each band exactly.
sed -i 's@//if (state.bitpos % 32)@if (state.bitpos % 32)@' "$source_dir/src/hiscoa-compress.c"
cc -std=c99 -O2 -Wall -Wextra -I"$source_dir/src" -I"$source_dir/tests" compress.c \
  "$source_dir/src/hiscoa-compress.c" "$source_dir/src/hiscoa-common.c" \
  "$source_dir/tests/hiscoa-decompress.c" -o capt-compress
