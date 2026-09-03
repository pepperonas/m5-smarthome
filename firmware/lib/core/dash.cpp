#include "dash.h"

#include <ArduinoJson.h>

namespace core {
namespace {

void copyStr(char* dst, size_t cap, const char* src) {
    if (!src) { dst[0] = 0; return; }
    size_t n = strnlen(src, cap - 1);
    memcpy(dst, src, n);
    dst[n] = 0;
    // Everything copied here ends up on the panel, whose font is ASCII-only.
    foldForDisplay(dst);
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
    // millis() restarts at zero after deep sleep. A snapshot restored from
    // RTC memory is flagged, and one that still carries its pre-sleep stamp
    // looks like it came from the future. Both are "old", never "fresh" —
    // guessing fresh is the dangerous direction, and "now minus receivedAt"
    // after a restart is uptime, not age.
    if (d.restoredFromSleep || nowMs < d.receivedAtMs) return kStaleAfterMs;
    return nowMs - d.receivedAtMs;
}

void markRestoredFromSleep(Dash& d) {
    d.restoredFromSleep = true;
}

void ageLabel(const Dash& d, uint32_t nowMs, char* out, size_t cap) {
    if (!d.valid) {
        snprintf(out, cap, "warte auf Daten");
    } else if (d.restoredFromSleep) {
        // The clock cannot say how old this is; "Stand 12s alt" would be
        // uptime dressed up as age. Say what is actually known.
        snprintf(out, cap, "Stand: vor dem Schlafen");
    } else if (isStale(d, nowMs)) {
        snprintf(out, cap, "Stand %lus alt",
                 (unsigned long)(ageMs(d, nowMs) / 1000));
    } else {
        out[0] = 0;
    }
}

void foldForDisplay(char* s) {
    unsigned char* r = reinterpret_cast<unsigned char*>(s);
    unsigned char* w = r;
    while (*r) {
        const unsigned char c = *r;
        if (c < 0x80) {
            *w++ = *r++;
            continue;
        }
        // Length of the UTF-8 sequence from its lead byte; a stray
        // continuation byte counts as one so nothing is ever skipped past.
        int len = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3
                : (c & 0xF8) == 0xF0 ? 4 : 1;
        const char* rep = "?";
        if (c == 0xC3 && r[1]) {
            switch (r[1]) {
                case 0xA4: rep = "ae"; break;
                case 0x84: rep = "Ae"; break;
                case 0xB6: rep = "oe"; break;
                case 0x96: rep = "Oe"; break;
                case 0xBC: rep = "ue"; break;
                case 0x9C: rep = "Ue"; break;
                case 0x9F: rep = "ss"; break;
                default: break;
            }
        }
        for (int i = 0; i < len && *r; ++i) ++r;
        // Every replacement is at most two bytes and replaces at least two,
        // so the write cursor never overtakes the read cursor.
        for (const char* q = rep; *q; ++q) *w++ = static_cast<unsigned char>(*q);
    }
    *w = 0;
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
