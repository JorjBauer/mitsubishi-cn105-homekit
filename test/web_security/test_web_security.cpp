#include "web_security.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    int checks = 0;
#define OK(o, h)  do { assert(webOriginAllowed((o), (h)));  checks++; } while (0)
#define BAD(o, h) do { assert(!webOriginAllowed((o), (h))); checks++; } while (0)

    // Missing Origin: non-browser clients and same-origin requests that omit it.
    OK(nullptr, "192.168.1.50");
    OK("", "192.168.1.50");
    // Same host, with and without ports, scheme and case differences.
    OK("http://192.168.1.50", "192.168.1.50");
    OK("http://192.168.1.50", "192.168.1.50:80");
    OK("http://192.168.1.50:80", "192.168.1.50");
    OK("https://serin-339020.local", "SERIN-339020.local");
    OK("http://[fe80::1]:80", "[fe80::1]");
    // Other host: the cross-site case this check exists for.
    BAD("http://evil.example", "192.168.1.50");
    BAD("http://192.168.1.51", "192.168.1.50");
    BAD("http://192.168.1.50.evil.example", "192.168.1.50");
    BAD("http://evil.example/192.168.1.50", "192.168.1.50");
    BAD("http://evil.example:80", "192.168.1.50:80");
    // Opaque / odd origins and a missing Host header.
    BAD("null", "192.168.1.50");
    BAD("file://192.168.1.50", "192.168.1.50");
    BAD("http://", "192.168.1.50");
    BAD("http://192.168.1.50", nullptr);
    BAD("http://192.168.1.50", "");

    // DNS rebinding: only current STA IP, actual mDNS name, and recovery IP.
#define HOST_OK(h) do { assert(webHostAllowed((h), "192.168.1.50", "Serin-AB12")); checks++; } while (0)
#define HOST_BAD(h) do { assert(!webHostAllowed((h), "192.168.1.50", "Serin-AB12")); checks++; } while (0)
    for (const char *host : {"192.168.1.50", "192.168.1.50:80", "192.168.1.50:00080",
                             "serin-ab12", "SERIN-AB12:80", "Serin-AB12.local",
                             "SERIN-AB12.LOCAL:80", "192.168.4.1", "192.168.4.1:80"}) HOST_OK(host);
    for (const char *host : {"", "evil.example", "evil.example:80", "192.168.1.51",
                             "0.0.0.0", "192.168.1.50.evil.example", "Serin-AB12.local.evil.example",
                             "Serin-AB12.local.", "Serin-AB12.local.local", "Display Label",
                             "192.168.1.50:8080", "192.168.1.50:443", "192.168.1.50:0",
                             "192.168.1.50:", "192.168.1.50:65536", "192.168.1.50:99999999999",
                             "192.168.1.50:+80", "192.168.1.50:-80", "192.168.1.50:80x",
                             "192.168.1.50:80:80", "192.168.1.50:80/", "192.168.1.50/",
                             "192.168.1.50?x", "192.168.1.50#x", "192.168.1.50@evil.example",
                             "evil.example@192.168.1.50", "http://192.168.1.50",
                             " 192.168.1.50", "192.168.1.50 ", "192.168.1.50\r\n",
                             "[192.168.1.50]", "[fe80::1]", "[fe80::1]:80"}) HOST_BAD(host);
    HOST_BAD(nullptr);
    assert(!webHostAllowed("192.168.1.50", "192.168.1.77", "Serin-AB12")); checks++;
    assert(!webHostAllowed("0.0.0.0", "0.0.0.0", nullptr)); checks++;
    assert(!webHostAllowed("Serin-AB12", "", "")); checks++;
    assert(webHostAllowed("192.168.4.1", nullptr, nullptr)); checks++;
    assert(!webHostAllowed("Display-Label.local", "", "Serin-AB12")); checks++;
    assert(webHostAllowed("Other-Name.local", "", "Other-Name")); checks++;
    BAD("http://192.168.1.50:garbage", "192.168.1.50");
    BAD("http://[fe80::1]garbage", "[fe80::1]");
    BAD("http://192.168.1.50", "192.168.1.50:80x");

    // Setup code only while unpaired.
    assert(strcmp(webHkSetupValue(true, 0, "123-45-678"), "123-45-678") == 0); checks++;
    assert(strcmp(webHkSetupValue(true, 1, "123-45-678"), "") == 0);           checks++;
    assert(strcmp(webHkSetupValue(true, 3, "X-HM://abc"), "") == 0);           checks++;
    assert(strcmp(webHkSetupValue(false, 0, "123-45-678"), "") == 0);          checks++;
    assert(strcmp(webHkSetupValue(true, 0, nullptr), "") == 0);                checks++;

    // Destructive commands need their exact typed word.
    assert(webConfirmed("{\"cmd\":\"restart\",\"confirm\":\"RESTART\"}", "RESTART")); checks++;
    assert(!webConfirmed("{\"cmd\":\"restart\"}", "RESTART"));                        checks++;
    assert(!webConfirmed("{\"cmd\":\"restart\",\"confirm\":\"restart\"}", "RESTART")); checks++;
    assert(!webConfirmed("{\"cmd\":\"hkReset\",\"confirm\":\"RESTART\"}", "RESET"));   checks++;
    assert(!webConfirmed("{\"cmd\":\"wifiForget\",\"confirm\":\"FORGET", "FORGET"));  checks++;

    printf("web_security: all %d checks passed\n", checks);
    return 0;
}
