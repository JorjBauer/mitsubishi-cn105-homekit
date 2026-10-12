#define CASE(name) if (!strcmp(argv[1], name))
static uint8_t a[6]={1,2,3,4,5,6},b[6]={6,5,4,3,2,1};
static uint64_t id(const uint8_t* mac){return sl2_room_source_mac_id(SL2_ROOM_SOURCE_NS_LINK,mac);}
static sl2_dial_sensor_pkt reading(int16_t t){sl2_dial_sensor_pkt p={};p.flags=SL2_DSF_HAS_SENSOR;p.temp_cc=t;p.hum_cc=5000;return p;}
static void average(){auto& s=settings.get();s.roomMode=1;s.roomMembers=1u<<ROOM_MEMBER_LINK;s.roomSourceId=SL2_ROOM_SOURCE_AVERAGE_ID;}
static void tick(CN105Controller& c){LinkSensor::loop(c);RoomAvg::loop(c);}   // main.cpp order
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
 CASE("average_alternating_dials_hold_a_stable_mean") {
  average();auto pa=reading(2000),pb=reading(2200);
  for(int i=0;i<12;i++){
   LinkSensor::feed(i%2?b:a,i%2?&pb:&pa);
   if(i){tick(c);CHECK(RoomAvg::status().effective==21.0f);CHECK(c.temp==21);}
   now_ms+=1000;
  }
  CHECK(c.sends==2);CHECK(c.clears==0);CHECK(RoomAvg::status().contributors==(1u<<ROOM_MEMBER_LINK));
 }
 CASE("average_flags_only_dial_keeps_other_reading") {
  average();auto pa=reading(2000);LinkSensor::feed(a,&pa);
  auto flags=reading(SL2_CC_NA);flags.hum_cc=SL2_HUM_CC_NA;LinkSensor::feed(b,&flags);
  tick(c);
  CHECK(LinkSensor::isActive());CHECK(LinkSensor::temperature()==20);CHECK(LinkSensor::humidity()==50);
  CHECK(RoomAvg::status().effective==20.0f);CHECK(c.temp==20);
 }
 CASE("pinned_dial_ignores_other_dial") {
  auto pb=reading(2500);LinkSensor::feed(b,&pb);
  CHECK(LinkSensor::temperature()==18);CHECK(LinkSensor::isActive());
  now_ms+=6000;LinkSensor::loop(c);CHECK(c.temp==18);CHECK(c.sends==1);
  // The other dial's reading was kept, so moving the pin is a live hand-off.
  settings.get().roomSourceId=id(b);LinkSensor::loop(c);
  CHECK(c.temp==25);CHECK(c.sends==2);CHECK(c.clears==0);
 }
 CASE("each_dial_keeps_its_own_reading") {
  auto pb=reading(2250);pb.hum_cc=4000;now_ms+=500;LinkSensor::feed(b,&pb);
  LinkSensor::Reading r[SL2_MAX_DIALS];CHECK(LinkSensor::readings(r)==2);
  CHECK(!memcmp(r[0].mac,a,6));CHECK(r[0].temperature==18);CHECK(r[0].humidity==50);CHECK(r[0].ageMs==500);CHECK(r[0].fresh);
  CHECK(!memcmp(r[1].mac,b,6));CHECK(r[1].temperature==22.5f);CHECK(r[1].humidity==40);CHECK(r[1].ageMs==0);CHECK(r[1].fresh);
  auto flags=reading(SL2_CC_NA);flags.flags=0;flags.hum_cc=SL2_HUM_CC_NA;LinkSensor::feed(b,&flags);
  CHECK(LinkSensor::readings(r)==2);CHECK(r[1].temperature==22.5f);CHECK(!r[1].hasSensor);CHECK(r[0].hasSensor);
  now_ms+=600000;CHECK(LinkSensor::readings(r)==2);CHECK(!r[0].fresh);CHECK(!r[1].fresh);
 }
 CASE("forgotten_dial_leaves_the_average") {
  average();auto pa=reading(2000),pb=reading(2200);LinkSensor::feed(a,&pa);LinkSensor::feed(b,&pb);
  tick(c);CHECK(c.temp==21);
  LinkSensor::forget(b);LinkSensor::Reading r[SL2_MAX_DIALS];CHECK(LinkSensor::readings(r)==1);
  now_ms+=6000;tick(c);CHECK(RoomAvg::status().effective==20.0f);CHECK(c.temp==20);
  LinkSensor::forget(nullptr);CHECK(LinkSensor::readings(r)==0);CHECK(!LinkSensor::hasSensor());
  now_ms+=6000;tick(c);++now_ms;tick(c);CHECK(RoomAvg::status().fallback);CHECK(c.clears==1);
 }
 CASE("full_table_replaces_least_recently_heard_dial") {
  settings.get().roomSourceId=SL2_ROOM_SOURCE_INTERNAL_ID;settings.get().roomSingle=ROOM_MEMBER_INTERNAL;
  const uint8_t more[4][6]={{9,9,9,9,9,1},{9,9,9,9,9,2},{9,9,9,9,9,3},{9,9,9,9,9,4}};
  for(int i=0;i<3;i++){now_ms+=1000;auto q=reading(2000+i);LinkSensor::feed(i?more[i]:b,&q);}   // a,b,+2: full
  now_ms+=1000;auto q=reading(1900);LinkSensor::feed(a,&q);           // a heard again: b is now oldest
  now_ms+=1000;q=reading(2100);LinkSensor::feed(more[0],&q);
  LinkSensor::Reading r[SL2_MAX_DIALS];CHECK(LinkSensor::readings(r)==SL2_MAX_DIALS);
  bool haveA=false,haveB=false,haveNew=false;
  for(auto& x:r){haveA|=!memcmp(x.mac,a,6);haveB|=!memcmp(x.mac,b,6);haveNew|=!memcmp(x.mac,more[0],6);}
  CHECK(haveA);CHECK(!haveB);CHECK(haveNew);
 }
 CASE("unpinned_summary_is_the_mean_of_fresh_dials") {
  settings.get().roomSourceId=SL2_ROOM_SOURCE_INTERNAL_ID;settings.get().roomSingle=ROOM_MEMBER_INTERNAL;
  auto pb=reading(2200);pb.hum_cc=4000;LinkSensor::feed(b,&pb);
  CHECK(LinkSensor::temperature()==20);CHECK(LinkSensor::humidity()==45);CHECK(LinkSensor::isActive());
  now_ms+=599000;auto pa=reading(2400);LinkSensor::feed(a,&pa);   // b goes stale 1 s later
  now_ms+=1000;CHECK(LinkSensor::temperature()==24);CHECK(LinkSensor::lastUpdateAge()==1000);
  now_ms+=600000;CHECK(LinkSensor::isStale());CHECK(LinkSensor::temperature()==24);   // newest stale reading
  CHECK(LinkSensor::status()==SL2_ROOMST_STALE);
 }
 CASE("forget_dial_request_drops_one_bond_and_its_reading") {
  average();auto pa=reading(2000),pb=reading(2200);LinkSensor::feed(a,&pa);LinkSensor::feed(b,&pb);
  tick(c);CHECK(c.temp==21);
  CHECK(queue_forget_dial(b));CHECK(queue_forget_dial(b));   // a repeated click queues once
  drain_forget_dials();
  uint8_t m[6];CHECK(sl2_link_dial_count(&s_link)==1);CHECK(sl2_link_dial_mac(&s_link,0,m)&&!memcmp(m,a,6));
  LinkSensor::Reading r[SL2_MAX_DIALS];CHECK(LinkSensor::readings(r)==1);CHECK(!memcmp(r[0].mac,a,6));
  now_ms+=6000;tick(c);CHECK(c.temp==20);
  drain_forget_dials();CHECK(sl2_link_dial_count(&s_link)==1);   // nothing left queued
  const uint8_t stranger[6]={7,7,7,7,7,7};CHECK(queue_forget_dial(stranger));drain_forget_dials();
  CHECK(sl2_link_dial_count(&s_link)==1);CHECK(LinkSensor::readings(r)==1);
 }
 // Forgetting the pinned dial: its reading goes at once, the pump gets one
 // clear, and the 1 Hz reconcile repairs the dangling pin.
 CASE("forget_pinned_dial_with_two_left_reverts_to_internal") {
  const uint8_t third[6]={3,3,3,3,3,3};memcpy(s_link.mac[2],third,6);s_link.n=3;
  CHECK(queue_forget_dial(a));drain_forget_dials();CHECK(sl2_link_dial_count(&s_link)==2);
  now_ms+=1000;LinkSensor::loop(c);CHECK(!LinkSensor::isActive());
  room_source_reconcile_catalog(now_ms);
  CHECK(settings.get().roomSingle==ROOM_MEMBER_INTERNAL);CHECK(settings.get().roomSourceId==SL2_ROOM_SOURCE_INTERNAL_ID);
  now_ms+=1000;LinkSensor::loop(c);CHECK(c.clears==1);CHECK(std::isnan(c.temp));
  now_ms+=30000;LinkSensor::loop(c);room_source_reconcile_catalog(now_ms);
  CHECK(c.clears==1);CHECK(c.sends==1);CHECK(settings.get().roomSingle==ROOM_MEMBER_INTERNAL);
 }
 CASE("forget_pinned_dial_with_one_left_repins_it") {
  auto pb=reading(2200);LinkSensor::feed(b,&pb);
  CHECK(queue_forget_dial(a));drain_forget_dials();CHECK(sl2_link_dial_count(&s_link)==1);
  now_ms+=1000;LinkSensor::loop(c);CHECK(!LinkSensor::isActive());
  room_source_reconcile_catalog(now_ms);
  CHECK(settings.get().roomSingle==ROOM_MEMBER_LINK);CHECK(settings.get().roomSourceId==id(b));
  now_ms+=1000;LinkSensor::loop(c);
  CHECK(c.clears==1);CHECK(c.temp==22);CHECK(c.sends==2);CHECK(LinkSensor::isActive());
 }
}
