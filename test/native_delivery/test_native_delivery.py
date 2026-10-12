#!/usr/bin/env python3
"""Compile production control paths with host clock/UART/radio boundaries.

ESP-IDF headers are removed only for the host build. The adapter and BLE
functions are extracted intact: assertions execute their compiled bodies.
Each invocation owns its build directory, including concurrent repo runs.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
MAIN = HERE.parent.parent / "main"


def clean(name):
    return re.sub(r"^#(?:include[^\n]*|pragma once)\n", "", (MAIN / name).read_text(), flags=re.M)


def block(src, start):
    opening = src.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        if src[end] == "{":
            depth += 1
        elif src[end] == "}":
            depth -= 1
        end += 1
    return src[start:end]


def function(src, name):
    match = re.search(r"^(?:(?:static|inline) )*(?:bool|void|uint8_t|uint16_t|uint32_t|int|float|AutoTarget) "
                      + re.escape(name) + r"\(", src, re.M)
    assert match, name
    return block(src, match.start())


HOST = r'''
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <deque>
#include <atomic>
#include "sl2_proto.h"
#include "cn105_baud.h"
#include "uart_interface.h"
#define private public
using uart_port_t=int;
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(mux) ((void)(mux))
#define taskEXIT_CRITICAL(mux) ((void)(mux))
#define LOG_ERROR(...) ((void)0)
#define LOG_WARN(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr,"line %d: %s\n",__LINE__,#cond); exit(1); } } while(0)
static int currentLogLevel=0;
static constexpr int LOG_LEVEL_DEBUG=3;
static uint32_t now_ms=1000;
static uint32_t uptime_ms() {return now_ms;}
static const char* modeToLogStr(uint8_t){return "";}
static const char* fanToLogStr(uint8_t){return "";}
static const char* vaneToLogStr(uint8_t){return "";}
static const char* wideVaneToLogStr(uint8_t){return "";}
static const char* subModeToLogStr(uint8_t){return "";}
static const char* stageToLogStr(uint8_t){return "";}
static const char* autoSubModeToLogStr(uint8_t){return "";}
'''
CN105 = HOST + clean("cn105_protocol.h") + clean("cn105_protocol.cpp")
PUMP = r'''
class Pump : public UartInterface {
public:
 std::deque<uint8_t> rx;
 int infos=0,connects=0,sets=0;
 bool responding=true;
 uint32_t readCostMs=0;
 uint8_t mode=CN105_MODE_AUTO;
 uint8_t target=172,roomRaw=170;
 bool legacy=false;int legacyC=22;
 int read(uint8_t* b,size_t n) override {size_t i=0;while(i<n&&!rx.empty()){b[i++]=rx.front();rx.pop_front();}if(i)now_ms+=readCostMs;return i;}
 size_t available() override {return rx.size();}
 void flush() override {rx.clear();}
 void packet(uint8_t type,uint8_t subtype) {
  uint8_t p[22]={};CN105Controller::buildHeader(p,type,16);p[5]=subtype;
  if(subtype==CN105_INFO_SETTINGS){p[8]=1;p[9]=mode;if(legacy)p[10]=31-legacyC;else p[16]=target;}
  if(subtype==CN105_INFO_ROOMTEMP)p[11]=roomRaw;
  if(subtype==CN105_INFO_ERRORCODE)p[9]=0x80;
  p[21]=CN105Controller::calcChecksum(p,21);
  for(uint8_t b:p)rx.push_back(b);
 }
 void write(const uint8_t* b,size_t) override {
  if(b[1]==0x5a){++connects;if(responding)packet(CN105_PKT_CONNECT_OK,0);}
  if(b[1]==CN105_PKT_INFO_REQ){++infos;if(responding)packet(CN105_PKT_INFO_RESP,b[5]);}
  if(b[1]==CN105_PKT_SET){
   ++sets;
   if(b[5]==1){if(b[6]&CN105_FLAG_MODE)mode=b[9];if(b[6]&CN105_FLAG_TEMP){if(b[19])target=b[19];else legacyC=31-b[10];}}
   if(responding)packet(CN105_PKT_SET_ACK,0);
  }
 }
};
static void ticks(CN105Controller& c,int n=12,uint32_t step=10){for(int i=0;i<n;i++){c.loop();now_ms+=step;}}
'''

LINK = HOST + r'''
#include "room_feed.h"
#include "link_units.h"
constexpr int ROOM_MEMBER_LINK=1;
struct DeviceSettings{uint64_t roomSourceId=0;int roomMode=0,roomSingle=1,roomStaleTimeoutS=600;};
struct Settings{DeviceSettings s;DeviceSettings& get(){return s;}void save(){}}settings;
static int room_offset_dc(const DeviceSettings&,int){return 0;}
namespace RoomAvg {static bool feeding=false;bool isFeeding(){return feeding;}}
class CN105Controller{public:float temp=NAN;int sends=0,clears=0;bool connected=true;
 bool isConnected(){return connected;}void sendRemoteTemperature(float t){temp=t;++sends;}
 void clearRemoteTemperature(){temp=NAN;++clears;}};
''' + clean("link_sensor.h") + clean("link_sensor.cpp")

ble = (MAIN / "ble_sensor.cpp").read_text()
BLE = HOST + r'''
constexpr int ROOM_MAX_BLE_SENSORS=4,ROOM_MEMBER_LINK=1,ROOM_MEMBER_BLE0=2;
constexpr uint32_t BLE_RESEND_MIN_MS=5000,BLE_KEEPALIVE_MS=20000;
struct DeviceSettings{int roomMode=0,roomSingle=ROOM_MEMBER_BLE0,roomStaleTimeoutS=600;};
struct Settings{DeviceSettings s;DeviceSettings& get(){return s;}void save(){}}settings;
static float room_apply_offset(const DeviceSettings&,int,float t){return t;}
namespace LinkSensor{static bool active=false;bool isActive(){return active;}}
namespace RoomAvg{static bool feeding=false;bool isFeeding(){return feeding;}}
class CN105Controller{public:
 float temp=NAN;int sends=0,clears=0;bool connected=true;
 bool isConnected(){return connected;}
 void sendRemoteTemperature(float t){temp=t;++sends;}
 void clearRemoteTemperature(){temp=NAN;++clears;}
 static float quantizeRemoteTemp(float t);
};
namespace BleSensor{int feedSlot();void loop(CN105Controller&);}
static bool initNimble(){return true;}
static void deinitNimble(){}
static void stopScan(){}
static void startScan(){}
static bool wantScan(){return true;}
static std::atomic<bool> s_discoveryMode{false};
''' + ble[ble.index("struct SlotState"):ble.index("// ── Discovery state")]
BLE += "\n" + function((MAIN / "cn105_protocol.cpp").read_text(), "CN105Controller::quantizeRemoteTemp")
ble_functions = ["BleSensor::feedSlot", "freshAt"]
if re.search(r"^static void drainPendingClear\(", ble, re.M):
    ble_functions.append("drainPendingClear")
ble_functions.append("BleSensor::loop")
BLE += "\n" + "\n".join(function(ble, n) for n in ble_functions)

adapter = (MAIN / "espnow_link.cpp").read_text()

# Exercise the real catalog and selection adapter too. Only the bonded-dial
# store is a fixture; its two MACs stand for already-authenticated peers.
LINK += r'''
#include "sl2_bond.h"
constexpr int ROOM_MEMBER_INTERNAL=0,ROOM_MEMBER_BLE0=2,ROOM_MAX_BLE_SENSORS=4;
#define ROOM_CATALOG_MAX (2 + ROOM_MAX_BLE_SENSORS + 1 + SL2_MAX_DIALS)
struct BondedDials {};
static BondedDials s_link;
static int sl2_link_dial_count(const BondedDials*) {return 2;}
static bool sl2_link_dial_mac(const BondedDials*,int index,uint8_t out[6]) {
 const uint8_t macs[2][6]={{1,2,3,4,5,6},{6,5,4,3,2,1}};
 if(index<0||index>=2)return false;
 memcpy(out,macs[index],6);return true;
}
namespace RoomAvg {
 bool averageSelectable(){return false;}
 bool memberAvailable(int bit);
}
'''
LINK += "\n" + function((MAIN / "room_avg.cpp").read_text(), "RoomAvg::memberAvailable")
LINK += "\n" + function((MAIN / "esp_utils.h").read_text(), "fnv1a32")
LINK += "\n" + "\n".join(function(adapter, n) for n in
    ["copy_name", "room_catalog_build", "room_catalog_hash", "room_catalog_revision", "h_room_source_set"])
thermo_name = "homekit_thermostat.cpp" if (MAIN / "homekit_thermostat.cpp").exists() else "matter_thermostat.cpp"
thermo = (MAIN / thermo_name).read_text()
strings = (MAIN / "cn105_strings.h").read_text()
AUTO = CN105 + r'''
#include "mode_caps.h"
#include "sl2_link.h"
#include "solar.h"
struct DeviceSettings{
 uint8_t modeMask=MODE_CAP_ALL,vaneConfig=2,nightCeilPct=20;
 float heatingThreshold=20,coolingThreshold=25,latitude=NAN,longitude=NAN;
 int16_t nightDuskOffMin=0,nightDawnOffMin=0;
 bool useFahrenheit=false;
 char deviceName[32]="Test";
};
struct Settings{DeviceSettings s;int saves=0;DeviceSettings& get(){return s;}void save(){++saves;}}settings;
namespace WifiManager{bool isConnected(){return true;}bool hasCredentials(){return true;}}
struct Recovery{bool isAPActive(){return false;}bool wifiChangeFailed(){return false;}}wifiRecovery;
static uint32_t time_sync_epoch(){return 0;}
static CN105Controller* s_ctrl;
''' + function(strings, "modeToCapBit") + "\n" + function(strings, "mode_mask_allows")
AUTO += "\n" + adapter[adapter.index("static uint8_t mode_to_sl2"):adapter.index("/* A bonded dial reported")]
AUTO += "\n" + function(adapter, "h_get_caps")
AUTO += "\n" + block(thermo, thermo.index("struct AutoTarget")) + ";\n" + function(thermo, "resolveAutoTarget")
AUTO += "\nstatic void run_auto_sync(CN105Controller& cn105){const CN105State s=cn105.getEffectiveState();\n"
AUTO += block(thermo, thermo.index("    if (s.power && s.mode == CN105_MODE_AUTO)")) + "\n}\n" + PUMP

SOURCES = {"cn105_polling": CN105 + PUMP, "link_source": LINK, "ble_handoff": BLE, "auto_band": AUTO}
failed = False
with tempfile.TemporaryDirectory(prefix="cn105-native-delivery-") as directory:
    build = Path(directory)
    for name, source in SOURCES.items():
        if len(sys.argv) > 1 and name not in sys.argv[1:]:
            continue
        path = build / (name + ".cpp")
        path.write_text(source + "\n" + (HERE / ("test_" + name + ".cpp")).read_text())
        exe = build / name
        subprocess.run(["g++", "-std=c++17", "-DUNIT_TEST", "-I" + str(MAIN), str(path), *([str(MAIN / "solar.cpp")] if name == "auto_band" else []), "-o", str(exe)], check=True)
        cases = re.findall(r'CASE\("([^"]+)"\)', (HERE / ("test_" + name + ".cpp")).read_text())
        for case in cases:
            result = subprocess.run([str(exe), case])
            print(f"{name}/{case}: {'PASS' if result.returncode == 0 else 'FAIL'}", flush=True)
            failed |= result.returncode != 0
sys.exit(1 if failed else 0)
