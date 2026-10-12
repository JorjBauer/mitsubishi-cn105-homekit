#pragma once
#include <cstdint>
#include <cstddef>

// Serin Link protocol v2 (see main/sl2_proto.h + the libserinlink core in
// main/sl2_link.c). This header keeps the v1 EspnowLink surface so main.cpp,
// wifi_recovery.cpp and web_ws.cpp compile unchanged; the implementation is a
// thin adapter wiring the sl2 core to CN105Controller / WifiManager / HomeKit.

#ifndef ESPNOW_REMOTE_ENABLE
#define ESPNOW_REMOTE_ENABLE 1
#endif

class CN105Controller;

// Terminal pairing-window outcome, classified once in the adapter where the
// core's result string originates. NONE covers the non-terminal states and the
// deliberately-unindicated results (idle/listening/confirming/cancelled).
enum EspnowPairOutcome : uint8_t {
    ESPNOW_PAIR_NONE = 0,
    ESPNOW_PAIR_OK,        // "paired"
    ESPNOW_PAIR_FAIL,      // "timeout" / "full" / "pin-mismatch" / "storage-error"
};

// Read-only detail for the primary bonded dial, for the web UI Link card.
struct EspnowDialDetail {
    int32_t lastSeenSec;   // seconds since last heard; -1 if unknown
    int8_t  rssi;          // dBm; 0 if unknown
    bool    haveInfo;      // DIAL_INFO received (model/fw valid)
    char    model[24];
    char    fw[16];
    bool    syncing;       // dial's caps_seq != unit's
    uint8_t certState;     // sl2_cert_state_t: 0 NONE/1 PRESENT/2 INVALID/3 OK
};

// One bonded dial, for the Link card's per-dial list.
struct EspnowDialRow {
    uint8_t mac[6];
    bool    live;          // probed within the liveness window
    int32_t lastSeenSec;   // seconds since last heard; -1 if never
};

class EspnowLink {
public:
    void begin(CN105Controller *ctrl);   // call AFTER WiFi started + esp_now usable
    void loop();                         // call from main loop (~every 10ms ok)
    bool isStarted() const;              // begin() brought the link up
    bool isBonded() const;               // >=1 dial in the bond table
    bool isPeerLive() const;             // any bonded dial probed recently
    void getPeerMac(uint8_t out[6]) const;   // first bonded dial (00.. if none)
    // Bond-table access for callers (e.g. the web UI) that need more than the
    // first dial: how many are bonded, and each one's MAC by slot.
    int  bondedDialCount() const;
    bool bondedDialMac(int slot, uint8_t mac[6]) const;   // false if slot is out of range/empty
    bool bondedDial(int slot, EspnowDialRow &out) const;  // same range rule
    // Forget one bonded dial (bond, radio peer and its room reading) without
    // the restart forget-all does; the other dials stay paired. False if the
    // MAC is not bonded. Any task: loop() runs it on the main task.
    bool forgetDial(const uint8_t mac[6]);
    bool getDialDetail(EspnowDialDetail &out) const;  // false if not bonded
    // Health of the room-temperature source currently selected in settings
    // (enum sl2_room_status). The same check the INFO TLV publishes to the
    // dial, so the web UI and the dial can never show conflicting freshness.
    uint8_t roomSourceStatus() const;
    void startPairing();                 // 60 s signed-TOFU pairing window
    void cancelPairing();
    bool pairingActive() const;
    int  pairingSecondsLeft() const;
    const char *pairResult() const;      // idle/listening/confirming/paired/
                                         // timeout/full/pin-mismatch/storage-error/cancelled
    EspnowPairOutcome pairOutcome() const;   // pairResult() classified for LED/UI
};

extern EspnowLink espnowLink;
void espnow_register_console(void);
// True once the ESP-NOW serial REPL has taken ownership of the USB-Serial-JTAG
// console. Used to keep Improv Serial and this REPL mutually exclusive (a single
// console owner) — see enableFallbackAP().
bool espnow_console_started(void);
// The full "forget remote" flow shared by the web and button paths: clear the
// bonds, show the SLED_UNPAIR blink, esp_restart(). Callable from any task —
// it lands in the cross-task mailbox and EspnowLink::loop() (main task, which
// owns both the sl2 core and the LED strip) runs the terminal sequence.
void espnow_forget_and_restart(void);
// Factory-reset erase: wipes every NVS namespace but writes the unit's Serin
// Link identity (namespace "serinlink", key "sl2_id") back, so dials that
// pinned it can pair again. Leaves NVS deinitialized for the caller's
// restart. False if the erase or the write-back failed.
bool espnow_factory_erase_nvs(void);
