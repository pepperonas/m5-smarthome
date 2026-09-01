// M5 Smart-Home Remote — Cardputer firmware.
//
// Shape of the thing: a remote control, not a wall display. It sleeps, a key
// press wakes it, and it must be usable in well under a second. Three things
// buy that (see docs/ARCHITECTURE.md):
//
//   1. no Wi-Fi scan on wake — the BSSID, channel and last lease live in RTC
//      memory and the association is made directly;
//   2. the last snapshot is drawn from RTC memory immediately, marked stale,
//      and replaced when fresh data lands;
//   3. key presses are queued, never blocked on the radio.
//
// Everything deterministic lives in lib/core and is tested on the host.
// This file is the wiring.

#include <Arduino.h>
#include <M5Cardputer.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include <cstdio>
#include <cstring>

#include "board.h"
#include "dash.h"
#include "hw_ir.h"
#include "hw_net.h"
#include "hw_ota.h"
#include "hw_store.h"
#include "hw_ui.h"
#include "ir_teufel.h"
#include "keymap.h"
#include "netplan.h"
#include "optimistic.h"
#include "ui_state.h"

// A personal build may carry credentials so the device joins the network on
// first boot. The default environment never defines this, so the published
// binaries always ask on the device and contain no secret.
#if M5SH_LOCAL_SECRETS
#include "../secrets_local.h"
#endif

