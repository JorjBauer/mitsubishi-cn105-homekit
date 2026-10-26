#!/usr/bin/env python3
"""Run the real HTTP guards and GET handlers with simulated HTTPD/network state."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "main/web_server.cpp").read_text()
WS = (ROOT / "main/web_ws.cpp").read_text()


def function(name, source=SOURCE):
    match = re.search(r"^(?:static )?(?:bool|void|esp_err_t) " + re.escape(name)
                      + r"\([^;]*?\)\s*\{.*?^\}", source, re.MULTILINE | re.DOTALL)
    assert match, f"cannot find {name}"
    return match[0]


PREFIX = r'''
#include "web_security.h"
#include "wifi_manager.h"
#include <string>
#include <cstdio>
#include <cstdlib>
using esp_err_t = int;
using httpd_err_code_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1;
constexpr int HTTPD_403_FORBIDDEN = 403, HTTPD_404_NOT_FOUND = 404;
struct httpd_req_t {
    const char *uri = "/";
    std::string host, origin;
    int status = 200, sends = 0;
    bool headerFailure = false, cached = false;
};
static size_t httpd_req_get_hdr_value_len(httpd_req_t *req, const char *name) {
    return strcmp(name, "Host") == 0 ? req->host.size() : req->origin.size();
}
static int httpd_req_get_hdr_value_str(httpd_req_t *req, const char *name, char *out, size_t len) {
    const auto &value = strcmp(name, "Host") == 0 ? req->host : req->origin;
    if (req->headerFailure || value.size() >= len) return ESP_FAIL;
    memcpy(out, value.c_str(), value.size() + 1);
    return ESP_OK;
}
static void httpd_resp_send_err(httpd_req_t *req, int status, const char *) { req->status = status; ++req->sends; }
static void httpd_resp_set_type(httpd_req_t *, const char *) {}
static void httpd_resp_set_hdr(httpd_req_t *, const char *, const char *) {}
static void httpd_resp_sendstr(httpd_req_t *req, const char *) { ++req->sends; }
static void httpd_resp_send(httpd_req_t *req, const char *, size_t) { ++req->sends; }
static bool sendNotModified(httpd_req_t *req) {
    if (!req->cached) return false;
    req->status = 304; ++req->sends; return true;
}
static const char *assetEtag() { return "etag"; }
static bool apClient;
static bool isApSubnetClient(httpd_req_t *) { return apClient; }
static const char CNA_PAGE[] = "recovery", MANIFEST_JSON[] = "manifest";
static const uint8_t web_ui_html_gz_start[] = {0}, web_ui_html_gz_end[] = {0};
static const uint8_t recovery_html_gz_start[] = {0}, recovery_html_gz_end[] = {0};
static const uint8_t icon_192_png_start[] = {0}, icon_192_png_end[] = {0};
static const uint8_t icon_512_png_start[] = {0}, icon_512_png_end[] = {0};
static const uint8_t favicon_png_start[] = {0}, favicon_png_end[] = {0};
static std::string currentIP = "192.168.1.50", currentHostname = "Serin-AB12";
static bool connected = true;
static int scans, credentialReads;
void WifiManager::getIP(char *out, size_t len) { snprintf(out, len, "%s", currentIP.c_str()); }
bool WifiManager::isConnected() { return connected; }
const char *WifiManager::getHostname() { return currentHostname.c_str(); }
bool WifiManager::loadCredentials(char *ssid, size_t, char *pass, size_t) {
    ++credentialReads; ssid[0] = pass[0] = '\0'; return false;
}
bool WifiManager::isJoinPending() { return false; }
WifiManager::WifiTrialState WifiManager::getTrialState() { return WifiManager::WIFI_TRIAL_IDLE; }
int WifiManager::scanNetworks(ScannedNetwork *, int) { ++scans; return 0; }
struct DeviceSettings { const char *deviceName = "Display Label"; bool homekitPaired = false; char trustedHost[64] = ""; };
static struct { DeviceSettings &get() { static DeviceSettings cfg; return cfg; } } settings;
static int homekit_get_controller_count() { return 0; }
static const char *homekit_get_setup_code() { return "123-45-678"; }
using ErrorHandler = esp_err_t (*)(httpd_req_t *, httpd_err_code_t);
static ErrorHandler registered404;
static void httpd_register_err_handler(int, int, ErrorHandler handler) { registered404 = handler; }
static void silent_log(const char *, ...) {}
#define LOG_WARN(...) silent_log(__VA_ARGS__)
#define LOG_INFO(...) silent_log(__VA_ARGS__)
class WebUI {
public:
    bool _apMode = false;
    int _server = 1;
    void applyCaptivePortalHandler();
    bool isAPMode() const { return _apMode; }
    static bool requireSameOrigin(httpd_req_t *);
    static esp_err_t handleRoot(httpd_req_t *);
    static esp_err_t handleRecoveryPage(httpd_req_t *);
    static esp_err_t handleWifiStatus(httpd_req_t *);
    static esp_err_t handleWifiScan(httpd_req_t *);
    static esp_err_t handleManifest(httpd_req_t *);
    static esp_err_t handleIcon192(httpd_req_t *);
    static esp_err_t handleIcon512(httpd_req_t *);
    static esp_err_t handleFavicon(httpd_req_t *);
    static esp_err_t handleRedirect404(httpd_req_t *, httpd_err_code_t);
    static esp_err_t sendGzipPage(httpd_req_t *, const uint8_t *, size_t);
    static esp_err_t wsPreHandshake(httpd_req_t *);
};
static WebUI webUI;
'''

CHECKS = r'''
static int checks;
static void check(bool ok, const char *label) {
    ++checks;
    if (!ok) { fprintf(stderr, "FAIL: %s\n", label); exit(1); }
}
static void expectGuard(const char *host, const char *origin, bool allowed) {
    httpd_req_t req; req.host = host; req.origin = origin;
    check(WebUI::requireSameOrigin(&req) == allowed, host);
    check(req.status == (allowed ? 200 : 403), "guard HTTP status");
    check(req.sends == (allowed ? 0 : 1), "guard response count");
}
int main() {
    // Actual bug: a foreign Host with no Origin reaches the device.
    expectGuard("evil.example", "", false);
    for (const char *host : {"192.168.1.50", "192.168.1.50:80", "Serin-AB12",
                             "serin-ab12.local", "SERIN-AB12.LOCAL:80", "192.168.4.1"})
        expectGuard(host, "", true);
    for (const char *host : {"", "192.168.1.51", "Display Label", "evil.example:80",
                             "serin-ab12.local.evil.example", "192.168.4.1:8080",
                             "192.168.1.50:garbage", "192.168.1.50/", "192.168.1.50:80:80"})
        expectGuard(host, "", false);
    expectGuard("Serin-AB12.local", "http://SERIN-AB12.local:80", true);
    expectGuard("192.168.1.50", "http://evil.example", false);
    expectGuard("evil.example", "http://evil.example", false);
    expectGuard("192.168.1.50", "null", false);
    expectGuard("192.168.4.1", "http://192.168.4.1", true);
    // The trusted hostname: that exact name, any case, port 80 only.
    expectGuard("minisplit.example.com", "", false);
    strcpy(settings.get().trustedHost, "minisplit.example.com");
    for (const char *host : {"minisplit.example.com", "MiniSplit.Example.Com:80"})
        expectGuard(host, "", true);
    for (const char *host : {"minisplit.example.com:8080", "minisplit.example.com.evil.example",
                             "evil.minisplit.example.com", "minisplit.example"})
        expectGuard(host, "", false);
    expectGuard("minisplit.example.com", "http://minisplit.example.com", true);
    expectGuard("minisplit.example.com", "http://evil.example", false);
    expectGuard("serin-ab12.local", "", true);   // the built-in names still work
    settings.get().trustedHost[0] = '\0';
    expectGuard("minisplit.example.com", "", false);
    currentIP = "192.168.1.77";
    expectGuard("192.168.1.50", "", false);
    expectGuard("192.168.1.77", "", true);
    connected = false;
    expectGuard("192.168.1.77", "", false);
    currentIP = "0.0.0.0";
    connected = true;
    expectGuard("0.0.0.0", "", false);
    // Recovery before HAP/mDNS startup still allows its configured name.
    connected = false;
    expectGuard("Serin-AB12", "", true);
    expectGuard("Serin-AB12.local", "", true);
    expectGuard("Display Label", "", false);
    expectGuard("192.168.4.1", "", true);
    currentHostname = "Serin-CD34";
    expectGuard("Serin-AB12.local", "", false);
    expectGuard("serin-cd34.local", "", true);
    httpd_req_t unreadable; unreadable.host = "192.168.4.1"; unreadable.headerFailure = true;
    check(!WebUI::requireSameOrigin(&unreadable) && unreadable.status == 403, "unreadable Host");
    expectGuard(std::string(200, 'a').c_str(), "", false);
    expectGuard("192.168.4.1", std::string(200, 'a').c_str(), false);
    using Handler = esp_err_t (*)(httpd_req_t *);
    Handler handlers[] = {WebUI::handleRoot, WebUI::handleRecoveryPage, WebUI::handleWifiStatus,
        WebUI::handleWifiScan, WebUI::handleManifest, WebUI::handleIcon192,
        WebUI::handleIcon512, WebUI::handleFavicon, WebUI::wsPreHandshake};
    for (bool ap : {false, true}) {
        webUI._apMode = ap; apClient = ap;
        for (Handler handler : handlers) {
            httpd_req_t foreign; foreign.host = "evil.example"; foreign.cached = true;
            scans = credentialReads = 0;
            if (ap && handler == WebUI::handleRoot) {
                // "/" captive probes (Ubuntu) from AP clients get the portal page.
                check(handler(&foreign) == ESP_OK && foreign.status == 200 && foreign.sends == 1,
                      "foreign-Host root probe gets portal page on AP");
                apClient = false;
                httpd_req_t lan; lan.host = "evil.example";
                check(handler(&lan) == ESP_FAIL && lan.status == 403,
                      "foreign Host from a non-AP client rejected by root");
                apClient = ap;
            } else
            check(handler(&foreign) == ESP_FAIL && foreign.status == 403 && foreign.sends == 1,
                  "foreign Host rejected by real handler");
            check(scans == 0 && credentialReads == 0, "refusal precedes Wi-Fi access");
            httpd_req_t own; own.host = "192.168.4.1";
            check(handler(&own) == ESP_OK && own.status == 200, "recovery Host reaches real handler");
            if (handler != WebUI::handleWifiStatus && handler != WebUI::handleWifiScan &&
                handler != WebUI::wsPreHandshake) {
                httpd_req_t cached; cached.host = "192.168.4.1"; cached.cached = true;
                check(handler(&cached) == ESP_OK && cached.status == 304,
                      "allowed Host retains cached asset response");
            }
        }
        registered404 = nullptr;
        webUI.applyCaptivePortalHandler();
        check(registered404 != nullptr, "404 guard installed in STA and AP modes");
        // OS captive probes use a foreign Host; AP clients must still get the
        // static portal page or the phone's sign-in sheet shows a 403.
        httpd_req_t foreign; foreign.host = "connectivitycheck.gstatic.com";
        if (ap)
            check(registered404(&foreign, 404) == ESP_OK && foreign.status == 200 &&
                  foreign.sends == 1, "foreign captive-probe Host gets portal page on AP");
        else
            check(registered404(&foreign, 404) == ESP_FAIL && foreign.status == 403,
                  "foreign Host rejected by STA 404");
        apClient = false;
        httpd_req_t lan; lan.host = "evil.example";
        check(registered404(&lan, 404) == ESP_FAIL && lan.status == 403,
              "foreign Host from a non-AP client rejected by 404");
        apClient = ap;
        httpd_req_t own; own.host = "192.168.4.1";
        check(registered404(&own, 404) == (ap ? ESP_OK : ESP_FAIL) && own.status == (ap ? 200 : 404),
              "own recovery Host retains 404 routing");
    }
    printf("web_security HTTP glue: all %d checks passed\n", checks);
}
'''

redirect = "WebUI::handleRedirect404" if "esp_err_t WebUI::handleRedirect404(" in SOURCE else "handleRedirect404"
methods = ["WebUI::requireSameOrigin", "WebUI::sendGzipPage", "requestHostAllowed",
           "serveCNAPage", "WebUI::handleRoot", "WebUI::handleRecoveryPage", redirect, "WebUI::handleWifiStatus",
           "WebUI::handleWifiScan", "WebUI::handleManifest", "WebUI::handleIcon192",
           "WebUI::handleIcon512", "WebUI::handleFavicon", "WebUI::applyCaptivePortalHandler"]
with tempfile.TemporaryDirectory() as build:
    source = Path(build) / "http_guards.cpp"
    source.write_text(PREFIX + "\n" + "\n".join(function(name) for name in methods)
                      + "\n" + function("WebUI::wsPreHandshake", WS) + "\n"
                      + CHECKS.replace("REDIRECT_HANDLER", redirect))
    binary = Path(build) / "http_guards"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "main"),
                    str(source), "-o", str(binary)], check=True)
    raise SystemExit(subprocess.run([str(binary)]).returncode)
