#include "auto_band.h"
#include "settings.h"
#include "logging.h"

#include <algorithm>
#include <cmath>

static const char *TAG = "auto_band";

// externalAutoChanges() value already acted on. Main task only.
static uint32_t s_seenExternal = 0;

bool AutoBand::deviceOwns() {
    return !settings.get().autoOwnerUnit;
}

void AutoBand::claimDevice() {
    if (!settings.get().autoOwnerUnit) return;
    settings.get().autoOwnerUnit = false;
    settings.save();
    LOG_INFO("[AutoBand] Range control taken back by this device");
}

void AutoBand::importFromUnit(float setpointC) {
    const float s = roundf(setpointC * 2.0f) / 2.0f;
    const float heat = std::clamp(s - 1.0f, CN105_TEMP_MIN, CN105_TEMP_MAX - AUTO_BAND_MIN_GAP_C);
    const float cool = heat + AUTO_BAND_MIN_GAP_C;
    settings.get().heatingThreshold = heat;
    settings.get().coolingThreshold = cool;
    settings.get().autoOwnerUnit = true;
    settings.save();
    LOG_INFO("[AutoBand] Auto setpoint %.1f%sC set at the heat pump's controller — range control handed to the unit, band %.1f–%.1f",
             s, "\xC2\xB0", heat, cool);
}

void AutoBand::loop(CN105Controller &cn105) {
    const uint32_t n = cn105.externalAutoChanges();
    if (n == s_seenExternal) return;
    s_seenExternal = n;
    // Confirmed value, not the effective one: nothing of ours is in flight
    // when a change counts as external.
    importFromUnit(cn105.getState().targetTemp);
}
