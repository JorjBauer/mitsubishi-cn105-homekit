#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
python3 test_wifi_credentials.py
python3 test_wifi_setup_body.py
