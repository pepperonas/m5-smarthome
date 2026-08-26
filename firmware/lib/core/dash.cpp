#include "dash.h"

#include <ArduinoJson.h>

namespace core {
namespace {

void copyStr(char* dst, size_t cap, const char* src) {
    if (!src) { dst[0] = 0; return; }
    size_t n = strnlen(src, cap - 1);
    memcpy(dst, src, n);
    dst[n] = 0;
}

// The gateway names failed sources in "err" and last-known ones in "old".
// Both arrive as short string arrays; map them onto our bit field.
uint16_t bitFor(const char* key) {
    struct { const char* k; uint16_t b; } table[] = {
        {"hue", SRC_HUE}, {"lw", SRC_LW}, {"yam", SRC_YAM}, {"tf", SRC_TF},
        {"fog", SRC_FOG}, {"disco", SRC_DISCO}, {"clima", SRC_CLIMA},
        {"wx", SRC_WX}, {"pi", SRC_PI},
    };
    for (auto& e : table) {
        if (strcmp(e.k, key) == 0) return e.b;
    }
    return 0;
}

uint16_t bitsFrom(JsonArrayConst arr) {
    uint16_t bits = 0;
    for (JsonVariantConst v : arr) {
        const char* s = v.as<const char*>();
        if (s) bits |= bitFor(s);
    }
    return bits;
}

}  // namespace

uint32_t ageMs(const Dash& d, uint32_t nowMs) {
    if (!d.valid) return 0;
    // millis() restarts at zero after deep sleep, so a snapshot restored from
    // RTC memory looks like it came from the future. Treat that as "old",
    // never as "fresh" — guessing fresh is the dangerous direction.
    if (nowMs < d.receivedAtMs) return kStaleAfterMs;
    return nowMs - d.receivedAtMs;
}

bool isStale(const Dash& d, uint32_t nowMs) {
    if (!d.valid) return true;
    return ageMs(d, nowMs) >= kStaleAfterMs;
}

const Room* findRoom(const Dash& d, int id) {
    for (int i = 0; i < d.hue.count; ++i) {
        if (d.hue.rooms[i].id == id) return &d.hue.rooms[i];
    }
    return nullptr;
}

Room* findRoom(Dash& d, int id) {
    return const_cast<Room*>(findRoom(static_cast<const Dash&>(d), id));
}

bool parseDash(const char* json, size_t len, Dash& out, uint32_t nowMs) {
    if (!json || len == 0) return false;

    // Sized from the measured payload (721 B live, 1 KB budget) with room to
    // spare. ArduinoJson v7 grows on demand, so this is a starting hint.
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, len);
    if (err) return false;
    if (!doc["t"].is<uint32_t>() && !doc["t"].is<int>()) return false;

    Dash d;                       // build aside; only commit on success
    d.serverTime = doc["t"] | 0u;
    d.receivedAtMs = nowMs;
    d.valid = true;

    if (JsonArrayConst e = doc["err"]) d.missing = bitsFrom(e);
    if (JsonArrayConst o = doc["old"]) d.stale = bitsFrom(o);

    if (JsonObjectConst hue = doc["hue"]) {
        d.hue.litCount = hue["on"] | 0;
        JsonArrayConst g = hue["g"];
        for (JsonObjectConst r : g) {
            if (d.hue.count >= kMaxRooms) break;
            Room& room = d.hue.rooms[d.hue.count++];
            room.id = r["i"] | 0;
            copyStr(room.name, kNameLen, r["n"] | "");
            room.on = r["on"] | false;
            room.bri = static_cast<uint8_t>(r["b"] | 0);
        }
    }

    if (JsonObjectConst lw = doc["lw"]) {
        d.lw.on = lw["on"] | false;
        d.lw.bri = static_cast<uint8_t>(lw["b"] | 0);
        copyStr(d.lw.effect, sizeof(d.lw.effect), lw["fx"] | "");
        d.lw.warnOwned = lw["warn"] | false;
    }

    if (JsonObjectConst y = doc["yam"]) {
        d.yam.on = y["on"] | false;
        d.yam.raw = y["raw"] | 0;
        d.yam.db = y["vol"] | 0.0f;
        d.yam.mute = y["mute"] | false;
        copyStr(d.yam.input, sizeof(d.yam.input), y["in"] | "");
    }

    if (JsonObjectConst t = doc["tf"]) {
        d.tf.on = t["on"] | false;
        d.tf.volume = t["vol"] | 0;
        d.tf.mute = t["mute"] | false;
        copyStr(d.tf.input, sizeof(d.tf.input), t["in"] | "");
        d.tf.estimated = t["est"] | true;
    }

    if (JsonObjectConst f = doc["fog"]) {
        d.fog.on = f["on"] | false;
        d.fog.tankPct = f["tank"] | -1;
        d.fog.tankMl = f["ml"] | -1;
    }

    if (JsonObjectConst s = doc["disco"]) {
        d.disco.on = s["on"] | false;
        d.disco.bpm = s["bpm"] | 0;
        d.disco.spl = s["spl"] | 0.0f;
        copyStr(d.disco.mode, sizeof(d.disco.mode), s["mode"] | "");
    }

    if (JsonObjectConst c = doc["clima"]) {
        auto read = [](JsonObjectConst o, Reading& r) {
            if (!o) return;
            r.temp = o["t"] | 0.0f;
            r.humidity = o["h"] | -1;
            r.ageSeconds = o["old"] | 0;
            r.valid = true;
        };
        read(c["in"], d.indoor);
        read(c["out"], d.outdoor);
    }

    if (JsonObjectConst w = doc["wx"]) {
        d.wx.temp = w["t"] | 0.0f;
        d.wx.high = w["hi"] | 0;
        d.wx.low = w["lo"] | 0;
        copyStr(d.wx.icon, sizeof(d.wx.icon), w["ic"] | "");
        copyStr(d.wx.desc, sizeof(d.wx.desc), w["d"] | "");
        d.wx.valid = true;
    }

    if (JsonObjectConst p = doc["pi"]) {
        d.pi.cpu = p["cpu"] | 0.0f;
        d.pi.temp = p["tmp"] | 0.0f;
        d.pi.mem = p["mem"] | 0.0f;
        d.pi.valid = true;
    }

    out = d;
    return true;
}

}  // namespace core
