// Infrared transmitter.
//
// The second control path, and the only one that works while the Pi reboots
// or the Wi-Fi is gone — and the only one that acts with no round trip at all.
// It is also blind: nothing acknowledges an IR burst, which is why every
// caller marks the resulting state unconfirmed.
#pragma once

#include <cstdint>

namespace hwir {

void begin();

// Send one NEC frame. Blocking for ~68 ms plus repeats; call it from the UI
// task between frames, never from an ISR.
void sendNec(uint16_t address, uint8_t command, uint8_t repeats = 0);

// Convenience for the house's Teufel amplifier.
void sendTeufel(uint8_t command, uint8_t repeats = 0);

}  // namespace hwir
