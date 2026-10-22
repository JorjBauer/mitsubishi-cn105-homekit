#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
node test/web_connection/test.js
