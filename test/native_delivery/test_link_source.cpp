#define CASE(name) if (!strcmp(argv[1], name))
static uint8_t a[6]={1,2,3,4,5,6},b[6]={6,5,4,3,2,1};
static uint64_t id(const uint8_t* mac){return sl2_room_source_mac_id(SL2_ROOM_SOURCE_NS_LINK,mac);}
static sl2_dial_sensor_pkt reading(int16_t t){sl2_dial_sensor_pkt p={};p.flags=SL2_DSF_HAS_SENSOR;p.temp_cc=t;p.hum_cc=5000;return p;}
int main(int, char** argv) {
 CN105Controller c;now_ms=10000;settings.get().roomSourceId=id(a);
 auto p=reading(1800);LinkSensor::feed(a,&p);LinkSensor::loop(c);CHECK(c.temp==18);
 CASE("pin_change_clears_and_hides_previous_reading") {
  settings.get().roomSourceId=id(b);
  CHECK(!LinkSensor::isActive());CHECK(std::isnan(LinkSensor::temperature()));
  CHECK(std::isnan(LinkSensor::humidity()));CHECK(LinkSensor::lastUpdateAge()==UINT32_MAX);
  CHECK(LinkSensor::status()==SL2_ROOMST_UNAVAILABLE);CHECK(LinkSensor::hasSensor()); // capability gate still permits changing the pin
  now_ms+=1000;LinkSensor::loop(c);++now_ms;LinkSensor::loop(c);
  CHECK(c.clears==1);CHECK(std::isnan(c.temp));
  now_ms+=22000;LinkSensor::loop(c);CHECK(c.clears==1);CHECK(c.sends==1);
 }
 CASE("offline_pin_can_be_reselected_through_catalog") {
  const uint32_t revision=room_catalog_revision();
  CHECK(h_room_source_set(nullptr,revision,id(b))==SL2_ROOM_SET_OK);
  CHECK(!LinkSensor::isActive());CHECK(LinkSensor::status()==SL2_ROOMST_UNAVAILABLE);
  LinkSensor::loop(c);++now_ms;LinkSensor::loop(c);CHECK(std::isnan(c.temp));
  CHECK(h_room_source_set(nullptr,revision,id(a))==SL2_ROOM_SET_OK);
  CHECK(settings.get().roomSourceId==id(a));CHECK(LinkSensor::isActive());
  LinkSensor::loop(c);CHECK(c.temp==18);
 }
 CASE("flags_only_new_sender_cannot_claim_old_reading") {
  settings.get().roomSourceId=id(b);auto flags=reading(SL2_CC_NA);flags.hum_cc=SL2_HUM_CC_NA;
  LinkSensor::feed(b,&flags);CHECK(LinkSensor::hasSensor());CHECK(!LinkSensor::isActive());
  CHECK(std::isnan(LinkSensor::temperature()));CHECK(LinkSensor::lastUpdateAge()==UINT32_MAX);
  LinkSensor::loop(c);++now_ms;LinkSensor::loop(c);CHECK(c.clears==1);
  p.temp_cc=2300;LinkSensor::feed(b,&p);LinkSensor::loop(c);CHECK(c.temp==23);CHECK(LinkSensor::isActive());
 }
 CASE("flags_only_same_sender_does_not_refresh") {
  now_ms+=600000;auto flags=reading(SL2_CC_NA);LinkSensor::feed(a,&flags);
  CHECK(LinkSensor::isStale());CHECK(!LinkSensor::isActive());CHECK(LinkSensor::lastUpdateAge()==600000);
 }
 CASE("disconnected_handoff_recovers_without_old_clear") {
  c.connected=false;settings.get().roomSourceId=id(b);LinkSensor::loop(c);CHECK(c.clears==0);
  p.temp_cc=2300;now_ms+=1000;LinkSensor::feed(b,&p);LinkSensor::loop(c);
  c.connected=true;LinkSensor::loop(c);CHECK(c.clears==0);CHECK(c.temp==23);
 }
 CASE("disconnected_handoff_eventually_clears") {
  c.connected=false;settings.get().roomSourceId=id(b);LinkSensor::loop(c);CHECK(c.clears==0);
  c.connected=true;LinkSensor::loop(c);CHECK(c.clears==1);CHECK(std::isnan(c.temp));
 }
}
