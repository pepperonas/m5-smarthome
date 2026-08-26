// Host tests for the pure core. Run with:  pio test -e native
//
// Everything here is deterministic: no display, no radio, no clock of its own.
// The Arduino shell around these modules is verified on the device instead.

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "command.h"
#include "dash.h"
#include "ir_nec.h"
#include "ir_teufel.h"
#include "netplan.h"
#include "optimistic.h"

using namespace core;

// A real gateway reply, captured live from raspi5 on 2026-08-26 (721 bytes).
static const char kLiveSnapshot[] =
"{\"t\":1787714151,\"hue\":{\"g\":[{\"i\":81,\"n\":\"Wohnzimmer\",\"on\":true,\"b\":174},"
"{\"i\":82,\"n\":\"Schlafzimmer\",\"on\":true,\"b\":162},{\"i\":83,\"n\":\"K\xC3\xBC" "che\",\"on\":true,\"b\":254},"
"{\"i\":84,\"n\":\"Badezimmer\",\"on\":true,\"b\":254},{\"i\":85,\"n\":\"Flur\",\"on\":true,\"b\":24},"
"{\"i\":86,\"n\":\"Garten\",\"on\":false,\"b\":190}],\"on\":5},"
"\"lw\":{\"on\":false,\"b\":255,\"fx\":\"iris_warn\",\"warn\":true},"
"\"yam\":{\"on\":true,\"raw\":-280,\"vol\":-28.0,\"mute\":false,\"in\":\"Spotify\"},"
"\"tf\":{\"on\":true,\"vol\":29,\"mute\":true,\"in\":\"AUX\",\"est\":true},"
"\"fog\":{\"on\":false,\"tank\":48,\"ml\":120},"
"\"disco\":{\"on\":false,\"bpm\":0,\"spl\":31.5,\"mode\":\"rainbow\"},"
"\"clima\":{\"in\":{\"t\":22.7,\"h\":54},\"out\":{\"t\":16.3,\"h\":65}},"
"\"wx\":{\"t\":14.1,\"ic\":\"04n\",\"d\":\"Bedeckt\",\"hi\":26,\"lo\":14},"
"\"pi\":{\"cpu\":2.6,\"tmp\":47.4,\"mem\":15.0}}";

const char* liveSnapshotJson() { return kLiveSnapshot; }

static Dash liveDash(uint32_t nowMs = 1000) {
    Dash d;
    TEST_ASSERT_TRUE(parseDash(kLiveSnapshot, strlen(kLiveSnapshot), d, nowMs));
    return d;
}

// ---------------------------------------------------------------- dash ----

void test_parses_a_real_gateway_snapshot(void) {
    Dash d = liveDash();
    TEST_ASSERT_TRUE(d.valid);
    TEST_ASSERT_EQUAL(6, d.hue.count);
    TEST_ASSERT_EQUAL(5, d.hue.litCount);
    TEST_ASSERT_EQUAL_STRING("Wohnzimmer", d.hue.rooms[0].name);
    TEST_ASSERT_EQUAL(174, d.hue.rooms[0].bri);
    TEST_ASSERT_FALSE(d.hue.rooms[5].on);              // Garten
    TEST_ASSERT_EQUAL(-280, d.yam.raw);
    TEST_ASSERT_EQUAL_FLOAT(-28.0f, d.yam.db);
    TEST_ASSERT_EQUAL_STRING("Spotify", d.yam.input);
    TEST_ASSERT_EQUAL(48, d.fog.tankPct);
    TEST_ASSERT_TRUE(d.lw.warnOwned);
}

void test_teufel_is_always_marked_as_an_estimate(void) {
    // The Pi flips a boolean after firing IR; nothing confirms it. If the
    // remote presents that as fact, it is lying with a straight face.
    TEST_ASSERT_TRUE(liveDash().tf.estimated);
}

void test_a_broken_payload_leaves_the_last_good_state_alone(void) {
    // The house rule that cost the dashboard its dB chart: a failed poll is
    // not "no data".
    Dash d = liveDash();
    const int roomsBefore = d.hue.count;
    TEST_ASSERT_FALSE(parseDash("<html>502 Bad Gateway</html>", 28, d, 2000));
    TEST_ASSERT_EQUAL(roomsBefore, d.hue.count);
    TEST_ASSERT_TRUE(d.valid);
}