namespace {

core::Dash g_dash;
core::UiState g_ui;
core::OverlayStore g_overlays;
store::Config g_cfg;

uint32_t g_lastKeyMs = 0;
uint32_t g_lastPollMs = 0;
uint32_t g_lastDrawMs = 0;
uint8_t g_backlight = core::kBrightFull;
bool g_needRedraw = true;
bool g_setupMode = false;

// --- keyboard -------------------------------------------------------------

// ⚠️ isChange() is CONSUMING: it compares against its own last-seen key count
// and updates it, so the second call in the same loop returns false. Calling
// it once to detect an event and again to read it meant every press was seen
// and then read back as empty — no key did anything at all.
//
// So it is called exactly once here, and the report is handed to the pure
// mapper in lib/core/keymap.cpp where it can be tested on a host.
bool readKey(core::Key& out) {
    core::KeyReport r;
    r.changed = M5Cardputer.Keyboard.isChange();
    r.pressedCount = M5Cardputer.Keyboard.isPressed();
    if (!core::hasKeyEvent(r)) return false;

    const auto st = M5Cardputer.Keyboard.keysState();
    r.enter = st.enter;
    r.del = st.del;
    r.tab = st.tab;
    r.fn = st.fn;
    r.word = st.word.empty() ? nullptr : st.word.data();
    r.wordLen = st.word.size();
    return core::mapKey(r, out);
}

// --- battery --------------------------------------------------------------

int batteryPercent() {
    const int32_t level = M5Cardputer.Power.getBatteryLevel();
    return level < 0 ? 0 : (level > 100 ? 100 : (int)level);
}

// --- sending --------------------------------------------------------------

// Map an intent onto an optimistic claim, so the screen moves on the press
// and rolls back only if the gateway says no.
uint32_t claimFor(const core::Intent& in, uint32_t nowMs, bool viaIr) {
    using core::Field;
    const bool on = strcmp(in.action, "on") == 0;
    const bool off = strcmp(in.action, "off") == 0;

    if (strcmp(in.target, "hue") == 0) {
        if (on || off) return g_overlays.claim(Field::RoomOn, in.arg, on, nowMs);
        if (strcmp(in.action, "bri") == 0)
            return g_overlays.claim(Field::RoomBri, in.arg, in.arg2, nowMs);
    } else if (strcmp(in.target, "lw") == 0) {
        if (on || off) return g_overlays.claim(Field::LwOn, 0, on, nowMs);
        if (strcmp(in.action, "bri") == 0)
            return g_overlays.claim(Field::LwBri, 0, in.arg2, nowMs);
    } else if (strcmp(in.target, "yam") == 0) {
        if (on || off) return g_overlays.claim(Field::YamOn, 0, on, nowMs);
        if (strcmp(in.action, "vol") == 0)
            return g_overlays.claim(Field::YamRaw, 0,
                                    g_dash.yam.raw + in.arg * 5, nowMs);
        if (strcmp(in.action, "mute") == 0)
            return g_overlays.claim(Field::YamMute, 0, !g_dash.yam.mute, nowMs);
    } else if (strcmp(in.target, "tf") == 0) {
        if (strcmp(in.action, "power") == 0)
            return g_overlays.claim(Field::TfOn, 0, !g_dash.tf.on, nowMs, viaIr);
        if (strcmp(in.action, "vol") == 0)
            return g_overlays.claim(Field::TfVol, 0,
                                    g_dash.tf.volume + in.arg, nowMs, viaIr);
        if (strcmp(in.action, "mute") == 0)
            return g_overlays.claim(Field::TfMute, 0, !g_dash.tf.mute, nowMs, viaIr);
    } else if (strcmp(in.target, "disco") == 0) {
        if (on || off) return g_overlays.claim(Field::DiscoOn, 0, on, nowMs);
    } else if (strcmp(in.target, "fog") == 0) {
        if (on || off) return g_overlays.claim(Field::FogOn, 0, on, nowMs);
    }
    return 0;
}

// The Teufel over infrared. No acknowledgement exists, which is exactly why
// claimFor() marks these viaIr.
bool sendTeufelIr(const core::Intent& in) {
    if (strcmp(in.action, "power") == 0) {
        hwir::sendTeufel(ir::IR_POWER);
        return true;
    }
    if (strcmp(in.action, "vol") == 0) {
        const uint8_t code = in.arg > 0 ? ir::IR_VOL_UP : ir::IR_VOL_DOWN;
        const int n = in.arg > 0 ? in.arg : -in.arg;
        for (int i = 0; i < n && i < 10; ++i) hwir::sendTeufel(code);
        return true;
    }
    if (strcmp(in.action, "mute") == 0) {
        hwir::sendTeufel(ir::IR_MUTE);      // known to do nothing; see docs
        return true;
    }
    if (strcmp(in.action, "input") == 0) {
        hwir::sendTeufel(ir::IR_AUX);
        return true;
    }
    return false;
}

void sendIntent(const core::Intent& in, bool viaIr, uint32_t nowMs) {
    if (!in.valid) return;

    const uint32_t token = claimFor(in, nowMs, viaIr);
    g_needRedraw = true;

    if (viaIr && strcmp(in.target, "tf") == 0) {
        sendTeufelIr(in);
        return;                       // nothing to ask, nothing to confirm
    }

    char body[192];
    if (strcmp(in.target, "macro") == 0) {
        snprintf(body, sizeof(body),
                 "{\"target\":\"macro\",\"action\":\"%s\"}", in.action);
    } else if (strcmp(in.target, "fog") == 0 && strcmp(in.action, "on") == 0) {
        // The gateway refuses this without confirm:true. We only get here
        // after the user answered the on-screen prompt.
        snprintf(body, sizeof(body),
                 "{\"target\":\"fog\",\"action\":\"on\",\"confirm\":true}");
    } else if (in.name[0]) {
        // input / effect / mode all take a single named value, and the
        // parameter key is the action's own name.
        snprintf(body, sizeof(body),
                 "{\"target\":\"%s\",\"action\":\"%s\",\"%s\":\"%s\"}",
                 in.target, in.action, in.action, in.name);
    } else if (in.hasArg2) {
        const char* second = strcmp(in.action, "bri") == 0 ? "bri" : "value";
        if (in.hasArg) {
            snprintf(body, sizeof(body),
                     "{\"target\":\"%s\",\"action\":\"%s\",\"group\":%d,\"%s\":%d}",
                     in.target, in.action, in.arg, second, in.arg2);
        } else {
            snprintf(body, sizeof(body),
                     "{\"target\":\"%s\",\"action\":\"%s\",\"%s\":%d}",
                     in.target, in.action, second, in.arg2);
        }
    } else if (in.hasArg) {
        const char* key = strcmp(in.target, "hue") == 0 ? "group" : "step";
        snprintf(body, sizeof(body),
                 "{\"target\":\"%s\",\"action\":\"%s\",\"%s\":%d}",
                 in.target, in.action, key, in.arg);
    } else {
        snprintf(body, sizeof(body), "{\"target\":\"%s\",\"action\":\"%s\"}",
                 in.target, in.action);
    }
    net::requestAction(body, token);
}

// --- results --------------------------------------------------------------

void pumpNetwork(uint32_t nowMs) {
    net::Result r;
    while (net::takeResult(r)) {
        if (r.isDash) {
            if (r.ok && r.body && r.len > 0) {
                core::Dash fresh;
                if (core::parseDash(r.body, r.len, fresh, nowMs)) {
                    g_dash = fresh;
                    g_overlays.settleWith(g_dash, nowMs);
                    store::saveSnapshot(r.body, r.len);
                    g_needRedraw = true;
                }
                // A reply that will not parse is dropped on purpose: the last
                // good snapshot stays on screen, ageing visibly.
            }
        } else if (!r.ok && r.overlayToken) {
            // The house refused or never answered. Roll the screen back and
            // say so, rather than leaving a lie on the display.
            g_overlays.reject(r.overlayToken);
            core::toast(g_ui, r.status == 409 ? "abgelehnt" : "fehlgeschlagen",
                        nowMs);
            g_needRedraw = true;
        }
    }
}

// --- sleep ----------------------------------------------------------------

void goToSleep() {
    ui::setBacklight(0);
    M5Cardputer.Display.sleep();
    net::prepareForSleep();

    // The TCA8418 keeps scanning the matrix on its own and pulls its INT line
    // low on a press, so the ESP32 can be fully asleep and still be woken by
    // the keyboard. GPIO 11 is in the RTC domain, which is what makes it
    // usable as an ext0 source.
    const gpio_num_t wake = (gpio_num_t)board::kKeyboardIntPin;
    rtc_gpio_pullup_en(wake);
    esp_sleep_enable_ext0_wakeup(wake, 0);      // active low
    esp_deep_sleep_start();
}

// --- first-run setup ------------------------------------------------------

// Typed on the device. Nothing secret is ever compiled in, so the repository
// stays clean and a network change needs no toolchain.
void runSetup() {
    const char* prompts[] = {"WLAN-Name", "WLAN-Passwort",
                             "Gateway (leer = suchen)", "Token"};
    const bool masked[] = {false, true, false, true};
    char values[4][64] = {{0}, {0}, {0}, {0}};
    int step = 0, len = 0;

    ui::setBacklight(core::kBrightFull);
    ui::drawSetup(prompts[0], values[0], masked[0], 0);

    while (step < 4) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            const auto st = M5Cardputer.Keyboard.keysState();
            if (st.enter) {
                // Blank is allowed for the password (open network) and for
                // the host (mDNS finds it). Name and token are required.
                const bool optional = (step == 1) || (step == 2);
                if (len > 0 || optional) {
                    ++step;
                    len = 0;
                }
            } else if (st.del) {
                if (len > 0) values[step][--len] = 0;
            } else {
                for (auto c : st.word) {
                    if (c >= 32 && c < 127 && len < 62) {
                        values[step][len++] = c;
                        values[step][len] = 0;
                    }
                }
            }
            if (step < 4) ui::drawSetup(prompts[step], values[step],
                                        masked[step], step);
        }
        delay(10);
    }

    store::Config cfg;
    strncpy(cfg.ssid, values[0], sizeof(cfg.ssid) - 1);
    strncpy(cfg.pass, values[1], sizeof(cfg.pass) - 1);
    strncpy(cfg.host, values[2], sizeof(cfg.host) - 1);
    strncpy(cfg.token, values[3], sizeof(cfg.token) - 1);
    cfg.port = 5010;
    store::save(cfg);
    g_cfg = cfg;
    g_cfg.valid = true;
}

}  // namespace

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, /*enableKeyboard=*/true);
    ui::begin();
    hwir::begin();
    store::bumpBootCount();

    // If M5Unified does not recognise the board, Keyboard_Class::begin()
    // installs a do-nothing reader and reports it only over serial — leaving
    // a device whose keys are simply dead with no explanation on screen.
    const auto boardType = M5.getBoard();
    if (boardType != m5::board_t::board_M5Cardputer &&
        boardType != m5::board_t::board_M5CardputerADV) {
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5Cardputer.Display.drawString("Board nicht erkannt", 6, 20);
        M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        char b[40];
        snprintf(b, sizeof(b), "M5.getBoard() = %d", (int)boardType);
        M5Cardputer.Display.drawString(b, 6, 42);
        M5Cardputer.Display.drawString("Tastatur bleibt tot.", 6, 64);
        M5Cardputer.Display.drawString("Firmware fuer Cardputer?", 6, 86);
        delay(6000);
    }

    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    const bool fromSleep = cause == ESP_SLEEP_WAKEUP_EXT0;

    // Reset gesture, promised in the README: hold a key through power-on and
    // the stored credentials are dropped. Checked before load() so a wrong
    // token cannot lock you out of a device with no other input path.
    M5Cardputer.update();
    if (!fromSleep && M5Cardputer.Keyboard.isPressed()) {
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5Cardputer.Display.drawString("Taste halten fuer Reset...", 6, 40);
        // Long enough that a key pressed while plugging in does not wipe the
        // configuration by accident.
        uint32_t held = 0;
        while (held < 2000) {
            delay(50);
            M5Cardputer.update();
            if (!M5Cardputer.Keyboard.isPressed()) break;
            held += 50;
        }
        if (held >= 2000) {
            store::erase();
            M5Cardputer.Display.drawString("Zugangsdaten geloescht.  ", 6, 60);
            delay(1200);
        }
    }

