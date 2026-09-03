// The credential-reset gesture, as a pure state machine.
//
// The first version checked the keyboard once, right after begin(), for a key
// held through power-on. On the Cardputer ADV that can never fire: the
// TCA8418 reader flushes its event FIFO in begin(), and a key that was
// already down produces no further event, so isPressed() reads 0 however
// long you hold it. The gesture is now "hold any key for two seconds within
// the first three seconds after a cold boot", polled from the main loop, so
// a press that arrives after the flush counts.
#pragma once

#include <cstdint>

namespace core {

constexpr uint32_t kResetWindowMs = 3000;   // after a cold boot
constexpr uint32_t kResetHoldMs = 2000;     // continuous hold

class ResetGesture {
public:
    enum class State : uint8_t { Idle, Holding, Fire };

    // `coldBoot` false (a wake from deep sleep) disables the gesture: the key
    // that woke the device must not be able to wipe it.
    void begin(bool coldBoot, uint32_t nowMs);

    // Call every loop with whether any key is currently down. Returns Fire
    // exactly once, when the hold has lasted kResetHoldMs.
    State feed(bool pressed, uint32_t nowMs);

    // Milliseconds of hold remaining, for the on-screen countdown; 0 if idle.
    uint32_t remainingMs(uint32_t nowMs) const;

private:
    bool armed_ = false;
    uint32_t deadlineMs_ = 0;      // end of the window
    bool holding_ = false;
    uint32_t holdSinceMs_ = 0;
    bool fired_ = false;
};

}  // namespace core