void test_an_empty_payload_is_rejected(void) {
    Dash d = liveDash();
    TEST_ASSERT_FALSE(parseDash("", 0, d, 2000));
    TEST_ASSERT_FALSE(parseDash(nullptr, 10, d, 2000));
    TEST_ASSERT_EQUAL(6, d.hue.count);
}

void test_valid_json_that_is_not_a_snapshot_is_rejected(void) {
    Dash d;
    TEST_ASSERT_FALSE(parseDash("{\"error\":\"unauthorized\"}", 24, d, 1));
    TEST_ASSERT_FALSE(d.valid);
}

void test_missing_and_stale_sources_are_told_apart(void) {
    const char* j = "{\"t\":1,\"lw\":{\"on\":true,\"b\":10},"
                    "\"err\":[\"yam\",\"wx\"],\"old\":[\"lw\"]}";
    Dash d;
    TEST_ASSERT_TRUE(parseDash(j, strlen(j), d, 500));
    TEST_ASSERT_FALSE(d.sourceOk(SRC_YAM));      // never arrived
    TEST_ASSERT_TRUE(d.sourceOk(SRC_LW));        // arrived...
    TEST_ASSERT_TRUE(d.sourceStale(SRC_LW));     // ...but is a last-known value
    TEST_ASSERT_FALSE(d.sourceStale(SRC_YAM));
}

void test_more_rooms_than_fit_do_not_overflow(void) {
    char big[2048];
    int n = snprintf(big, sizeof(big), "{\"t\":1,\"hue\":{\"on\":0,\"g\":[");
    for (int i = 0; i < 20; ++i) {
        n += snprintf(big + n, sizeof(big) - n,
                      "%s{\"i\":%d,\"n\":\"R%d\",\"on\":false,\"b\":1}",
                      i ? "," : "", 100 + i, i);
    }
    snprintf(big + n, sizeof(big) - n, "]}}");
    Dash d;
    TEST_ASSERT_TRUE(parseDash(big, strlen(big), d, 1));
    TEST_ASSERT_EQUAL(kMaxRooms, d.hue.count);
}

void test_a_long_room_name_is_truncated_not_overflowed(void) {
    const char* j = "{\"t\":1,\"hue\":{\"on\":0,\"g\":[{\"i\":9,"
        "\"n\":\"AVeryLongRoomNameThatDoesNotFit\",\"on\":true,\"b\":1}]}}";
    Dash d;
    TEST_ASSERT_TRUE(parseDash(j, strlen(j), d, 1));
    TEST_ASSERT_EQUAL(kNameLen - 1, (int)strlen(d.hue.rooms[0].name));
}

void test_a_snapshot_from_before_a_reboot_counts_as_stale(void) {
    // millis() restarts at 0 after deep sleep, so a snapshot restored from
    // RTC memory carries a timestamp in the future. Guessing "fresh" there
    // would show week-old values as current.
    Dash d = liveDash(/*nowMs=*/900000);
    TEST_ASSERT_TRUE(isStale(d, /*nowMs=*/50));
    TEST_ASSERT_EQUAL(kStaleAfterMs, ageMs(d, 50));
}

void test_freshness_window(void) {
    Dash d = liveDash(1000);
    TEST_ASSERT_FALSE(isStale(d, 1000 + kStaleAfterMs - 1));
    TEST_ASSERT_TRUE(isStale(d, 1000 + kStaleAfterMs));
}

// ------------------------------------------------------------- overlay ----

void test_a_press_shows_immediately(void) {
    Dash d = liveDash();
    OverlayStore ov;
    TEST_ASSERT_TRUE(findRoom(d, 86)->on == false);
    ov.claim(Field::RoomOn, 86, 1, 1000);
    ov.apply(d, 1000);
    TEST_ASSERT_TRUE(findRoom(d, 86)->on);
    TEST_ASSERT_EQUAL(6, d.hue.litCount);     // header agrees with the tiles
}

void test_a_rejected_request_rolls_the_screen_back(void) {
    Dash d = liveDash();
    OverlayStore ov;
    uint32_t tok = ov.claim(Field::RoomOn, 86, 1, 1000);
    ov.reject(tok);
    ov.apply(d, 1000);
    TEST_ASSERT_FALSE(findRoom(d, 86)->on);
}

