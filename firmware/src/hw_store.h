// Persistence: credentials in NVS, fast-wake hints and the last snapshot in
// RTC memory.
//
// Nothing secret is ever compiled in. Wi-Fi password and gateway token are
// typed on the device (it has a keyboard) and live in NVS only, so the build
// carries no secret and changing networks needs no toolchain.
#pragma once

#include <cstddef>
#include <cstdint>

#include "netplan.h"

namespace store {

struct Config {
    char ssid[33] = {0};
    char pass[65] = {0};
    char host[40] = {0};        // gateway host or IP
    uint16_t port = 5010;
    char token[64] = {0};
    bool valid = false;
};

bool load(Config& out);
bool save(const Config& cfg);
void erase();

// --- RTC memory: survives deep sleep, lost on power cycle ----------------

// The remembered access point. Reconnecting with a known BSSID and channel
// skips the scan, which is the single biggest win on wake-to-usable time.
bool loadApHint(core::ApHint& out);
void saveApHint(const core::ApHint& hint);
void clearApHint();

// The last snapshot, so a wake can draw something in milliseconds instead of
// showing a spinner while Wi-Fi negotiates.
bool loadSnapshot(char* buf, size_t cap, size_t& len);
void saveSnapshot(const char* json, size_t len);

// Wake counter, purely for the diagnostics screen.
uint32_t bootCount();
void bumpBootCount();

}  // namespace store
