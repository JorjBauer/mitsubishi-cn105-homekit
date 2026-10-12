#!/usr/bin/env bash
set -euo pipefail
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/serin-native-rxq-tests.XXXXXX")
trap 'rm -rf "$BUILD_DIR"' EXIT
cd "$(dirname "$0")"
gcc -std=c11 -Wall -Wextra -Werror -I../../main \
    test_sl2_rxq.c -o "$BUILD_DIR/test_sl2_rxq"
"$BUILD_DIR/test_sl2_rxq"
RXQ_FLAGS=()
if [[ "${RUN_TSAN:-0}" == 1 ]]; then RXQ_FLAGS=(-fsanitize=thread -g); fi
g++ -std=c++17 -O1 -Wall -Wextra -Werror -pthread "${RXQ_FLAGS[@]}" -I../../main \
    test_sl2_rxq_thread.cpp -o "$BUILD_DIR/test_sl2_rxq_thread"
"$BUILD_DIR/test_sl2_rxq_thread"
