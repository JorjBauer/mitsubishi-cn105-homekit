#define CASE(name) if (!strcmp(argv[1], name))
int main(int, char** argv) {
 Pump pump; CN105Controller c;
 CASE("poll_after_rollover") {
  now_ms=UINT32_MAX-1000;c.begin(&pump);ticks(c);
  CHECK(pump.infos==5);int before=pump.infos;
  now_ms=1000;ticks(c,640,100);
  CHECK(pump.infos>=before+100);CHECK(pump.connects==1);CHECK(c.isHealthy());
 }
 CASE("receive_time_does_not_restart_poll_early") {
  now_ms=5000;pump.readCostMs=1;c.begin(&pump);ticks(c);
  CHECK(pump.infos==5);ticks(c,20,10);CHECK(pump.infos==5);
 }
 CASE("set_deferral_across_rollover") {
  now_ms=UINT32_MAX-1000;c.begin(&pump);ticks(c);
  c.setTargetTemp(24);c.sendPendingChanges();c.loop();int before=pump.infos;
  now_ms+=CN105_DEFER_DELAY+CN105_UPDATE_INTERVAL-1;c.loop();CHECK(pump.infos==before);
  ++now_ms;c.loop();CHECK(pump.infos==before+1);ticks(c);CHECK(pump.infos==before+5);
 }
 CASE("remote_deferral_across_rollover") {
  now_ms=UINT32_MAX-1000;c.begin(&pump);ticks(c);
  c.sendRemoteTemperature(19);c.loop();int before=pump.infos;
  now_ms+=CN105_DEFER_DELAY+CN105_UPDATE_INTERVAL-1;c.loop();CHECK(pump.infos==before);
  ++now_ms;c.loop();CHECK(pump.infos==before+1);
 }
 CASE("reconnect_polls_immediately") {
  now_ms=UINT32_MAX-1000;c.begin(&pump);ticks(c);
  pump.responding=false;now_ms+=15000;ticks(c);CHECK(!c.isHealthy());
  pump.responding=true;now_ms+=4000;int before=pump.infos;ticks(c);
  CHECK(c.isHealthy());CHECK(pump.infos>=before+5);
 }
 CASE("remote_written_since_boot_latches_on_first_send") {
  // main.cpp persists "this device has fed the pump" off this flag; a bare
  // boot, polling included, must not trip it.
  now_ms=5000;c.begin(&pump);ticks(c,100,100);CHECK(!c.remoteWrittenSinceBoot());
  c.sendRemoteTemperature(21);CHECK(c.remoteWrittenSinceBoot());
  ticks(c,100,100);CHECK(c.remoteWrittenSinceBoot());
 }
 CASE("first_connect_no_clear_unless_enabled") {
  // Default: a device that was never told it fed the pump leaves the
  // register alone — it may belong to a wall controller, not to us.
  now_ms=5000;c.begin(&pump);ticks(c,100,100);
  CHECK(c.isHealthy());CHECK(pump.remoteClears==0);CHECK(pump.remoteSets==0);
 }
 CASE("first_connect_clears_when_enabled_and_no_source_feeds") {
  now_ms=5000;c.enableBootClear(true);c.begin(&pump);ticks(c);
  CHECK(pump.infos==5);            // the first poll still runs straight after the handshake
  CHECK(pump.remoteClears==1);CHECK(pump.remoteSets==0);
  ticks(c,100,100);CHECK(pump.remoteClears==1);
  pump.responding=false;now_ms+=15000;ticks(c);CHECK(!c.isHealthy());
  pump.responding=true;now_ms+=4000;ticks(c,100,100);
  CHECK(c.isHealthy());CHECK(pump.remoteClears==1);   // first connect after boot only
 }
 CASE("first_connect_no_clear_when_source_feeds") {
  now_ms=5000;c.enableBootClear(true);c.sendRemoteTemperature(21);c.begin(&pump);ticks(c,100,100);
  CHECK(pump.remoteSets==1);CHECK(pump.remoteClears==0);
 }
 CASE("first_connect_no_clear_when_source_starts_during_first_poll") {
  now_ms=5000;c.enableBootClear(true);c.begin(&pump);c.loop();now_ms+=10;c.loop();CHECK(c.isConnected());
  c.sendRemoteTemperature(21);ticks(c,100,100);
  CHECK(pump.remoteSets==1);CHECK(pump.remoteClears==0);
 }
}
