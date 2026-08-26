#include "ir_nec.h"

namespace ir {

uint32_t necFrame(uint16_t address, uint8_t command) {
    const uint8_t addrLow = static_cast<uint8_t>(address & 0xFF);
    const uint8_t addrHigh = static_cast<uint8_t>((address >> 8) & 0xFF);
    const uint8_t cmdInv = static_cast<uint8_t>(~command);
    // Byte 0 goes out first, so it sits in the low bits.
    return static_cast<uint32_t>(addrLow)
         | (static_cast<uint32_t>(addrHigh) << 8)
         | (static_cast<uint32_t>(command) << 16)
         | (static_cast<uint32_t>(cmdInv) << 24);
}

bool necBit(uint32_t frame, int index) {
    if (index < 0 || index > 31) return false;
    return (frame >> index) & 1u;
}

}  // namespace ir
