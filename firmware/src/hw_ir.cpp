#include "hw_ir.h"

#include <Arduino.h>

#include "board.h"
#include "ir_nec.h"
#include "ir_teufel.h"

namespace hwir {
namespace {

constexpr int kLedcChannel = 0;
constexpr int kLedcResolution = 8;
// 33 % duty is the usual compromise: enough energy for range, low enough that
// the LED and the regulator survive long bursts.
constexpr uint8_t kDuty = 85;

bool g_ready = false;

inline void carrierOn() { ledcWrite(kLedcChannel, kDuty); }
inline void carrierOff() { ledcWrite(kLedcChannel, 0); }

// delayMicroseconds busy-waits, which is what this needs: NEC works in 560 us
// units and a vTaskDelay would quantise to the 1 ms tick and destroy the frame.
inline void mark(uint16_t us) { carrierOn(); delayMicroseconds(us); }
inline void space(uint16_t us) { carrierOff(); delayMicroseconds(us); }

}  // namespace

void begin() {
    if (g_ready) return;
    ledcSetup(kLedcChannel, ir::kNecCarrierHz, kLedcResolution);
    ledcAttachPin(board::kIrTxPin, kLedcChannel);
    carrierOff();
    g_ready = true;
}

void sendNec(uint16_t address, uint8_t command, uint8_t repeats) {
    if (!g_ready) begin();
    const uint32_t frame = ir::necFrame(address, command);

    for (uint8_t r = 0; r <= repeats; ++r) {
        const uint32_t started = millis();
        mark(ir::kNecHeaderMark);
        space(ir::kNecHeaderSpace);
        for (int i = 0; i < 32; ++i) {
            mark(ir::kNecBitMark);
            space(ir::necSpaceFor(ir::necBit(frame, i)));
        }
        mark(ir::kNecBitMark);          // stop bit
        carrierOff();
        if (r < repeats) {
            // NEC repeats are measured from the start of the previous frame.
            const uint32_t elapsed = millis() - started;
            if (elapsed < ir::kNecFrameMs) delay(ir::kNecFrameMs - elapsed);
        }
    }
}

void sendTeufel(uint8_t command, uint8_t repeats) {
    sendNec(ir::kTeufelAddress, command, repeats);
}

}  // namespace hwir
