// NEC infrared framing — the arithmetic half, with no timing in it.
//
// Written by hand rather than pulled from a library on purpose: the framing
// is thirty lines, it is the part that can be *wrong*, and having it pure
// means the bit pattern is checked on the host instead of guessed at by
// pointing a remote at a speaker and hoping.
#pragma once

#include <cstdint>

namespace ir {

// NEC pulse timings in microseconds. One "tick" is 560 us.
constexpr uint16_t kNecHeaderMark  = 9000;
constexpr uint16_t kNecHeaderSpace = 4500;
constexpr uint16_t kNecBitMark     = 560;
constexpr uint16_t kNecZeroSpace   = 560;
constexpr uint16_t kNecOneSpace    = 1690;
constexpr uint16_t kNecRepeatSpace = 2250;
constexpr uint16_t kNecCarrierHz   = 38000;

// Gap between repeated presses, measured from the start of the last frame.
constexpr uint16_t kNecFrameMs = 110;

// Build the 32-bit NEC payload for an extended-address device.
//
// Wire order is LSB-first: address low byte, address high byte, command,
// command complement. We return them packed so bit 0 of the result is the
// first bit on the wire — that is what a transmit loop wants, and it is the
// convention this file is tested against.
uint32_t necFrame(uint16_t address, uint8_t command);

// The n-th bit to put on the wire (0 = first). Total is 32.
bool necBit(uint32_t frame, int index);

// Space length for one bit, in microseconds.
inline uint16_t necSpaceFor(bool bit) {
    return bit ? kNecOneSpace : kNecZeroSpace;
}

}  // namespace ir
