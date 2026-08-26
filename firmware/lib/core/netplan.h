// Timing policy: when to poll, when to dim, when to sleep, how to back off.
//
// Pure arithmetic over "how long since X". Kept out of the main loop so the
// battery-shaping decisions can be reasoned about — and tested — instead of
// being scattered as magic numbers among display calls.
#pragma once

#include <cstdint>

namespace core {

// --- polling --------------------------------------------------------------

// While the user is actually pressing keys the screen should feel live.
constexpr uint32_t kPollActiveMs = 1000;
// Awake but idle: still current, a quarter of the radio time.
constexpr uint32_t kPollIdleMs = 4000;
// "Recently active" window that separates the two.
constexpr uint32_t kActiveWindowMs = 15000;

uint32_t pollInterval(uint32_t msSinceLastKey);
bool shouldPoll(uint32_t msSinceLastPoll, uint32_t msSinceLastKey);

// --- display and sleep ----------------------------------------------------

constexpr uint8_t kBrightFull = 160;     // 0..255 backlight
constexpr uint8_t kBrightDim = 40;
constexpr uint32_t kDimAfterMs = 12000;
constexpr uint32_t kScreenOffAfterMs = 25000;
// The device is a remote, not a wall display: it goes back to sleep quickly.
constexpr uint32_t kSleepAfterMs = 30000;

uint8_t backlightFor(uint32_t msSinceLastKey);
bool shouldSleep(uint32_t msSinceLastKey, bool busy);

// --- Wi-Fi reconnection ---------------------------------------------------

// Losing Wi-Fi is a normal state, not an error: back off, keep the UI alive,
// keep showing the last known values marked stale.
constexpr uint32_t kBackoffStartMs = 500;
constexpr uint32_t kBackoffMaxMs = 30000;

uint32_t backoffDelay(int consecutiveFailures);

// --- fast wake ------------------------------------------------------------

// A remembered BSSID/channel skips the scan, which is the single biggest win
// on wake time. The hint is only trusted for a while: APs move channels, and
// a stale hint costs more than it saves.
constexpr uint32_t kApHintTtlS = 24 * 3600;

struct ApHint {
    uint8_t bssid[6] = {0};
    uint8_t channel = 0;
    uint32_t ip = 0;             // last DHCP lease, reused as a static guess
    uint32_t gw = 0;
    uint32_t mask = 0;
    uint32_t savedAtEpoch = 0;
    bool valid = false;
};

bool apHintUsable(const ApHint& h, uint32_t nowEpoch);

}  // namespace core
