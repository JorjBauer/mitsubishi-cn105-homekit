#!/usr/bin/env python3
"""Run production HTTP credential handlers against fragmented bodies and real cJSON."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "main/web_server.cpp").read_text()


def method(name):
    match = re.search(r"^(?:bool|esp_err_t) WebUI::" + name + r"\([^;]*?\) \{.*?^\}",
                      SOURCE, re.MULTILINE | re.DOTALL)
    assert match, f"missing WebUI::{name}"
    return match[0]


PREFIX = r'''
#include "json_utils.h"
#include "web_security.h"
#include "cJSON.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, HTTPD_SOCK_ERR_TIMEOUT = -3;
constexpr int HTTPD_400_BAD_REQUEST = 400, HTTPD_408_REQ_TIMEOUT = 408;
constexpr int HTTPD_413_CONTENT_TOO_LARGE = 413, EV_WIFI_CREDS_CHANGED = 1;
struct httpd_req_t {
    size_t content_len;
    std::string body;
    std::vector<int> chunks;
    size_t offset = 0, reads = 0;
    int status = 200;
    uint32_t rx_delay = 0;
    std::string response;
};
static uint32_t now_ms;
static uint32_t uptime_ms() { return now_ms; }
static int joins, events, pending, reprovisions;
static bool join_ok, origin_ok = true;
static std::string joined_ssid, joined_password;
namespace WifiManager {
static bool connect(const char *ssid, const char *password) {
    ++joins; joined_ssid = ssid; joined_password = password; return join_ok;
}
}
struct Recovery {
    void setChangePending(bool value) { if (value) ++pending; }
    void noteReprovision() { ++reprovisions; }
} wifiRecovery;
static void eventlog_append(int) { ++events; }
static void silent_log(const char *, ...) {}
#define LOG_INFO(...) silent_log(__VA_ARGS__)
static int httpd_req_recv(httpd_req_t *req, char *out, size_t capacity) {
    now_ms += req->rx_delay;
    const int requested = req->reads < req->chunks.size()
        ? req->chunks[req->reads] : static_cast<int>(capacity);
    ++req->reads;
    if (requested <= 0) return requested;
    const size_t count = std::min({capacity, static_cast<size_t>(requested),
                                   req->body.size() - req->offset});
    memcpy(out, req->body.data() + req->offset, count);
    req->offset += count;
    return static_cast<int>(count);
}
static int httpd_resp_send_err(httpd_req_t *req, int status, const char *message) {
    req->status = status; req->response = message; return ESP_OK;
}
static void httpd_resp_set_type(httpd_req_t *, const char *) {}
static int httpd_resp_sendstr(httpd_req_t *req, const char *body) {
    req->response = body; return ESP_OK;
}
class WebUI {
public:
    bool applyWifiCredentials(const char *, const char **);
    static esp_err_t handleWifiSetup(httpd_req_t *);
    static bool requireSameOrigin(httpd_req_t *req) {
        if (!origin_ok) req->status = 403;
        return origin_ok;
    }
} webUI;
'''

CHECKS = r'''
static int failures;
static void check(bool good, const char *message) {
    if (!good) { fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
static httpd_req_t request(const std::string &body, std::vector<int> chunks = {}) {
    joins = events = pending = reprovisions = 0;
    join_ok = origin_ok = true;
    joined_ssid.clear(); joined_password.clear();
    return {body.size(), body, std::move(chunks), 0, 0, 200, 0, ""};
}
static void rejects(const std::string &body) {
    auto req = request(body);
    check(WebUI::handleWifiSetup(&req) != ESP_OK && req.status >= 400,
          "malformed credentials must receive an error");
    check(joins == 0 && pending == 0 && reprovisions == 0,
          "malformed credentials must not change WiFi state");
}
int main() {
    const std::string body = R"({"ssid":"Home","password":"correct-password"})";
    auto req = request(body, {30});
    check(WebUI::handleWifiSetup(&req) == ESP_OK && req.status == 200,
          "fragmented request succeeds after consuming the complete body");
    check(joins == 1 && joined_ssid == "Home" && joined_password == "correct-password",
          "fragmented request must not apply a partial password");
    check(req.offset == body.size(), "successful HTTP request consumes all body bytes");

    req = request(body, std::vector<int>(body.size(), 1));
    check(WebUI::handleWifiSetup(&req) == ESP_OK && joined_password == "correct-password",
          "one-byte TCP fragments retain the complete credentials");

    const std::string longBody = "{\"ssid\":\"" + std::string(32, 'S') +
        "\",\"password\":\"" + std::string(126, '\\') + "\"}";
    req = request(longBody);
    check(WebUI::handleWifiSetup(&req) == ESP_OK && joined_ssid == std::string(32, 'S') &&
          joined_password == std::string(63, '\\'),
          "valid escaped credentials longer than 149 JSON bytes round-trip intact");

    std::string escapedMax = "{\"ssid\":\"";
    for (int i = 0; i < 32; ++i) escapedMax += "\\u0053";
    escapedMax += "\",\"password\":\"";
    for (int i = 0; i < 64; ++i) escapedMax += "\\u0050";
    escapedMax += "\"}";
    req = request(escapedMax);
    check(WebUI::handleWifiSetup(&req) == ESP_OK && joined_ssid == std::string(32, 'S') &&
          joined_password == std::string(64, 'P'),
          "maximum Unicode-escaped credentials fit the shared request cap");

    for (const std::string &valid : {
            std::string(" { \"ssid\" : \"Home\", \"password\" : \"correct-password\" } \n"),
            std::string(R"({"cmd":"wifi","ssid":"Home","password":"correct-password"})")}) {
        req = request(valid);
        check(WebUI::handleWifiSetup(&req) == ESP_OK && joined_password == "correct-password",
              "JSON whitespace and the shared WebSocket command shape are accepted");
    }
    for (const char *valid : {R"({"ssid":"Open"})", R"({"ssid":"Open","password":""})"}) {
        req = request(valid);
        check(WebUI::handleWifiSetup(&req) == ESP_OK && joined_password.empty(),
              "missing or empty password selects an open network");
    }
    req = request(R"({"ssid":"Home","password":"literal\\u0000"})");
    check(WebUI::handleWifiSetup(&req) == ESP_OK && joined_password == "literal\\u0000",
          "literal backslash-u0000 is not a NUL escape");

    for (const char *bad : {
            R"({"ssid":"Home","password":"unfinished})",
            R"({"ssid":"Home","password":false})",
            R"({"ssid":"Home","password":null})",
            R"({"ssid":"Home","password":{}})",
            R"({"ssid":"Home","password":"pass"})trailing",
            R"({"ssid":"Home","password":"pass")",
            R"({"ssid":"Home","password":"pass",})",
            R"({"ssid":"Home","password":"a\u0000b"})",
            R"({"ssid":"Home\u0000Other","password":"pass"})",
            R"({"ssid":42})", R"({"ssid":""})", R"({"password":"pass"})", "[]"})
        rejects(bad);
    rejects("{\"ssid\":\"" + std::string(33, 'S') + "\"}");
    rejects("{\"ssid\":\"Home\",\"password\":\"" + std::string(65, 'P') + "\"}");
    rejects(body + std::string(1, '\0') + "ignored");
    rejects("");
    rejects(std::string(10000, ' '));

    for (int error : {0, HTTPD_SOCK_ERR_TIMEOUT, -2}) {
        req = request(body, {30, error});
        check(WebUI::handleWifiSetup(&req) != ESP_OK && joins == 0,
              "disconnect and timeout must not submit partial credentials");
    }
    req = request(body, std::vector<int>(body.size(), 1));
    req.rx_delay = 1000;
    check(WebUI::handleWifiSetup(&req) != ESP_OK && req.status == 408 && joins == 0,
          "slow trickle has a total request deadline");

    req = request(body);
    join_ok = false;
    check(WebUI::handleWifiSetup(&req) != ESP_OK && req.status >= 400,
          "driver admission failure must not report saved");
    check(joins == 1 && pending == 0 && reprovisions == 0 && events == 0,
          "failed join admission must not start recovery or log a credential change");
    req = request(body);
    origin_ok = false;
    check(WebUI::handleWifiSetup(&req) != ESP_OK && req.status == 403 &&
          req.reads == 0 && joins == 0,
          "origin rejection must precede body reception and WiFi changes");
    if (failures) return 1;
    puts("PASS: HTTP credentials fragmentation, bounds, validation, deadlines and join failures");
}
'''

with tempfile.TemporaryDirectory(prefix="web-credentials-") as tmp:
    tmp = Path(tmp)
    unit = tmp / "test.cpp"
    unit.write_text(PREFIX + method("applyWifiCredentials") + "\n" +
                    method("handleWifiSetup") + CHECKS)
    sdk = Path(os.environ.get("IDF_PATH", Path.home() / "esp/esp-idf")) / "components/json/cJSON"
    if (sdk / "cJSON.c").is_file():
        obj = tmp / "cJSON.o"
        subprocess.run(["cc", "-c", str(sdk / "cJSON.c"), "-o", str(obj)], check=True)
        dependency = ["-I" + str(sdk), str(obj)]
    else:
        result = subprocess.run(["pkg-config", "--cflags", "--libs", "libcjson"],
                                capture_output=True, text=True)
        if result.returncode:
            raise SystemExit("Install libcjson-dev and pkg-config, or set IDF_PATH to ESP-IDF")
        dependency = shlex.split(result.stdout)
    exe = tmp / "test"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-function", "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer", "-g", "-I" + str(ROOT / "main"),
                    str(unit), *dependency, "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
