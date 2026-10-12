#define CASE(name) if (!strcmp(argv[1], name))
static void event(int e,const char* reason=nullptr){hap_event_handler(nullptr,HAP_EVENT,e,(void*)reason);}
static bool start(){return homekit_init("Test","Serin","CN105","serial","1.0",nullptr);}
static bool storedPaired(){auto& ns=hkNvs.committed["ac-settings"];return ns.count("hkPaired")&&ns["hkPaired"]==std::vector<uint8_t>{1};}
static void reboot(){settings=SettingsStore{};hkNvs.pending.clear();settings.begin();controllers=0;s_started=false;s_pairingsReset=false;}
int main(int,char**argv){
 settings.begin();
 CASE("initial_setup") {
  CHECK(!storedPaired());CHECK(start());CHECK(!storedPaired());
  CHECK(!strcmp(webHkSetupValue(true,controllers,homekit_get_setup_code()),"234-56-789"));
 }
 CASE("live_count_fallback_hides_credentials") {
  CHECK(start());controllers=1;
  CHECK(httpSetup().find("234-56-789")==std::string::npos);
  CHECK(wsSetup().find("234-56-789")==std::string::npos);
  CHECK(wsSetup().find("X-HM://")==std::string::npos);
 }
 CASE("startup_latches_existing_pairing") {
  controllers=1;CHECK(start());CHECK(storedPaired());
 }
 CASE("pairing_survives_recovery_ap") {
  CHECK(start());controllers=1;event(HAP_EVENT_CTRL_PAIRED);CHECK(storedPaired());
  int commits=hkNvs.commits;event(HAP_EVENT_CTRL_CONNECTED);CHECK(hkNvs.commits==commits);
  controllers=0;event(HAP_EVENT_CTRL_UNPAIRED);CHECK(storedPaired());
  CHECK(httpSetup().find("234-56-789")==std::string::npos);
  CHECK(wsSetup().find("234-56-789")==std::string::npos);
  CHECK(wsSetup().find("X-HM://")==std::string::npos);
  reboot();settings.save();CHECK(storedPaired());
  CHECK(httpSetup().find("234-56-789")==std::string::npos);
  CHECK(wsSetup().find("234-56-789")==std::string::npos);
  CHECK(wsSetup().find("X-HM://")==std::string::npos);
  CHECK(wsSetup().find("\"hkPaired\":true")!=std::string::npos);
 }
 CASE("reset_failure_preserves_pairing") {
  controllers=1;CHECK(start());CHECK(storedPaired());resetResult=ESP_FAIL;
  CHECK(!homekit_reset_pairings());CHECK(storedPaired());reboot();settings.save();CHECK(storedPaired());
 }
 CASE("queued_reset_clears_on_completion_without_relatching") {
  controllers=2;CHECK(start());CHECK(storedPaired());CHECK(homekit_reset_pairings());CHECK(storedPaired());
  event(HAP_EVENT_CTRL_DISCONNECTED);CHECK(storedPaired());
  event(HAP_EVENT_ACC_REBOOTING,HAP_REBOOT_REASON_RESET_PAIRINGS);CHECK(!storedPaired());
  // The SDK erases storage but leaves its in-memory controller entries valid.
  CHECK(homekit_get_controller_count()==2);event(HAP_EVENT_CTRL_CONNECTED);settings.save();CHECK(!storedPaired());
  reboot();settings.save();CHECK(!storedPaired());CHECK(start());
  CHECK(httpSetup().find("234-56-789")!=std::string::npos);
  CHECK(wsSetup().find("X-HM://test")!=std::string::npos);
  controllers=1;event(HAP_EVENT_CTRL_PAIRED);CHECK(storedPaired());
 }
 CASE("ordinary_reboot_preserves_pairing") {
  controllers=1;CHECK(start());CHECK(storedPaired());event(HAP_EVENT_ACC_REBOOTING,"reboot_acc");
  reboot();settings.save();CHECK(storedPaired());
 }
}