#if M5SH_LOCAL_SECRETS
    // The compiled-in credentials apply exactly once per *distinct value
    // set*, not only into an empty NVS. The old gate — seed only when load()
    // fails — kept a device on whatever the FIRST local build baked: load()
    // accepts a config with an empty host, so a stale host or token survived
    // every reflash and newer values in secrets_local.h never reached the
    // device. The fingerprint records what was last seeded; a config typed on
    // the device survives until the baked values themselves change, and the
    // reset gesture still wipes everything (a local build then re-seeds on
    // the next boot, which for a personal device is the useful behaviour).
    {
        store::Config baked;
        strncpy(baked.ssid, M5SH_WIFI_SSID, sizeof(baked.ssid) - 1);
        strncpy(baked.pass, M5SH_WIFI_PASS, sizeof(baked.pass) - 1);
        strncpy(baked.token, M5SH_GW_TOKEN, sizeof(baked.token) - 1);
        strncpy(baked.host, M5SH_GW_HOST, sizeof(baked.host) - 1);
        baked.port = M5SH_GW_PORT;
        const uint32_t fp = core::configFingerprint(
            baked.ssid, baked.pass, baked.host, baked.port, baked.token);
        if (baked.ssid[0] && baked.token[0] &&
            store::seedFingerprint() != fp) {
            store::save(baked);
            store::setSeedFingerprint(fp);
        }
    }