void test_a_claim_the_house_never_honours_expires(void) {
    Dash d = liveDash();
    OverlayStore ov;
    ov.claim(Field::LwOn, 0, 1, 1000);
    ov.apply(d, 1000);
    TEST_ASSERT_TRUE(d.lw.on);

    Dash fresh = liveDash();                  // house still says off
    ov.apply(fresh, 1000 + kOverlayTtlMs + 1);
    TEST_ASSERT_FALSE(fresh.lw.on);
}

void test_a_snapshot_that_agrees_retires_the_claim(void) {
    // Otherwise the overlay keeps re-asserting a value the house already
    // reports, and any later change flickers back for a few seconds.
    OverlayStore ov;
    ov.claim(Field::LwOn, 0, 1, 1000);
    TEST_ASSERT_EQUAL(1, ov.activeCount(1100));

    Dash agreeing = liveDash();
    agreeing.lw.on = true;
    ov.settleWith(agreeing, 1100);
    TEST_ASSERT_EQUAL(0, ov.activeCount(1100));
}

void test_repeated_presses_reuse_one_slot(void) {
    // Holding "+" must not fill the table and evict unrelated claims.
    OverlayStore ov;
    ov.claim(Field::YamOn, 0, 1, 1000);
    for (int i = 0; i < 20; ++i) ov.claim(Field::YamRaw, 0, -280 + i, 1000);
    TEST_ASSERT_EQUAL(2, ov.activeCount(1000));
}

void test_an_ir_press_is_marked_unconfirmed_and_stays_that_way(void) {
    // Infrared is blind: the LED fires and nothing answers. Even the Pi's own
    // view of the Teufel is an estimate, so network agreement proves nothing.
    OverlayStore ov;
    ov.claim(Field::TfVol, 0, 30, 1000, /*viaIr=*/true);
    TEST_ASSERT_TRUE(ov.hasUnconfirmed(1500));

    Dash agreeing = liveDash();
    agreeing.tf.volume = 30;
    ov.settleWith(agreeing, 1500);
    TEST_ASSERT_TRUE(ov.hasUnconfirmed(1500));          // still unconfirmed
    TEST_ASSERT_FALSE(ov.hasUnconfirmed(1000 + kUnconfirmedMs + 1));
}

void test_setting_brightness_also_shows_the_room_as_on(void) {
    // hue-controller auto-activates a dark group when brightness is set;
    // leaving the tile dark would contradict what the lamp does.
    Dash d = liveDash();
    OverlayStore ov;
    ov.claim(Field::RoomBri, 86, 200, 1000);
    ov.apply(d, 1000);
    TEST_ASSERT_TRUE(findRoom(d, 86)->on);
    TEST_ASSERT_EQUAL(200, findRoom(d, 86)->bri);
}

// ------------------------------------------------------------- command ----

void test_room_on_off_in_german(void) {
    Dash d = liveDash();
    Intent i = parseCommand("wohnzimmer aus", d);
    TEST_ASSERT_TRUE(i.valid);
    TEST_ASSERT_EQUAL_STRING("hue", i.target);
    TEST_ASSERT_EQUAL_STRING("off", i.action);
    TEST_ASSERT_EQUAL(81, i.arg);
}

void test_a_prefix_is_enough(void) {
    Dash d = liveDash();
    Intent i = parseCommand("wohn an", d);
    TEST_ASSERT_TRUE(i.valid);
    TEST_ASSERT_EQUAL(81, i.arg);
    TEST_ASSERT_EQUAL_STRING("on", i.action);
}

void test_umlauts_match_however_they_are_typed(void) {
    Dash d = liveDash();
    for (const char* s : {"küche aus", "kueche aus", "kuche aus", "KÜCHE AUS"}) {
        Intent i = parseCommand(s, d);
        TEST_ASSERT_TRUE_MESSAGE(i.valid, s);
        TEST_ASSERT_EQUAL(83, i.arg);
    }
}

void test_a_typo_still_lands(void) {
    Dash d = liveDash();
    Intent i = parseCommand("wohnzimer aus", d);       // one letter missing
    TEST_ASSERT_TRUE(i.valid);
    TEST_ASSERT_EQUAL(81, i.arg);
}

void test_two_typos_suggest_but_never_switch(void) {
    // One slip is forgiven; two is a guess. Acting on a guess is how you
    // switch the wrong room — so the second typo downgrades to a suggestion.
    Dash d = liveDash();
    Intent i = parseCommand("wohnzimee aus", d);
    TEST_ASSERT_FALSE(i.valid);
    TEST_ASSERT_TRUE(strstr(i.hint, "Wohnzimmer") != nullptr);
}

