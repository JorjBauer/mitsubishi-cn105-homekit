#include "host.h"
constexpr uint32_t BLE_RESEND_MIN_MS = 5000, BLE_KEEPALIVE_MS = 20000;
struct BleSensorCfg { char addr[18] = {}; char name[24] = {}; };
struct DeviceSettings {
    int roomMode = 0, roomSingle = 2, roomStaleTimeoutS = 600;
    uint8_t roomMembers = 0;
    int roomOffsets[6] = {};
    BleSensorCfg bleSensors[4];
};
struct Settings { DeviceSettings s; DeviceSettings& get() { return s; } void save() {} } settings;
namespace BleSensor { int feedSlot(); void setSensor(int, const char*, const char*); }
static bool validMac(const char* s) { return strlen(s) == 17; }
static std::atomic<bool> s_discoveryMode{false};
#include "ble_state.inc"
static bool wantScan() { return true; }
static void startScan() {}
struct SensorReading { float temp = NAN, hum = NAN; int8_t batt = -1; };
static const char* replacement = nullptr;
// Pause at the real callback's unlocked decode boundary, then perform the
// actual setter as the httpd task could before the callback reacquires its lock.
static const char* decodeAdvertisement(const uint8_t*, uint8_t, const char*, SensorReading& r) {
    if (replacement) BleSensor::setSensor(0, replacement, nullptr);
    r.temp = 18; r.hum = 40; r.batt = 90;
    return "Old sensor";
}
static void extractDeviceName(const uint8_t*, uint8_t, char*, size_t) {}
static void addDiscoveryResult(const char*, const char*, const char*, int, float, float) {}
constexpr int BLE_GAP_EVENT_DISC = 1, BLE_GAP_EVENT_DISC_COMPLETE = 2;
struct ble_gap_disc_desc { struct { uint8_t val[6]; } addr; uint8_t* data; uint8_t length_data; int rssi; };
struct ble_gap_event { int type; struct ble_gap_disc_desc disc; };
#include "ble_functions.inc"

int main(int, char** argv) {
    strcpy(settings.get().bleSensors[0].addr, "aa:bb:cc:dd:ee:ff");
    setSlotMac(s_slots[0], settings.get().bleSensors[0].addr);
    s_slots[0].valid = true;
    resetSlotReadings(s_slots[0]);
    CASE("replacement_during_decode_discards_old_sample") { replacement = "11:22:33:44:55:66"; }
    CASE("deletion_during_decode_discards_old_sample") { replacement = ""; }
    CASE("unchanged_slot_accepts_sample") { replacement = nullptr; }
    ble_gap_event event = {};
    event.type = BLE_GAP_EVENT_DISC;
    const uint8_t address[6] = {0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa};
    memcpy(event.disc.addr.val, address, 6);
    gap_event_cb(&event, nullptr);
    if (replacement) {
        CHECK(std::isnan(s_slots[0].temp));
        CHECK(std::isnan(s_slots[0].hum));
        CHECK(s_slots[0].lastUpdate == 0);
        CHECK(s_slots[0].type == nullptr);
    } else {
        CHECK(s_slots[0].temp == 18);
        CHECK(s_slots[0].lastUpdate == now_ms);
    }
}
