// The remote's model of the house.
//
// Fed by the gateway's /api/dash snapshot. Pure: no display, no sockets, no
// clock of its own — every function takes the time it should reason about.
// That is what lets `pio test -e native` cover it on a laptop.
#pragma once

#include <cstdint>
#include <cstring>

namespace core {

constexpr int kMaxRooms = 8;
constexpr int kNameLen = 16;

// How stale a snapshot may get before the UI stops presenting it as current.
// Not a hard cut-off: stale data is still shown, just marked. Blanking it
// would turn every Wi-Fi hiccup into a device that looks broken.
constexpr uint32_t kStaleAfterMs = 8000;

struct Room {
    int id = 0;
    char name[kNameLen] = {0};
    bool on = false;
    uint8_t bri = 0;
};

struct Hue {
    Room rooms[kMaxRooms];
    int count = 0;
    int litCount = 0;
};

struct Lichtwerk {
    bool on = false;
    uint8_t bri = 0;
    char effect[12] = {0};
    bool warnOwned = false;     // strip-warn holds the strip; we must not paint
};

struct Yamaha {
    bool on = false;
    int raw = 0;                // receiver units: -280 == -28.0 dB
    float db = 0.0f;
    bool mute = false;
    char input[16] = {0};
};

struct Teufel {
    bool on = false;
    int volume = 0;
    bool mute = false;
    char input[12] = {0};
    // Always true: the Pi estimates this state by toggling a flag after
    // firing IR. Nobody ever confirmed it. The UI must say so.
    bool estimated = true;
};

struct Fog {
    bool on = false;
    int tankPct = -1;           // -1 == unknown
    int tankMl = -1;
};

struct Disco {
    bool on = false;
    int bpm = 0;
    float spl = 0.0f;
    char mode[10] = {0};
};

struct Reading {
    float temp = 0.0f;
    int humidity = -1;
    bool valid = false;
    int ageSeconds = 0;         // >0 only when the gateway flagged it stale
};

struct Weather {
    float temp = 0.0f;
    int high = 0, low = 0;
    char icon[5] = {0};
    char desc[24] = {0};
    bool valid = false;
};

struct PiHealth {
    float cpu = 0.0f, temp = 0.0f, mem = 0.0f;
    bool valid = false;
};

// Which tiles the gateway could not refresh. Bit per source.
enum SourceBit : uint16_t {
    SRC_HUE   = 1 << 0,
    SRC_LW    = 1 << 1,
    SRC_YAM   = 1 << 2,
    SRC_TF    = 1 << 3,
    SRC_FOG   = 1 << 4,
    SRC_DISCO = 1 << 5,
    SRC_CLIMA = 1 << 6,
    SRC_WX    = 1 << 7,
    SRC_PI    = 1 << 8,
};

struct Dash {
    uint32_t serverTime = 0;
    Hue hue;
    Lichtwerk lw;
    Yamaha yam;
    Teufel tf;
    Fog fog;
    Disco disco;
    Reading indoor, outdoor;
    Weather wx;
    PiHealth pi;

    uint16_t missing = 0;       // source never delivered
    uint16_t stale = 0;         // gateway served a last-known value
    bool valid = false;         // have we ever had a snapshot at all?
    uint32_t receivedAtMs = 0;  // device uptime when this arrived

    bool sourceOk(SourceBit b) const { return !(missing & b); }
    bool sourceStale(SourceBit b) const { return (stale & b) != 0; }
};

// True when the whole snapshot is old enough that the UI should say so.
bool isStale(const Dash& d, uint32_t nowMs);

// Age in milliseconds, saturating at 0 for a snapshot from the future
// (which happens across a sleep/wake boundary when millis() restarts).
uint32_t ageMs(const Dash& d, uint32_t nowMs);

// Parse a gateway snapshot. Returns false and leaves `out` untouched if the
// payload is unusable — a bad reply must never destroy the last good state.
bool parseDash(const char* json, size_t len, Dash& out, uint32_t nowMs);

// Look up a room by gateway id; nullptr when unknown.
const Room* findRoom(const Dash& d, int id);
Room* findRoom(Dash& d, int id);

}  // namespace core
