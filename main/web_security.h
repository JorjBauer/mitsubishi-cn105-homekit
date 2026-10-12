#pragma once

#include <cstddef>

// Web UI request guards: pure functions, unit-tested on the host.

// Largest POST /wifi-setup body accepted. The worst legitimate payload is a
// 32-byte SSID plus a 63-byte password with every character \u-escaped.
constexpr size_t WIFI_SETUP_MAX_BODY = 320;

// Bytes to read for a request body of contentLen, or -1 to reject it (empty
// or larger than cap). Reading less than the declared length would hand a
// truncated JSON string to the caller, so oversize is refused, not clipped.
inline int webBodyLen(size_t contentLen, size_t cap) {
    if (contentLen == 0 || contentLen > cap) return -1;
    return (int)contentLen;
}
