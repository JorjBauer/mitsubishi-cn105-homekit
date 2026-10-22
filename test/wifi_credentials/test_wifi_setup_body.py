#!/usr/bin/env python3
"""POST /wifi-setup body handling, compiled from the real sources.

applyWifiCredentials() is extracted from main/web_server.cpp and run against
stubs. webBodyLen() comes from main/web_security.h. A maximum-length password
full of quotes and backslashes must reach WifiManager::connect() intact, and an
unterminated or oversize body must be refused instead of joining with a
truncated password.
"""
from pathlib import Path
import json
import os
import shlex
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "main/web_server.cpp").read_text()
m = re.search(r"^bool WebUI::applyWifiCredentials\([^;]*?\)\s*\{.*?^\}", SOURCE,
              re.MULTILINE | re.DOTALL)
assert m, "cannot find WebUI::applyWifiCredentials"

PREFIX = r'''
#include "json_utils.h"
#include "web_security.h"
#include "cJSON.h"
#include <memory>
#include <cassert>
#include <string>
#include <cstdio>
#define LOG_INFO(...) ((void)0)
#define EV_WIFI_CREDS_CHANGED 1
static void eventlog_append(int) {}
static std::string got_ssid, got_pass;
static int connects;
struct WifiManager { static bool connect(const char *s, const char *p)
    { got_ssid = s; got_pass = p; ++connects; return true; } };
static struct { void setChangePending(bool) {} void noteReprovision() {} } wifiRecovery;
struct WebUI { bool applyWifiCredentials(const char *json, const char **outError); };
'''

CHECKS = r'''
static bool tryApply(const std::string &body, const char **err) {
    WebUI ui; *err = nullptr; return ui.applyWifiCredentials(body.c_str(), err);
}
int main() {
    const char *err;
    // 63-character password made of quotes and backslashes round-trips.
    std::string pass;
    for (int i = 0; i < 63; i++) pass += (i % 3 == 0) ? '"' : (i % 3 == 1) ? '\\' : 'a';
    std::string json = std::string("{\"ssid\":\"Home \\\"2.4\\\"\",\"password\":") + PASS_JSON + "}";
    assert(json.size() <= WIFI_SETUP_MAX_BODY);
    assert(tryApply(json, &err));
    assert(got_ssid == "Home \"2.4\"");
    assert(got_pass == pass);

    // Unterminated password: refused, connect() never called.
    connects = 0;
    assert(!tryApply("{\"ssid\":\"net\",\"password\":\"abc", &err));
    assert(err && connects == 0);
    // A body clipped mid-escape is refused too.
    assert(!tryApply("{\"ssid\":\"net\",\"password\":\"abc\\", &err) && connects == 0);
    // Open network (no password key) and empty password still work.
    assert(tryApply("{\"ssid\":\"open\"}", &err) && got_pass.empty());
    assert(tryApply("{\"ssid\":\"open\",\"password\":\"\"}", &err) && got_pass.empty());

    // Body length policy.
    assert(webBodyLen(0, WIFI_SETUP_MAX_BODY) == -1);
    assert(webBodyLen(1, WIFI_SETUP_MAX_BODY) == 1);
    assert(webBodyLen(WIFI_SETUP_MAX_BODY, WIFI_SETUP_MAX_BODY) == (int)WIFI_SETUP_MAX_BODY);
    assert(webBodyLen(WIFI_SETUP_MAX_BODY + 1, WIFI_SETUP_MAX_BODY) == -1);
    assert(webBodyLen(100000, WIFI_SETUP_MAX_BODY) == -1);
    puts("wifi-setup body: round trip, unterminated and oversize checks passed");
}
'''
# The 63-char password above, as the browser's JSON.stringify would send it.
pw = "".join('"' if i % 3 == 0 else '\\' if i % 3 == 1 else 'a' for i in range(63))
body = CHECKS.replace("PASS_JSON", 'R"J(' + json.dumps(pw) + ')J"')

here = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="build.", dir=here) as tmp:
    unit = Path(tmp) / "t.cpp"
    unit.write_text(PREFIX + "\n" + m[0] + "\n" + body)
    sdk = Path(os.environ.get("IDF_PATH", Path.home() / "esp/esp-idf")) / "components/json/cJSON"
    if (sdk / "cJSON.c").is_file():
        obj = Path(tmp) / "cJSON.o"
        subprocess.run(["cc", "-c", str(sdk / "cJSON.c"), "-o", str(obj)], check=True)
        dependency = ["-I" + str(sdk), str(obj)]
    else:
        result = subprocess.run(["pkg-config", "--cflags", "--libs", "libcjson"],
                                capture_output=True, text=True)
        if result.returncode:
            raise SystemExit("Install libcjson-dev and pkg-config, or set IDF_PATH to ESP-IDF")
        dependency = shlex.split(result.stdout)
    exe = Path(tmp) / "t"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-g",
                    "-I" + str(ROOT / "main"), str(unit), *dependency, "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
