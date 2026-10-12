#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
out=$(mktemp -d "$PWD/build.XXXXXX")
trap 'rm -rf "$out"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -I../../main test_web_security.cpp -o "$out/t"
"$out/t"