#endif
    if (!store::load(g_cfg)) {
        g_setupMode = true;
        runSetup();
    }

    // Draw *something* before the radio is even up. On a wake the last
    // snapshot is right there in RTC memory; showing it marked stale beats
    // showing a spinner, and the fresh values slot in a moment later.
    char cached[1400];
    size_t cachedLen = 0;
    if (fromSleep && store::loadSnapshot(cached, sizeof(cached), cachedLen)) {
        core::parseDash(cached, cachedLen, g_dash, 0);
        // Age it deliberately: this data predates the sleep.
        g_dash.receivedAtMs = 0;
    }
    ui::setBacklight(core::kBrightFull);
    ui::draw(g_ui, g_dash, millis(), net::status(), batteryPercent(), false);

    net::begin(g_cfg);
    net::requestDash();
    g_lastKeyMs = millis();
    g_lastPollMs = millis();
}

void loop() {
    M5Cardputer.update();
    const uint32_t now = millis();

    core::Key k;
    if (readKey(k)) {
        g_lastKeyMs = now;

        core::Dash view = g_dash;
        g_overlays.apply(view, now);
        // 'u' on the home screen parks the device awake for an over-the-air
        // update. Deliberately not a background listener: this thing sleeps
        // after 30 s with its radio off, so an always-on OTA server would be
        // either asleep when wanted or the reason the battery does not last.
        // Handled here rather than in core, which knows nothing about radios.
        if (k.ch == 'u' && g_ui.screen == core::Screen::Home &&
            !g_ui.confirming) {
            ota::runMode();
            g_lastKeyMs = millis();
            g_needRedraw = true;
            return;
        }

        const core::KeyResult res = core::handleKey(g_ui, k, view, now);
        if (res.intent.valid) sendIntent(res.intent, res.viaIr, now);
        if (res.redraw) g_needRedraw = true;

        // Any press means the user is here: refresh sooner than the idle
        // schedule would.
        if (now - g_lastPollMs > 400) {
            net::requestDash();
            g_lastPollMs = now;
        }
    }

    pumpNetwork(now);
    g_overlays.expire(now);

    const uint32_t sinceKey = now - g_lastKeyMs;
    if (core::shouldPoll(now - g_lastPollMs, sinceKey)) {
        if (net::requestDash()) g_lastPollMs = now;
    }

    const uint8_t want = core::backlightFor(sinceKey);
    if (want != g_backlight) {
        g_backlight = want;
        ui::setBacklight(want);
    }

    // Redraw on change, plus a slow tick so the "stand N s alt" counter and
    // the toast timeout stay truthful without burning frames.
    if (g_needRedraw || (now - g_lastDrawMs) > 1000) {
        core::Dash view = g_dash;
        g_overlays.apply(view, now);
        ui::draw(g_ui, view, now, net::status(), batteryPercent(),
                 g_overlays.hasUnconfirmed(now));
        g_needRedraw = false;
        g_lastDrawMs = now;
    }

    if (core::shouldSleep(sinceKey, net::busy() || g_ui.confirming)) {
        goToSleep();
    }

    delay(5);
}
