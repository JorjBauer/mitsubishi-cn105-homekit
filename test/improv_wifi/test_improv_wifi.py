#!/usr/bin/env python3
"""Provisioning must wait for WifiManager's persisted trial verdict."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'main/improv_serial.cpp').read_text()
handler = re.search(r'^static void handle_wifi_settings\(.*?^}', source, re.M | re.S)[0]
helper = re.search(r'^static bool wifi_settings_committed\(.*?^}', source, re.M | re.S)
prefix = r'''
#include "wifi_manager.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
constexpr uint8_t STATE_AUTHORIZED=2, STATE_PROVISIONING=3, STATE_PROVISIONED=4;
constexpr uint8_t ERR_INVALID_RPC=1, ERR_UNABLE_TO_CONNECT=3, PKT_RPC_RESULT=4, CMD_WIFI_SETTINGS=1;
static uint8_t s_cur_state=STATE_AUTHORIZED;
static bool s_stop_requested=false;
static int ticks=0, saves=0, errors=0, provisioned_at=-1, success_at=-1, connect_calls=0;
static bool connect_ok=true, connected=true, joining=false;
static WifiManager::WifiTrialState trial=WifiManager::WIFI_TRIAL_TESTING;
struct Recovery {
    void setChangePending(bool) {}
    void noteReprovision() {}
} wifiRecovery;
static void silent_log(const char *, ...) {}
#define LOG_INFO(...) silent_log(__VA_ARGS__)
#define LOG_WARN(...) silent_log(__VA_ARGS__)
#define pdMS_TO_TICKS(n) (n)
static void vTaskDelay(int) {
    ++ticks;
    if (success_at == ticks) trial=WifiManager::WIFI_TRIAL_SUCCESS;
}
namespace WifiManager {
    bool connect(const char *, const char *) { ++connect_calls; return connect_ok; }
    bool isConnected() { return connected; }
    bool isJoinPending() { return joining; }
    WifiTrialState getTrialState() { return trial; }
    bool saveCredentials(const char *, const char *) { ++saves; return false; }
    void getIP(char *out, size_t) { strcpy(out,"192.0.2.1"); }
}
static void send_error(uint8_t) { ++errors; }
static void send_current_state(uint8_t state) {
    if (state==STATE_PROVISIONED) provisioned_at=ticks;
}
static void write_frame(uint8_t,const uint8_t *,uint8_t) {}
'''
checks = r'''
int main(int argc,char **argv) {
    assert(argc==2); std::string name=argv[1];
    std::vector<uint8_t> payload={4,'H','o','m','e',8,'p','a','s','s','w','o','r','d'};
    if (name=="persistence_pending") {
        handle_wifi_settings(payload.data(),payload.size());
        assert(provisioned_at==-1 && errors==1 && saves==0);
    } else if (name=="wait_for_commit") {
        success_at=4;
        handle_wifi_settings(payload.data(),payload.size());
        assert(provisioned_at==4 && errors==0 && saves==0);
    } else if (name=="rejected_connect") {
        connect_ok=false; trial=WifiManager::WIFI_TRIAL_IDLE;
        handle_wifi_settings(payload.data(),payload.size());
        assert(provisioned_at==-1 && errors==1 && ticks==0 && saves==0);
    } else if (name=="stored_credentials") {
        trial=WifiManager::WIFI_TRIAL_IDLE;
        handle_wifi_settings(payload.data(),payload.size());
        assert(provisioned_at==1 && errors==0 && saves==0);
    } else if (name=="rolled_back") {
        trial=WifiManager::WIFI_TRIAL_FAILED;
        handle_wifi_settings(payload.data(),payload.size());
        assert(provisioned_at==-1 && errors==1 && saves==0);
    } else if (name=="join_pending" || name=="disconnected") {
        trial=WifiManager::WIFI_TRIAL_SUCCESS;
        joining=name=="join_pending";
        connected=name!="disconnected";
        handle_wifi_settings(payload.data(),payload.size());
        assert(provisioned_at==-1 && errors==1 && saves==0);
    } else { assert(false); }
    printf("PASS: %s\n",name.c_str());
}
'''
with tempfile.TemporaryDirectory(prefix='improv-wifi-') as tmp:
    unit, exe = Path(tmp)/'test.cpp', Path(tmp)/'test'
    unit.write_text(prefix + (helper[0] if helper else '') + handler + checks)
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-variable',
                    '-I'+str(ROOT/'main'),str(unit),'-o',str(exe)],check=True)
    cases=['persistence_pending','wait_for_commit','rejected_connect','stored_credentials',
           'rolled_back','join_pending','disconnected']
    codes=[subprocess.run([str(exe),case]).returncode for case in cases]
    raise SystemExit(any(codes))
