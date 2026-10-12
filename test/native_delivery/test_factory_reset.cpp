#define CASE(name) if (!strcmp(argv[1], name))
static std::vector<uint8_t> bytes(size_t n,uint8_t seed){std::vector<uint8_t> v(n);for(size_t i=0;i<n;i++)v[i]=(uint8_t)(seed+i*7);return v;}
int main(int, char** argv) {
 const auto id=bytes(SL2_ED25519_PRIV+SL2_ED25519_PUB,3);
 nvs.ns["serinlink"][SL2_KV_IDENTITY]=id;
 nvs.ns["serinlink"][SL2_KV_BONDS]=bytes(64,9);
 nvs.ns["ac-settings"]["hkPaired"]={1};
 nvs.ns["settings"]["deviceName"]=bytes(8,1);
 nvs.ns["nvs.net80211"]["sta.ssid"]=bytes(32,2);
 CASE("identity_survives_factory_erase") {
  CHECK(espnow_factory_erase_nvs());
  CHECK(nvs.erases==1);
  CHECK(nvs.ns.size()==1);CHECK(nvs.ns.count("serinlink")==1);
  CHECK(nvs.ns["serinlink"].size()==1);                      // bonds are gone
  CHECK(nvs.ns["serinlink"][SL2_KV_IDENTITY]==id);           // byte for byte
  CHECK(!nvs.init);   // closed again: nothing else lands before the restart
 }
 CASE("no_identity_erases_everything") {
  nvs.ns["serinlink"].erase(SL2_KV_IDENTITY);
  CHECK(espnow_factory_erase_nvs());CHECK(nvs.erases==1);CHECK(nvs.ns.empty());CHECK(nvs.inits==0);
 }
 CASE("malformed_identity_is_not_restored") {
  nvs.ns["serinlink"][SL2_KV_IDENTITY]=bytes(40,5);
  CHECK(espnow_factory_erase_nvs());CHECK(nvs.erases==1);CHECK(nvs.ns.empty());
 }
}
