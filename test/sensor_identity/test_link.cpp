#include "host.h"
#include "sl2_link.h"
#include "room_feed.h"
#include "link_units.h"
struct DeviceSettings { uint64_t roomSourceId = 0; int roomMode = 0, roomSingle = 1, roomStaleTimeoutS = 600; };
struct Settings { DeviceSettings s; DeviceSettings& get() { return s; } void save() {} } settings;
static int room_offset_dc(const DeviceSettings&, int) { return 0; }
namespace RoomAvg {
    bool isFeeding() { return false; }
    bool averageSelectable() { return false; }
    bool memberAvailable(int bit);
}
class CN105Controller {
public:
    bool isConnected() { return true; }
    void sendRemoteTemperature(float) {}
    void clearRemoteTemperature() {}
};
#include "link_sensor.inc"
static sl2_link_t s_link;
#define ROOM_CATALOG_MAX (2 + ROOM_MAX_BLE_SENSORS + 1 + SL2_MAX_DIALS)
#include "room_adapter.inc"
static const uint8_t a[6] = {1,2,3,4,5,6}, b[6] = {6,5,4,3,2,1};
static uint64_t id(const uint8_t* mac) { return sl2_room_source_mac_id(SL2_ROOM_SOURCE_NS_LINK, mac); }
static uint32_t clock_ms(void*) { return now_ms; }
static void sensor(void*, const uint8_t* mac, const sl2_dial_sensor_pkt* p, bool) { LinkSensor::feed(mac, p); }
static void report(const uint8_t* mac, bool hasSensor, uint16_t epoch = 123) {
    sl2_dial_sensor_pkt p = {};
    p.type = SL2_PKT_DIAL_SENSOR; p.version = SL2_PROTO_VERSION;
    p.epoch = epoch; p.flags = hasSensor ? SL2_DSF_HAS_SENSOR : 0;
    p.temp_cc = hasSensor ? 2100 : SL2_CC_NA; p.hum_cc = SL2_HUM_CC_NA;
    p.want_src = SL2_ROOMSRC_NOEDIT;
    sl2_link_on_recv(&s_link, mac, s_link.own_mac, (const uint8_t*)&p, sizeof p);
}
static bool selectable(const uint8_t* mac) {
    sl2_room_source_entry entries[ROOM_CATALOG_MAX] = {};
    int count = room_catalog_build(entries, ROOM_CATALOG_MAX);
    for (int i = 0; i < count; ++i)
        if (entries[i].id == id(mac)) return (entries[i].flags & SL2_ROOM_SOURCE_F_SELECTABLE) != 0;
    return false;
}
int main(int, char** argv) {
    sl2_port_t port = {}; port.now_ms = clock_ms;
    sl2_hvac_iface_t hvac = {}; hvac.room_sensor = sensor;
    sl2_link_init(&s_link, &port, nullptr, &hvac);
    s_link.started = true; s_link.epoch = 123; s_link.n_dials = 2;
    memcpy(s_link.dial[0].bond.mac, a, 6); memcpy(s_link.dial[1].bond.mac, b, 6);
    s_link.dial[0].bond.flags = s_link.dial[1].bond.flags = SL2_BOND_F_EPOCH;
    settings.get().roomSourceId = id(b); // persisted selection; B is offline after boot
    CASE("offline_pin_does_not_block_live_dial_selection") {
        report(a, true);
        CHECK(std::isnan(LinkSensor::temperature())); // A cannot feed B's pin
        CHECK(h_room_source_set(nullptr, room_catalog_revision(), id(a)) == SL2_ROOM_SET_OK);
        CHECK(settings.get().roomSourceId == id(a));
        report(a, true);
        CHECK(LinkSensor::temperature() == 21);
    }
    CASE("catalog_capability_belongs_to_each_dial") {
        report(a, true); report(b, false);
        CHECK(selectable(a));
        CHECK(!selectable(b));
    }
    CASE("sensorless_dial_cannot_borrow_another_dials_capability") {
        settings.get().roomSourceId = id(a);
        report(a, true); report(b, false);
        CHECK(h_room_source_set(nullptr, room_catalog_revision(), id(b)) == SL2_ROOM_SET_BAD_SOURCE);
        CHECK(settings.get().roomSourceId == id(a));
    }
    CASE("rejected_epoch_cannot_make_a_dial_selectable") {
        report(a, true, 122);
        CHECK(!selectable(a));
        CHECK(h_room_source_set(nullptr, room_catalog_revision(), id(a)) == SL2_ROOM_SET_BAD_SOURCE);
    }
    CASE("offline_capable_dial_remains_selectable") {
        report(a, true);
        now_ms += 600000;
        CHECK(selectable(a));
        CHECK(h_room_source_set(nullptr, room_catalog_revision(), id(a)) == SL2_ROOM_SET_OK);
    }
}