void test_a_percentage_becomes_hue_brightness(void) {
    Dash d = liveDash();
    Intent i = parseCommand("flur 50", d);
    TEST_ASSERT_TRUE(i.valid);
    TEST_ASSERT_EQUAL_STRING("bri", i.action);
    TEST_ASSERT_EQUAL(85, i.arg);
    TEST_ASSERT_EQUAL(127, i.arg2);                    // 50 % of 254
}

void test_zero_percent_never_becomes_an_invalid_brightness(void) {
    // Hue rejects bri 0; "off" is a different command.
    Dash d = liveDash();
    Intent i = parseCommand("flur 0", d);
    TEST_ASSERT_TRUE(i.valid);
    TEST_ASSERT_EQUAL(1, i.arg2);
}

void test_alles_aus_is_the_house_macro(void) {
    Dash d = liveDash();
    for (const char* s : {"aus", "alles aus"}) {
        Intent i = parseCommand(s, d);
        TEST_ASSERT_TRUE_MESSAGE(i.valid, s);
        TEST_ASSERT_EQUAL_STRING("macro", i.target);
        TEST_ASSERT_EQUAL_STRING("alloff", i.action);
    }
}

void test_fog_from_the_command_line_demands_confirmation(void) {
    // A typo on a command line must not be able to ignite a 220 V heater.
    Dash d = liveDash();
    Intent i = parseCommand("nebel", d);
    TEST_ASSERT_TRUE(i.valid);
    TEST_ASSERT_EQUAL_STRING("fog", i.target);
    TEST_ASSERT_TRUE(i.needsConfirm);
}

void test_nothing_else_demands_confirmation(void) {
    Dash d = liveDash();
    for (const char* s : {"disco", "licht", "wohnzimmer an", "alles aus"}) {
        TEST_ASSERT_FALSE_MESSAGE(parseCommand(s, d).needsConfirm, s);
    }
}

void test_gibberish_is_refused_with_a_hint_not_executed(void) {
    Dash d = liveDash();
    Intent i = parseCommand("xyzzy", d);
    TEST_ASSERT_FALSE(i.valid);
    TEST_ASSERT_EQUAL_STRING("unbekannt", i.hint);
}

void test_a_near_miss_says_what_it_thought_you_meant(void) {
    Dash d = liveDash();
    Intent i = parseCommand("disko", d);
    TEST_ASSERT_FALSE(i.valid);
    TEST_ASSERT_TRUE(strstr(i.hint, "disco") != nullptr);
}

void test_empty_input_is_not_a_command(void) {
    Dash d = liveDash();
    TEST_ASSERT_FALSE(parseCommand("", d).valid);
    TEST_ASSERT_FALSE(parseCommand("   ", d).valid);
    TEST_ASSERT_FALSE(parseCommand(nullptr, d).valid);
}

