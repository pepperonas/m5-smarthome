// Optimistic overlay: what the user just did, before the house confirms it.
//
// Rule this exists to enforce: a key press changes the screen *now*, and the
// request happens behind it. Waiting for a round trip before moving a pixel is
// what makes a remote feel broken even when it works.
//
// An overlay is a short-lived claim about one field. It wins over the snapshot
// until either a fresher snapshot agrees (nothing flickers, the values match)
// or it expires (the house never agreed, so stop pretending).
#pragma once

#include <cstdint>

#include "dash.h"

namespace core {

// How long a claim may override the snapshot. Long enough for a round trip
// plus the next poll, short enough that a wrong assumption corrects itself
// while the user is still looking at it.
constexpr uint32_t kOverlayTtlMs = 4000;

// After an IR press there is no confirmation at all — the LED fires into the
// room and nothing answers. The UI marks such state unconfirmed until a
// network poll independently agrees.
constexpr uint32_t kUnconfirmedMs = 10000;

enum class Field : uint8_t {
    RoomOn, RoomBri, LwOn, LwBri, YamOn, YamRaw, YamMute,
    TfOn, TfVol, TfMute, DiscoOn, FogOn,
    // Named values. These bypassed the overlay once, so an input change did
    // not move the screen until the next poll — and a second quick press
    // cycled from the stale value instead of the one just chosen.
    YamInput, TfInput, LwEffect, DiscoMode,
};

constexpr int kOverlayTextLen = 16;

struct Overlay {
    Field field = Field::RoomOn;
    int key = 0;                 // room id where the field needs one
    int value = 0;
    char text[kOverlayTextLen] = {0};   // for the named fields
    uint32_t expiresAt = 0;
    uint32_t token = 0;
    bool viaIr = false;          // fired by infrared: nobody will confirm it
    bool active = false;
};

constexpr int kMaxOverlays = 8;

class OverlayStore {
public:
    // Records a claim and returns its token (never 0).
    uint32_t claim(Field f, int key, int value, uint32_t nowMs,
                   bool viaIr = false);

    // Same, for the named fields (input, effect, mode).
    uint32_t claimText(Field f, const char* text, uint32_t nowMs,
                       bool viaIr = false);

    // The request failed: drop the claim so the screen tells the truth again.
    void reject(uint32_t token);

    // Drop everything that has outlived its ttl.
    void expire(uint32_t nowMs);

    // Paint active claims over a snapshot.
    void apply(Dash& d, uint32_t nowMs) const;

    // True while any claim is still unconfirmed by a network reading. Drives
    // the little "?" the UI shows next to a value it cannot vouch for.
    bool hasUnconfirmed(uint32_t nowMs) const;

    // A snapshot newer than a claim settles it, whichever way it went.
    void settleWith(const Dash& d, uint32_t nowMs);

    int activeCount(uint32_t nowMs) const;
    void clear();

private:
    Overlay slots_[kMaxOverlays];
    uint32_t nextToken_ = 1;
};

}  // namespace core
