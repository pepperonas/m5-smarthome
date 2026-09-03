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

    // Diagnostics. A device with no cable and no console has to be able to
    // say why it is not working, or the only tool left is guesswork.
    char ip[16] = {0};
    char url[64] = {0};          // what it last tried to fetch
    int lastStatus = 0;          // HTTP code, 0 = transport failure
    uint16_t lastBytes = 0;
    uint32_t requests = 0;
    uint32_t failed = 0;
    char lastError[40] = {0};
    uint32_t freeHeap = 0;
    uint32_t stackHighWater = 0; // words left on the worker stack

    // Wi-Fi is down and the worker is holding off reconnect attempts until
    // this uptime, per core::backoffDelay. Snapshot polls fail fast meanwhile
    // (so the device can still fall asleep); presses always try.
    uint32_t retryAtMs = 0;

    // The configured target, so the diagnostics screen can show where the
    // next request WILL go. A stale or empty host in NVS is otherwise
    // invisible: it produces no URL, no HTTP status and no error — just a
    // device that never fetches anything.
    char cfgHost[40] = {0};
    uint16_t cfgPort = 0;
    bool haveToken = false;
};

// Starts the worker task. Safe to call once from setup().
void begin(const store::Config& cfg);

// Ask for a fresh snapshot. Non-blocking. At most one snapshot request waits
// in the queue at a time: a second one is folded into it, so a poller that
// keeps asking during an outage cannot fill the queue and crowd out presses.
// Returns true when a request is pending afterwards (queued now or already).
bool requestDash();

// Queue a write action. `body` is JSON. Returns false if the queue is full —
// the caller must roll the optimistic overlay back then, or the screen keeps
// showing a change that was never sent. Never blocks.
bool requestAction(const char* body, uint32_t overlayToken);

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
