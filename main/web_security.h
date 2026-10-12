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

// Host part of "host", "host:port", "[v6]" or "[v6]:port" (no scheme, no path).
// Copies into out; returns false if it is empty or does not fit.
inline bool webHostOnly(const char *authority, size_t len, char *out, size_t outLen) {
    if (len == 0) return false;
    size_t n = len;
    if (authority[0] == '[') {
        const char *close = (const char *)memchr(authority, ']', len);
        if (!close) return false;
        n = (size_t)(close - authority) + 1;
    } else {
        const char *colon = (const char *)memchr(authority, ':', len);
        if (colon) n = (size_t)(colon - authority);
    }
    if (n == 0 || n + 1 > outLen) return false;
    memcpy(out, authority, n);
    out[n] = '\0';
    return true;
}

// True when a request may change device state. No Origin header (curl, native
// apps, same-origin GET upgrades from some clients) is allowed. Otherwise the
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
inline const char *webHkSetupValue(bool hkReady, int controllers, const char *value) {
    return (hkReady && controllers <= 0 && value) ? value : "";
}

// Largest POST /wifi-setup body accepted. The worst legitimate payload is a
// 32-byte SSID plus a 63-byte password with every character escaped as two
// bytes (JSON.stringify only escapes quotes, backslashes and control
// characters), about 215 bytes with the keys; 320 leaves headroom.
constexpr size_t WIFI_SETUP_MAX_BODY = 320;

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
