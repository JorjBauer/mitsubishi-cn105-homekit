#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -I../../main test_web_security.cpp -o "$out/t"
"$out/t"
python3 test_http_guards.py
