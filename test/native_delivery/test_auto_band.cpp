#define CASE(name) if (!strcmp(argv[1], name))
int main(int, char** argv) {
 Pump pump;CN105Controller c;s_ctrl=&c;now_ms=5000;c.begin(&pump);ticks(c);CHECK(c.isHealthy());
 sl2_cmd_pkt cmd={};sl2_hvac_state_t state={};sl2_caps_pkt caps={};
 CASE("advertise_and_echo_actual_auto_band") {
  h_get_caps(nullptr,&caps);CHECK(caps.modes&(1u<<SL2_MODE_HEAT_COOL));CHECK(!(caps.modes&(1u<<SL2_MODE_AUTO)));
  CHECK(caps.band_min_gap_dc==20);CHECK(caps.set_step_dc==5);
  h_get_state(nullptr,&state);CHECK(state.mode==SL2_MODE_HEAT_COOL);CHECK(state.set_low_dc==200&&state.set_high_dc==250);
  settings.get().modeMask &= ~MODE_CAP_AUTO;h_get_caps(nullptr,&caps);CHECK(!(caps.modes&(1u<<SL2_MODE_HEAT_COOL)));
 }
 CASE("band_edit_survives_periodic_auto_engine") {
  cmd.set_low_dc=270;cmd.set_high_dc=290;CHECK(h_apply(nullptr,SL2_CM_TEMP_BAND,&cmd));
  CHECK(settings.get().heatingThreshold==27&&settings.get().coolingThreshold==29);CHECK(settings.saves==1);
  h_get_state(nullptr,&state);CHECK(state.set_low_dc==270&&state.set_high_dc==290);
  run_auto_sync(c);c.loop();CHECK(c.getEffectiveState().targetTemp==27);
  now_ms+=2000;run_auto_sync(c);CHECK(c.getEffectiveState().targetTemp==27);
 }
 CASE("combined_mode_band_and_gap") {
  c.setMode(CN105_MODE_HEAT);c.sendPendingChanges();c.loop();
  cmd.mode=SL2_MODE_HEAT_COOL;cmd.set_low_dc=300;cmd.set_high_dc=305;
  CHECK(h_apply(nullptr,SL2_CM_MODE|SL2_CM_TEMP_BAND,&cmd));
  CHECK(settings.get().heatingThreshold==28.5f&&settings.get().coolingThreshold==30.5f);
  h_get_state(nullptr,&state);CHECK(state.mode==SL2_MODE_HEAT_COOL);CHECK(state.set_low_dc==285&&state.set_high_dc==305);
 }
 CASE("band_clamps_and_snaps_with_controller_constraints") {
  cmd.set_low_dc=100;cmd.set_high_dc=162;CHECK(h_apply(nullptr,SL2_CM_TEMP_BAND,&cmd));
  CHECK(settings.get().heatingThreshold==16&&settings.get().coolingThreshold==18);
  cmd.set_low_dc=201;cmd.set_high_dc=249;CHECK(h_apply(nullptr,SL2_CM_TEMP_BAND,&cmd));
  CHECK(settings.get().heatingThreshold==20&&settings.get().coolingThreshold==25);
 }
 CASE("invalid_or_disabled_band_cannot_change_thresholds") {
  cmd.set_low_dc=SL2_DC_NA;cmd.set_high_dc=290;CHECK(!h_apply(nullptr,SL2_CM_TEMP_BAND,&cmd));
  CHECK(settings.saves==0);CHECK(settings.get().heatingThreshold==20&&settings.get().coolingThreshold==25);
  settings.get().modeMask &= ~MODE_CAP_AUTO;cmd.mode=SL2_MODE_HEAT_COOL;cmd.set_low_dc=270;
  h_apply(nullptr,SL2_CM_MODE|SL2_CM_TEMP_BAND,&cmd);CHECK(settings.saves==0);
 }
 CASE("single_setpoint_preserved_when_leaving_auto") {
  cmd.mode=SL2_MODE_HEAT;cmd.set_dc=290;CHECK(h_apply(nullptr,SL2_CM_MODE|SL2_CM_TEMP,&cmd));
  CHECK(c.getEffectiveState().targetTemp==29);CHECK(c.getEffectiveState().mode==CN105_MODE_HEAT);
  CHECK(settings.get().heatingThreshold==20&&settings.get().coolingThreshold==25);
  h_get_state(nullptr,&state);CHECK(state.set_dc==290);CHECK(state.set_low_dc==SL2_DC_NA&&state.set_high_dc==SL2_DC_NA);
 }
 CASE("legacy_auto_single_target_is_not_temporarily_accepted") {
  cmd.set_dc=290;CHECK(!h_apply(nullptr,SL2_CM_TEMP,&cmd));CHECK(c.getEffectiveState().targetTemp!=29);
 }
 CASE("legacy_unit_half_degree_threshold_sends_once") {
  pump.legacy=true;pump.roomRaw=164;c._tempMode=false;settings.get().heatingThreshold=20.5f;
  ticks(c,300,10);CHECK(c.getEffectiveState().roomTemp==18);
  int sets0=pump.sets,sets1=-1,infos1=0;
  for(int t=0;t<60000;t+=10){
   if(t%2000==0){run_auto_sync(c);if(sets1<0&&pump.sets>sets0){sets1=pump.sets;infos1=pump.infos;}}
   c.loop();now_ms+=10;
  }
  CHECK(sets1==sets0+1);CHECK(pump.sets==sets1);CHECK(pump.infos>infos1);CHECK(pump.legacyC==20);
 }
 CASE("legacy_unit_confirms_truncated_target") {
  pump.legacy=true;pump.roomRaw=164;c._tempMode=false;settings.get().heatingThreshold=20.5f;
  ticks(c,300,10);run_auto_sync(c);ticks(c,300,10);
  CHECK(pump.legacyC==20);CHECK(c.getEffectiveState().targetTemp==20);CHECK(!c._wanted.hasTemp);
 }
 CASE("enhanced_unit_half_degree_unchanged") {
  pump.roomRaw=164;settings.get().heatingThreshold=20.5f;CHECK(c._tempMode);
  ticks(c,300,10);int sets0=pump.sets;
  for(int t=0;t<60000;t+=10){if(t%2000==0)run_auto_sync(c);c.loop();now_ms+=10;}
  CHECK(pump.sets==sets0+1);CHECK(pump.target==(uint8_t)(20.5f*2+128));CHECK(c.getEffectiveState().targetTemp==20.5f);
 }
}
