#include "hw_store.h"

#include <Preferences.h>
#include <cstring>

namespace store {
namespace {

constexpr const char* kNamespace = "m5sh";

// RTC_DATA_ATTR survives deep sleep but not a power cycle, which is exactly
// the lifetime these caches should have.
RTC_DATA_ATTR core::ApHint g_apHint;
RTC_DATA_ATTR char g_snapshot[1400];
RTC_DATA_ATTR uint16_t g_snapshotLen = 0;
RTC_DATA_ATTR uint32_t g_bootCount = 0;
RTC_DATA_ATTR uint32_t g_magic = 0;

constexpr uint32_t kMagic = 0x4D355348;   // "M5SH"

bool rtcReady() { return g_magic == kMagic; }

void markReady() { g_magic = kMagic; }

}  // namespace

bool load(Config& out) {
    Preferences p;
    if (!p.begin(kNamespace, /*readOnly=*/true)) return false;
    Config c;
    p.getString("ssid", c.ssid, sizeof(c.ssid));
    p.getString("pass", c.pass, sizeof(c.pass));
    p.getString("host", c.host, sizeof(c.host));
    p.getString("token", c.token, sizeof(c.token));
    c.port = p.getUShort("port", 5010);
    p.end();
    // A gateway without a token is useless and a Wi-Fi without an SSID is
    // meaningless. The host may be blank: mDNS can supply it, and asking the
    // user for an IP address they would have to look up is a bad first run.
    c.valid = c.ssid[0] != 0 && c.token[0] != 0;
    out = c;
    return c.valid;
}

bool save(const Config& cfg) {
    Preferences p;
    if (!p.begin(kNamespace, /*readOnly=*/false)) return false;
    p.putString("ssid", cfg.ssid);
    p.putString("pass", cfg.pass);
    p.putString("host", cfg.host);
    p.putString("token", cfg.token);
    p.putUShort("port", cfg.port);
    p.end();
    return true;
}

void erase() {
    Preferences p;
    if (p.begin(kNamespace, false)) {
        p.clear();
        p.end();
    }
    clearApHint();
}

bool loadApHint(core::ApHint& out) {
    if (!rtcReady() || !g_apHint.valid) return false;
    out = g_apHint;
    return true;
}

void saveApHint(const core::ApHint& hint) {
    g_apHint = hint;
    markReady();
}

void clearApHint() {
    g_apHint = core::ApHint();
    g_apHint.valid = false;
}

bool loadSnapshot(char* buf, size_t cap, size_t& len) {
    if (!rtcReady() || g_snapshotLen == 0 || g_snapshotLen >= cap) return false;
    memcpy(buf, g_snapshot, g_snapshotLen);
    buf[g_snapshotLen] = 0;
    len = g_snapshotLen;
    return true;
}

void saveSnapshot(const char* json, size_t len) {
    if (!json || len == 0 || len >= sizeof(g_snapshot)) return;
    memcpy(g_snapshot, json, len);
    g_snapshotLen = static_cast<uint16_t>(len);
    markReady();
}

uint32_t bootCount() { return rtcReady() ? g_bootCount : 0; }

void bumpBootCount() {
    if (!rtcReady()) g_bootCount = 0;
    ++g_bootCount;
    markReady();
}

}  // namespace store
