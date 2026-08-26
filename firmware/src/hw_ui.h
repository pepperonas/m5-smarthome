// Drawing. A thin adapter over the pure UiState — it decides nothing.
#pragma once

#include <cstdint>

#include "dash.h"
#include "hw_net.h"
#include "ui_state.h"

namespace ui {

void begin();
void setBacklight(uint8_t level);

// One frame. `d` is expected to already carry the optimistic overlay.
void draw(const core::UiState& st, const core::Dash& d, uint32_t nowMs,
          const net::Status& link, int batteryPct, bool unconfirmed);

// First-run screen: type SSID, password, host and token on the keyboard.
// Returns true when the user completed it.
void drawSetup(const char* prompt, const char* value, bool masked, int step);

}  // namespace ui
