#include "netplan.h"

namespace core {

uint32_t pollInterval(uint32_t msSinceLastKey) {
    return msSinceLastKey < kActiveWindowMs ? kPollActiveMs : kPollIdleMs;
}

bool shouldPoll(uint32_t msSinceLastPoll, uint32_t msSinceLastKey) {
    return msSinceLastPoll >= pollInterval(msSinceLastKey);
}

uint8_t backlightFor(uint32_t msSinceLastKey) {
    if (msSinceLastKey >= kScreenOffAfterMs) return 0;
    if (msSinceLastKey >= kDimAfterMs) return kBrightDim;
    return kBrightFull;
}

bool shouldSleep(uint32_t msSinceLastKey, bool busy) {
    // Never sleep on top of an in-flight request: the reply would be lost and
    // the user would see their press silently undone on the next wake.
    if (busy) return false;
    return msSinceLastKey >= kSleepAfterMs;
}

uint32_t backoffDelay(int consecutiveFailures) {
    if (consecutiveFailures <= 0) return 0;
    uint32_t d = kBackoffStartMs;
    for (int i = 1; i < consecutiveFailures && d < kBackoffMaxMs; ++i) {
        d *= 2;
    }
    return d > kBackoffMaxMs ? kBackoffMaxMs : d;
}

bool apHintUsable(const ApHint& h, uint32_t nowEpoch) {
    if (!h.valid || h.channel == 0 || h.channel > 14) return false;
    bool anyByte = false;
    for (uint8_t b : h.bssid) if (b) { anyByte = true; break; }
    if (!anyByte) return false;
    if (h.savedAtEpoch == 0) return true;      // no clock yet: trust it once
    if (nowEpoch < h.savedAtEpoch) return false;
    return (nowEpoch - h.savedAtEpoch) < kApHintTtlS;
}


uint32_t configFingerprint(const char* ssid, const char* pass,
                           const char* host, uint16_t port,
                           const char* token) {
    // FNV-1a. Each string is hashed *including* its terminating NUL, which
    // acts as the field separator — without it "ab","c" and "a","bc" would
    // collide and a changed config could go unseeded.
    uint32_t h = 2166136261u;
    const auto mix = [&h](const char* s) {
        do {
            h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
        } while (*s++);
    };
    mix(ssid);
    mix(pass);
    mix(host);
    h = (h ^ static_cast<uint8_t>(port & 0xFF)) * 16777619u;
    h = (h ^ static_cast<uint8_t>(port >> 8)) * 16777619u;
    mix(token);
    // NVS yields 0 for a key that was never written, so 0 already means
    // "never seeded". A fingerprint of 0 would mean the same thing, and the
    // device would silently stop applying changed credentials — precisely the
    // defect this fingerprint exists to prevent. Reserve the sentinel.
    return h ? h : 1u;
}

}  // namespace core
