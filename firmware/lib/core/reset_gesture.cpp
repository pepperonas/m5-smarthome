#include "reset_gesture.h"

namespace core {

void ResetGesture::begin(bool coldBoot, uint32_t nowMs) {
    armed_ = coldBoot;
    deadlineMs_ = nowMs + kResetWindowMs;
    holding_ = false;
    fired_ = false;
}

ResetGesture::State ResetGesture::feed(bool pressed, uint32_t nowMs) {
    if (!armed_ || fired_) return State::Idle;
    if (!holding_) {
        // The hold must START inside the window; it may run past its end.
        if ((int32_t)(nowMs - deadlineMs_) >= 0) {
            armed_ = false;
            return State::Idle;
        }
        if (!pressed) return State::Idle;
        holding_ = true;
        holdSinceMs_ = nowMs;
        return State::Holding;
    }
    if (!pressed) {
        // Released early: a normal key press, and the window stays open.
        holding_ = false;
        return State::Idle;
    }
    if (nowMs - holdSinceMs_ >= kResetHoldMs) {
        fired_ = true;
        armed_ = false;
        return State::Fire;
    }
    return State::Holding;
}

uint32_t ResetGesture::remainingMs(uint32_t nowMs) const {
    if (!holding_ || fired_) return 0;
    const uint32_t held = nowMs - holdSinceMs_;
    return held >= kResetHoldMs ? 0 : kResetHoldMs - held;
}

}  // namespace core
