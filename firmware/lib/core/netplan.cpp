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

}  // namespace core
