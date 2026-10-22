#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 test/web_credentials/test_web_credentials.py
