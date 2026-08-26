// Wi-Fi and the gateway client, on their own FreeRTOS task.
//
// The whole point: nothing here ever blocks the key handler. The UI task posts
// a request and carries on drawing; the reply arrives later and is merged.
// A remote that stops responding while the receiver is slow feels broken even
// when it is working perfectly.
#pragma once

#include <cstddef>
#include <cstdint>

#include "hw_store.h"
#include "netplan.h"

namespace net {

enum class LinkState : uint8_t { Off, Connecting, Online, Failed };

struct Status {
    LinkState link = LinkState::Off;
    int rssi = 0;
    uint32_t connectMs = 0;      // how long the last association took
    bool usedFastPath = false;   // reconnected from the stored BSSID hint
    int failures = 0;
};

// Starts the worker task. Safe to call once from setup().
void begin(const store::Config& cfg);

// Ask for a fresh snapshot. Non-blocking; returns false if one is in flight.
bool requestDash();

// Queue a write action. `body` is JSON. Returns a token used to match the
// reply, or 0 if the queue is full. Never blocks.
uint32_t requestAction(const char* body, uint32_t overlayToken);

// Poll for results from the UI task.
struct Result {
    bool isDash = false;
    bool ok = false;
    uint32_t overlayToken = 0;   // which optimistic claim this settles
    const char* body = nullptr;  // valid until the next takeResult()
    size_t len = 0;
    int status = 0;              // HTTP status, 0 on transport failure
};
bool takeResult(Result& out);

Status status();
bool busy();                     // a request is in flight

// Cleanly park the radio before deep sleep.
void prepareForSleep();

}  // namespace net
