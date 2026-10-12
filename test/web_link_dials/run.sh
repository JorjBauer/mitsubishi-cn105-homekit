#!/usr/bin/env bash
# The Link card's per-dial list: one row and one Forget per bonded dial once
# two or more are paired, each sending forgetDial for its own MAC only, and
# the field and command names the firmware (main/web_ws.cpp) actually uses.
set -euo pipefail
cd "$(dirname "$0")"
node ./test.js
