#include "optimistic.h"

namespace core {
namespace {
bool alive(const Overlay& o, uint32_t nowMs) {
    // Comparing against a wrapped millis() would resurrect stale claims; the
    // subtraction below is wrap-safe for the ~49-day counter.
    return o.active && (int32_t)(o.expiresAt - nowMs) > 0;
}
}  // namespace

uint32_t OverlayStore::claim(Field f, int key, int value, uint32_t nowMs,
                             bool viaIr) {
    Overlay* slot = nullptr;
    // Replacing the same field keeps repeated presses from filling the table:
    // holding "+" should be one moving claim, not eight stacked ones.
    for (Overlay& o : slots_) {
        if (o.active && o.field == f && o.key == key) { slot = &o; break; }
    }
    if (!slot) {
        for (Overlay& o : slots_) {
            if (!alive(o, nowMs)) { slot = &o; break; }
        }
    }
    if (!slot) slot = &slots_[0];        // oldest wins; never drop the press

    slot->field = f;
    slot->key = key;
    slot->value = value;
    slot->viaIr = viaIr;
    slot->expiresAt = nowMs + (viaIr ? kUnconfirmedMs : kOverlayTtlMs);
    slot->active = true;
    slot->token = nextToken_++;
    if (nextToken_ == 0) nextToken_ = 1;
    return slot->token;
}

void OverlayStore::reject(uint32_t token) {
    if (token == 0) return;
    for (Overlay& o : slots_) {
        if (o.active && o.token == token) { o.active = false; return; }
    }
}

void OverlayStore::expire(uint32_t nowMs) {
    for (Overlay& o : slots_) {
        if (o.active && !alive(o, nowMs)) o.active = false;
    }
}

void OverlayStore::clear() {
    for (Overlay& o : slots_) o.active = false;
}

int OverlayStore::activeCount(uint32_t nowMs) const {
    int n = 0;
    for (const Overlay& o : slots_) if (alive(o, nowMs)) ++n;
    return n;
}

bool OverlayStore::hasUnconfirmed(uint32_t nowMs) const {
    for (const Overlay& o : slots_) {
        if (alive(o, nowMs) && o.viaIr) return true;
    }
    return false;
}

void OverlayStore::apply(Dash& d, uint32_t nowMs) const {
    for (const Overlay& o : slots_) {
        if (!alive(o, nowMs)) continue;
        switch (o.field) {
            case Field::RoomOn:
                if (Room* r = findRoom(d, o.key)) r->on = o.value != 0;
                break;
            case Field::RoomBri:
                if (Room* r = findRoom(d, o.key)) {
                    r->bri = static_cast<uint8_t>(o.value);
                    // Hue turns a dark group on when you set brightness, so
                    // showing it still dark would contradict what happens.
                    r->on = true;
                }
                break;
            case Field::LwOn:    d.lw.on = o.value != 0; break;
            case Field::LwBri:   d.lw.bri = static_cast<uint8_t>(o.value); break;
            case Field::YamOn:   d.yam.on = o.value != 0; break;
            case Field::YamRaw:
                d.yam.raw = o.value;
                d.yam.db = o.value / 10.0f;
                break;
            case Field::YamMute: d.yam.mute = o.value != 0; break;
            case Field::TfOn:    d.tf.on = o.value != 0; break;
            case Field::TfVol:   d.tf.volume = o.value; break;
            case Field::TfMute:  d.tf.mute = o.value != 0; break;
            case Field::DiscoOn: d.disco.on = o.value != 0; break;
            case Field::FogOn:   d.fog.on = o.value != 0; break;
        }
    }
    // Recount lit rooms, otherwise the header contradicts the tiles.
    int lit = 0;
    for (int i = 0; i < d.hue.count; ++i) if (d.hue.rooms[i].on) ++lit;
    d.hue.litCount = lit;
}

void OverlayStore::settleWith(const Dash& d, uint32_t nowMs) {
    for (Overlay& o : slots_) {
        if (!alive(o, nowMs)) continue;
        // An IR claim is never settled by the network agreeing once — the
        // Teufel state on the Pi is itself an estimate, so agreement proves
        // nothing. Let those run out on their own clock.
        if (o.viaIr) continue;

        bool agrees = false;
        switch (o.field) {
            case Field::RoomOn:
                if (const Room* r = findRoom(d, o.key))
                    agrees = r->on == (o.value != 0);
                break;
            case Field::RoomBri:
                if (const Room* r = findRoom(d, o.key))
                    agrees = r->bri == static_cast<uint8_t>(o.value);
                break;
            case Field::LwOn:    agrees = d.lw.on == (o.value != 0); break;
            case Field::LwBri:   agrees = d.lw.bri == (uint8_t)o.value; break;
            case Field::YamOn:   agrees = d.yam.on == (o.value != 0); break;
            case Field::YamRaw:  agrees = d.yam.raw == o.value; break;
            case Field::YamMute: agrees = d.yam.mute == (o.value != 0); break;
            case Field::TfOn:    agrees = d.tf.on == (o.value != 0); break;
            case Field::TfVol:   agrees = d.tf.volume == o.value; break;
            case Field::TfMute:  agrees = d.tf.mute == (o.value != 0); break;
            case Field::DiscoOn: agrees = d.disco.on == (o.value != 0); break;
            case Field::FogOn:   agrees = d.fog.on == (o.value != 0); break;
        }
        if (agrees) o.active = false;    // reality caught up; stop overriding
    }
}

}  // namespace core