void test_completion_only_fires_on_a_real_prefix(void) {
    // Silently rewriting a typo into a different room is how you switch the
    // wrong floor. Completion is for prefixes only.
    Dash d = liveDash();
    char buf[24];
    TEST_ASSERT_NOT_NULL(completeCommand("wohn", d, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("Wohnzimmer", buf);
    TEST_ASSERT_NULL(completeCommand("zimmer", d, buf, sizeof(buf)));
}

void test_short_words_are_not_fuzzy_matched(void) {
    // "an" must never be read as a near miss for a three-letter room.
    TEST_ASSERT_EQUAL(0, matchScore("an", "aux"));
    TEST_ASSERT_EQUAL(0, matchScore("aus", "bus"));
}

// ------------------------------------------------------------------ ir ----

void test_nec_frame_byte_order(void) {
    // NEC goes out LSB-first: address low, address high, command, ~command.
    const uint32_t f = ir::necFrame(ir::kTeufelAddress, ir::IR_POWER);
    TEST_ASSERT_EQUAL_HEX8(0x80, f & 0xFF);            // 0x5780 low
    TEST_ASSERT_EQUAL_HEX8(0x57, (f >> 8) & 0xFF);     // 0x5780 high
    TEST_ASSERT_EQUAL_HEX8(0x48, (f >> 16) & 0xFF);    // POWER
    TEST_ASSERT_EQUAL_HEX8(0xB7, (f >> 24) & 0xFF);    // ~0x48
}

void test_command_complement_is_what_a_receiver_checks(void) {
    for (size_t i = 0; i < ir::kTeufelCommandCount; ++i) {
        const uint8_t c = ir::kTeufelCommands[i].code;
        const uint32_t f = ir::necFrame(ir::kTeufelAddress, c);
        const uint8_t cmd = (f >> 16) & 0xFF, inv = (f >> 24) & 0xFF;
        TEST_ASSERT_EQUAL_HEX8(0xFF, cmd ^ inv);
    }
}

void test_first_bit_on_the_wire_is_bit_zero(void) {
    const uint32_t f = ir::necFrame(ir::kTeufelAddress, ir::IR_POWER);
    TEST_ASSERT_FALSE(ir::necBit(f, 0));               // 0x80 -> bit0 = 0
    TEST_ASSERT_TRUE(ir::necBit(f, 7));                // 0x80 -> bit7 = 1
    TEST_ASSERT_FALSE(ir::necBit(f, 32));              // out of range
}

void test_the_ir_table_carries_the_house_codes(void) {
    // Spot-check against the canonical CSV so a regenerated table that lost
    // its mapping cannot ship silently.
    TEST_ASSERT_EQUAL_HEX8(0x48, ir::IR_POWER);
    TEST_ASSERT_EQUAL_HEX8(0xB0, ir::IR_VOL_UP);
    TEST_ASSERT_EQUAL_HEX8(0x30, ir::IR_VOL_DOWN);
    TEST_ASSERT_EQUAL_HEX8(0x44, ir::IR_AUX);
    TEST_ASSERT_EQUAL(19, (int)ir::kTeufelCommandCount);
    TEST_ASSERT_EQUAL_HEX16(0x5780, ir::kTeufelAddress);
}

void test_bit_timing_constants(void) {
    TEST_ASSERT_EQUAL(1690, ir::necSpaceFor(true));
    TEST_ASSERT_EQUAL(560, ir::necSpaceFor(false));
}

// ------------------------------------------------------------- netplan ----

void test_polling_is_fast_while_in_use_and_slow_when_idle(void) {
    TEST_ASSERT_EQUAL(kPollActiveMs, pollInterval(0));
    TEST_ASSERT_EQUAL(kPollActiveMs, pollInterval(kActiveWindowMs - 1));
    TEST_ASSERT_EQUAL(kPollIdleMs, pollInterval(kActiveWindowMs));
}

void test_backlight_dims_then_goes_dark(void) {
    TEST_ASSERT_EQUAL(kBrightFull, backlightFor(0));
    TEST_ASSERT_EQUAL(kBrightDim, backlightFor(kDimAfterMs));
    TEST_ASSERT_EQUAL(0, backlightFor(kScreenOffAfterMs));
}

void test_sleep_never_interrupts_an_in_flight_request(void) {
    // Otherwise the reply is lost and the press silently undoes itself.
    TEST_ASSERT_TRUE(shouldSleep(kSleepAfterMs, /*busy=*/false));
    TEST_ASSERT_FALSE(shouldSleep(kSleepAfterMs * 10, /*busy=*/true));
}

void test_backoff_doubles_and_is_capped(void) {
    TEST_ASSERT_EQUAL(0u, backoffDelay(0));
    TEST_ASSERT_EQUAL(kBackoffStartMs, backoffDelay(1));
    TEST_ASSERT_EQUAL(kBackoffStartMs * 2, backoffDelay(2));
    TEST_ASSERT_EQUAL(kBackoffStartMs * 4, backoffDelay(3));
    TEST_ASSERT_EQUAL(kBackoffMaxMs, backoffDelay(99));
}

void test_a_stale_access_point_hint_is_not_used(void) {
    // APs change channel. A stale hint costs a failed fast-connect plus a
    // full scan, which is worse than scanning straight away.
    ApHint h;
    h.valid = true;
    h.channel = 6;
    h.bssid[0] = 0xAA;
    h.savedAtEpoch = 1000;
    TEST_ASSERT_TRUE(apHintUsable(h, 1000 + kApHintTtlS - 1));
    TEST_ASSERT_FALSE(apHintUsable(h, 1000 + kApHintTtlS));
}

void test_an_empty_access_point_hint_is_rejected(void) {
    ApHint h;
    TEST_ASSERT_FALSE(apHintUsable(h, 1000));
    h.valid = true;
    h.channel = 6;
    TEST_ASSERT_FALSE(apHintUsable(h, 1000));      // all-zero BSSID
}

// -------------------------------------------------------------------------

void test_digits_jump_straight_into_an_app(void);
void test_escape_always_goes_home(void);
void test_room_list_wraps_around(void);
void test_enter_toggles_the_selected_room(void);
void test_brightness_keys_stay_inside_the_hue_range(void);
void test_fog_needs_a_second_key_press(void);
void test_any_other_key_cancels_the_fog_prompt(void);
void test_while_confirming_no_other_key_does_anything(void);
void test_turning_fog_off_is_never_gated(void);
void test_goodnight_is_confirmed_too(void);
void test_console_opens_and_runs_a_command(void);
void test_console_fog_command_still_asks(void);
void test_console_backspace_and_escape(void);
void test_tab_completes_a_room_name(void);
void test_console_input_cannot_overflow(void);
void test_an_unknown_command_reports_instead_of_acting(void);
void test_teufel_transport_can_be_switched_to_ir(void);
void test_teufel_mute_warns_about_the_known_quirk(void);
void test_a_toast_expires(void);
void test_keys_that_mean_nothing_here_do_nothing(void);

void test_input_cycling_carries_the_name(void);
void test_input_cycling_wraps(void);
void test_effect_and_mode_cycling_carry_names(void);
void test_iris_warn_is_not_in_the_effect_cycle(void);
void test_the_effect_key_yields_while_strip_warn_owns_the_strip(void);
void test_cycle_lists_match_the_gateway_whitelists(void);

void test_enter_opens_the_selected_row_on_the_home_screen(void);
void test_arrows_and_digits_agree_about_every_home_row(void);
void test_every_screen_reachable_from_home_responds_to_enter(void);
void test_the_home_cursor_stays_in_range(void);

void test_no_event_when_nothing_changed(void);
void test_no_event_when_no_key_is_down(void);
void test_a_changed_press_is_an_event(void);
void test_the_printed_arrows_map_to_directions(void);
void test_slash_also_arrives_as_a_character(void);
void test_backtick_is_escape(void);
void test_enter_arrives_with_an_empty_word(void);
void test_a_null_word_is_not_a_crash(void);
void test_ordinary_characters_pass_through(void);
void test_digits_are_not_swallowed_by_the_arrow_mapping(void);
void test_only_the_first_character_is_used(void);
void test_the_output_is_untouched_when_there_is_no_event(void);

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parses_a_real_gateway_snapshot);
    RUN_TEST(test_teufel_is_always_marked_as_an_estimate);
    RUN_TEST(test_a_broken_payload_leaves_the_last_good_state_alone);
    RUN_TEST(test_an_empty_payload_is_rejected);
    RUN_TEST(test_valid_json_that_is_not_a_snapshot_is_rejected);
    RUN_TEST(test_missing_and_stale_sources_are_told_apart);
    RUN_TEST(test_more_rooms_than_fit_do_not_overflow);
    RUN_TEST(test_a_long_room_name_is_truncated_not_overflowed);
    RUN_TEST(test_a_snapshot_from_before_a_reboot_counts_as_stale);
    RUN_TEST(test_freshness_window);

    RUN_TEST(test_a_press_shows_immediately);
    RUN_TEST(test_a_rejected_request_rolls_the_screen_back);
    RUN_TEST(test_a_claim_the_house_never_honours_expires);
    RUN_TEST(test_a_snapshot_that_agrees_retires_the_claim);
    RUN_TEST(test_repeated_presses_reuse_one_slot);
    RUN_TEST(test_an_ir_press_is_marked_unconfirmed_and_stays_that_way);
    RUN_TEST(test_setting_brightness_also_shows_the_room_as_on);

    RUN_TEST(test_room_on_off_in_german);
    RUN_TEST(test_a_prefix_is_enough);
    RUN_TEST(test_umlauts_match_however_they_are_typed);
    RUN_TEST(test_a_typo_still_lands);
    RUN_TEST(test_two_typos_suggest_but_never_switch);
    RUN_TEST(test_a_percentage_becomes_hue_brightness);
    RUN_TEST(test_zero_percent_never_becomes_an_invalid_brightness);
    RUN_TEST(test_alles_aus_is_the_house_macro);
    RUN_TEST(test_fog_from_the_command_line_demands_confirmation);
    RUN_TEST(test_nothing_else_demands_confirmation);
    RUN_TEST(test_gibberish_is_refused_with_a_hint_not_executed);
    RUN_TEST(test_a_near_miss_says_what_it_thought_you_meant);
    RUN_TEST(test_empty_input_is_not_a_command);
    RUN_TEST(test_completion_only_fires_on_a_real_prefix);
    RUN_TEST(test_short_words_are_not_fuzzy_matched);

    RUN_TEST(test_nec_frame_byte_order);
    RUN_TEST(test_command_complement_is_what_a_receiver_checks);
    RUN_TEST(test_first_bit_on_the_wire_is_bit_zero);
    RUN_TEST(test_the_ir_table_carries_the_house_codes);
    RUN_TEST(test_bit_timing_constants);

    RUN_TEST(test_polling_is_fast_while_in_use_and_slow_when_idle);
    RUN_TEST(test_backlight_dims_then_goes_dark);
    RUN_TEST(test_sleep_never_interrupts_an_in_flight_request);
    RUN_TEST(test_backoff_doubles_and_is_capped);
    RUN_TEST(test_a_stale_access_point_hint_is_not_used);
    RUN_TEST(test_an_empty_access_point_hint_is_rejected);
    RUN_TEST(test_enter_opens_the_selected_row_on_the_home_screen);
    RUN_TEST(test_arrows_and_digits_agree_about_every_home_row);
    RUN_TEST(test_every_screen_reachable_from_home_responds_to_enter);
    RUN_TEST(test_the_home_cursor_stays_in_range);
    RUN_TEST(test_digits_jump_straight_into_an_app);
    RUN_TEST(test_escape_always_goes_home);
    RUN_TEST(test_room_list_wraps_around);
    RUN_TEST(test_enter_toggles_the_selected_room);
    RUN_TEST(test_brightness_keys_stay_inside_the_hue_range);
    RUN_TEST(test_fog_needs_a_second_key_press);
    RUN_TEST(test_any_other_key_cancels_the_fog_prompt);
    RUN_TEST(test_while_confirming_no_other_key_does_anything);
    RUN_TEST(test_turning_fog_off_is_never_gated);
    RUN_TEST(test_goodnight_is_confirmed_too);
    RUN_TEST(test_console_opens_and_runs_a_command);
    RUN_TEST(test_console_fog_command_still_asks);
    RUN_TEST(test_console_backspace_and_escape);
    RUN_TEST(test_tab_completes_a_room_name);
    RUN_TEST(test_console_input_cannot_overflow);
    RUN_TEST(test_an_unknown_command_reports_instead_of_acting);
    RUN_TEST(test_teufel_transport_can_be_switched_to_ir);
    RUN_TEST(test_teufel_mute_warns_about_the_known_quirk);
    RUN_TEST(test_input_cycling_carries_the_name);
    RUN_TEST(test_input_cycling_wraps);
    RUN_TEST(test_effect_and_mode_cycling_carry_names);
    RUN_TEST(test_iris_warn_is_not_in_the_effect_cycle);
    RUN_TEST(test_the_effect_key_yields_while_strip_warn_owns_the_strip);
    RUN_TEST(test_cycle_lists_match_the_gateway_whitelists);
    RUN_TEST(test_a_toast_expires);
    RUN_TEST(test_keys_that_mean_nothing_here_do_nothing);
    RUN_TEST(test_no_event_when_nothing_changed);
    RUN_TEST(test_no_event_when_no_key_is_down);
    RUN_TEST(test_a_changed_press_is_an_event);
    RUN_TEST(test_the_printed_arrows_map_to_directions);
    RUN_TEST(test_slash_also_arrives_as_a_character);
    RUN_TEST(test_backtick_is_escape);
    RUN_TEST(test_enter_arrives_with_an_empty_word);
    RUN_TEST(test_a_null_word_is_not_a_crash);
    RUN_TEST(test_ordinary_characters_pass_through);
    RUN_TEST(test_digits_are_not_swallowed_by_the_arrow_mapping);
    RUN_TEST(test_only_the_first_character_is_used);
    RUN_TEST(test_the_output_is_untouched_when_there_is_no_event);
    return UNITY_END();
}
