#pragma once

#include <cstring>
#include <cstddef>
#include <strings.h>
#include "json_utils.h"

// ════════════════════════════════════════════════════════════════════════════
// Web UI request guards. Pure functions so they can be unit-tested on the host
// (test/web_security); the httpd glue lives in web_server.cpp.
//
// There is no LAN authentication yet. These checks stop a web page open in a
// browser on the same network from driving the device (cross-site WebSocket
// hijack / cross-site POST), and keep the HomeKit setup code off the wire once
// someone has paired. They do not stop a direct client on the LAN.
// ════════════════════════════════════════════════════════════════════════════

// Parse "host[:port]" or "[v6][:port]", rejecting malformed authorities.
// A missing port is reported as zero; explicit ports must be in 1..65535.
inline bool webHostOnly(const char *authority, size_t len, char *out, size_t outLen,
                        unsigned *port = nullptr) {
    if (!authority || len == 0) return false;
    size_t n = len;
    if (authority[0] == '[') {
        const char *close = (const char *)memchr(authority, ']', len);
        if (!close) return false;
        n = (size_t)(close - authority) + 1;
        if (n < 4) return false;
        for (size_t i = 1; i + 1 < n; ++i) {
            char c = authority[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                  (c >= 'A' && c <= 'F') || c == ':' || c == '.')) return false;
        }
    } else {
        const char *colon = (const char *)memchr(authority, ':', len);
        if (colon) n = (size_t)(colon - authority);
        for (size_t i = 0; i < n; ++i) {
            char c = authority[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') || c == '-' || c == '.')) return false;
        }
    }
    unsigned parsedPort = 0;
    if (n < len) {
        if (authority[n] != ':' || n + 1 == len) return false;
        for (size_t i = n + 1; i < len; ++i) {
            if (authority[i] < '0' || authority[i] > '9') return false;
            parsedPort = parsedPort * 10 + (unsigned)(authority[i] - '0');
            if (parsedPort > 65535) return false;
        }
        if (parsedPort == 0) return false;
    }
    if (n == 0 || n + 1 > outLen) return false;
    memcpy(out, authority, n);
    out[n] = '\0';
    if (port) *port = parsedPort;
    return true;
}

// DNS rebinding guard for every HTTP request, including requests without
// Origin. HTTP serves port 80. Only the current STA IP, configured mDNS hostname
// (bare or .local), and the default recovery AP IP may address this server.
inline bool webHostAllowed(const char *host, const char *staIP, const char *mdnsHostname) {
    char name[96];
    unsigned port;
    if (!host || !webHostOnly(host, strlen(host), name, sizeof(name), &port) ||
        (port != 0 && port != 80)) return false;
    if (strcmp(name, "192.168.4.1") == 0) return true;
    if (staIP && staIP[0] && strcmp(staIP, "0.0.0.0") != 0 && strcmp(name, staIP) == 0)
        return true;
    if (!mdnsHostname || !mdnsHostname[0]) return false;
    if (strcasecmp(name, mdnsHostname) == 0) return true;
    size_t n = strlen(mdnsHostname);
    return strlen(name) == n + 6 && strncasecmp(name, mdnsHostname, n) == 0 &&
           strcasecmp(name + n, ".local") == 0;
}

// Additional same-host Origin rule, after the Host allowlist passes. No Origin
// header (curl, native apps, some same-origin requests) is allowed. Otherwise the
// Origin must be http(s)://<host>[:port] where <host> equals the host in the
// request's Host header, compared case-insensitively. "null" and any other
// scheme are rejected.
inline bool webOriginAllowed(const char *origin, const char *host) {
    if (!origin || origin[0] == '\0') return true;
    const char *auth;
    if (strncasecmp(origin, "http://", 7) == 0)       auth = origin + 7;
    else if (strncasecmp(origin, "https://", 8) == 0) auth = origin + 8;
    else return false;
    if (!host || host[0] == '\0') return false;
    size_t alen = strcspn(auth, "/");
    char oh[96], hh[96];
    if (!webHostOnly(auth, alen, oh, sizeof(oh))) return false;
    if (!webHostOnly(host, strlen(host), hh, sizeof(hh))) return false;
    return strcasecmp(oh, hh) == 0;
}

// The HomeKit setup code (and the pairing URI that embeds it) is only useful,
// and only shown, while nobody is paired.
inline const char *webHkSetupValue(bool hkReady, int controllers, const char *value,
                                   bool previouslyPaired = false) {
    return (hkReady && !previouslyPaired && controllers <= 0 && value) ? value : "";
}

// Largest POST /wifi-setup body accepted. This fits a 32-byte SSID and
// 64-byte password with every character \u-escaped, plus JSON syntax and
// ordinary whitespace. Reject larger requests without reading a prefix.
constexpr size_t WIFI_SETUP_MAX_BODY = 1023;

// Bytes to read for a request body of contentLen, or -1 to reject it (empty
// or larger than cap). Reading less than the declared length would hand a
// truncated JSON string to the caller, so oversize is refused, not clipped.
inline int webBodyLen(size_t contentLen, size_t cap) {
    if (contentLen == 0 || contentLen > cap) return -1;
    return (int)contentLen;
}

// True when the command frame carries "confirm":"<word>" exactly. Destructive
// commands (factoryReset, hkReset, wifiForget, forgetRemote, restart) require
// it so a stray or forged frame cannot trigger them on its own.
inline bool webConfirmed(const char *msg, const char *word) {
    char given[16] = {0};
    return jsonGetString(msg, "confirm", given, sizeof(given)) && strcmp(given, word) == 0;
}
