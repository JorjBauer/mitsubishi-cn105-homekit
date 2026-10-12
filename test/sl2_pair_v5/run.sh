#!/usr/bin/env bash
set -euo pipefail
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/serin-native-pair-tests.XXXXXX")
trap 'rm -rf "$BUILD_DIR"' EXIT
cd "$(dirname "$0")"
gcc -std=c11 -Wall -Wextra -Werror -I../../main \
    test_sl2_pair_v5.c ../../main/sl2_link.c -lm -o "$BUILD_DIR/test_sl2_pair_v5"
"$BUILD_DIR/test_sl2_pair_v5"
