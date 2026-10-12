#define CASE(name) if (!strcmp(argv[1], name))
int main(int, char** argv) {
 CN105Controller c;now_ms=10000;s_bleEnabled.store(true);
 for(auto& sl:s_slots){sl.temp=NAN;sl.lastUpdate=0;}
 s_slots[0].valid=true;s_slots[0].temp=18;s_slots[0].lastUpdate=1000;
 s_slots[1].valid=true;BleSensor::loop(c);CHECK(c.temp==18&&c.sends==1);
 CASE("never_seen_replacement_releases_override") {
  settings.get().roomSingle=ROOM_MEMBER_BLE0+1;++now_ms;BleSensor::loop(c);++now_ms;BleSensor::loop(c);
  CHECK(c.clears==1);CHECK(std::isnan(c.temp));
  now_ms+=690000;BleSensor::loop(c);CHECK(c.clears==1);
 }
 CASE("disconnected_replacement_retains_clear") {
  c.connected=false;settings.get().roomSingle=ROOM_MEMBER_BLE0+1;++now_ms;BleSensor::loop(c);
  CHECK(c.clears==0);c.connected=true;++now_ms;BleSensor::loop(c);CHECK(c.clears==1);
 }
 CASE("fresh_replacement_supersedes_deferred_clear") {
  c.connected=false;settings.get().roomSingle=ROOM_MEMBER_BLE0+1;++now_ms;BleSensor::loop(c);
  s_slots[1].temp=23;s_slots[1].lastUpdate=now_ms;
  c.connected=true;++now_ms;BleSensor::loop(c);CHECK(c.temp==23);CHECK(c.clears==0);
  ++now_ms;BleSensor::loop(c);CHECK(c.temp==23);CHECK(c.clears==0);
 }
 CASE("stale_replacement_does_not_inherit_revert_flag") {
  now_ms=602000;BleSensor::loop(c);++now_ms;BleSensor::loop(c);CHECK(c.clears==1);
  s_slots[1].temp=23;s_slots[1].lastUpdate=now_ms;settings.get().roomSingle=ROOM_MEMBER_BLE0+1;
  ++now_ms;BleSensor::loop(c);CHECK(c.temp==23);CHECK(!s_staleReverted.load());
  now_ms+=600000;BleSensor::loop(c);++now_ms;BleSensor::loop(c);CHECK(c.clears==2);
 }
 CASE("nan_replacement_releases_override") {
  settings.get().roomSingle=ROOM_MEMBER_BLE0+1;s_slots[1].lastUpdate=now_ms;
  ++now_ms;BleSensor::loop(c);++now_ms;BleSensor::loop(c);CHECK(c.clears==1);
 }
 CASE("handoff_does_not_clear_an_active_link_sensor") {
  settings.get().roomSingle=ROOM_MEMBER_LINK;LinkSensor::active=true;c.temp=24;
  ++now_ms;BleSensor::loop(c);++now_ms;BleSensor::loop(c);CHECK(c.temp==24);CHECK(c.clears==0);
 }
}
