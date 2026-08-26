// Interaction model tests. No pixels involved.
#include <unity.h>

#include <cstring>
#include <initializer_list>

#include "dash.h"
#include "ui_state.h"

using namespace core;

extern const char* liveSnapshotJson();     // shared with test_main.cpp

static Dash makeDash() {
    Dash d;
    const char* j = liveSnapshotJson();
    parseDash(j, strlen(j), d, 1000);
    return d;
}

static Key chr(char c) { Key k; k.ch = c; return k; }
static Key enterKey() { Key k; k.enter = true; return k; }
static Key escKey() { Key k; k.esc = true; return k; }
static Key downKey() { Key k; k.down = true; return k; }
static Key upKey() { Key k; k.up = true; return k; }

void test_digits_jump_straight_into_an_app(void) {
    // The reason this device has a keyboard: no menu walking.
    Dash d = makeDash();
    UiState st;
    handleKey(st, chr('3'), d, 1000);
    TEST_ASSERT_TRUE(st.screen == Screen::Yamaha);
    handleKey(st, escKey(), d, 1000);
    TEST_ASSERT_TRUE(st.screen == Screen::Home);
    handleKey(st, chr('1'), d, 1000);
    TEST_ASSERT_TRUE(st.screen == Screen::Rooms);
}

void test_escape_always_goes_home(void) {
    Dash d = makeDash();
    UiState st;
    for (char c : {'1', '2', '3', '4', '5', '6', '7'}) {
        st.screen = Screen::Home;
        handleKey(st, chr(c), d, 1000);
        handleKey(st, escKey(), d, 1000);
        TEST_ASSERT_TRUE(st.screen == Screen::Home);
    }
}

void test_room_list_wraps_around(void) {
    Dash d = makeDash();
    UiState st;
    st.screen = Screen::Rooms;
    handleKey(st, upKey(), d, 1000);
    TEST_ASSERT_EQUAL(d.hue.count - 1, st.cursor);      // wrapped backwards
    handleKey(st, downKey(), d, 1000);
    TEST_ASSERT_EQUAL(0, st.cursor);
}

void test_enter_toggles_the_selected_room(void) {
    Dash d = makeDash();
    UiState st;
    st.screen = Screen::Rooms;
    st.cursor = 0;                                       // Wohnzimmer, on
    KeyResult r = handleKey(st, enterKey(), d, 1000);
    TEST_ASSERT_TRUE(r.intent.valid);
    TEST_ASSERT_EQUAL_STRING("hue", r.intent.target);
    TEST_ASSERT_EQUAL_STRING("off", r.intent.action);
    TEST_ASSERT_EQUAL(81, r.intent.arg);
}

void test_brightness_keys_stay_inside_the_hue_range(void) {
    Dash d = makeDash();
    UiState st;
    st.screen = Screen::Rooms;
    st.cursor = 2;                                       // Küche, bri 254
    KeyResult up = handleKey(st, chr('+'), d, 1000);
    TEST_ASSERT_EQUAL(254, up.intent.arg2);              // clamped, not 286
    st.cursor = 4;                                       // Flur, bri 24
    for (int i = 0; i < 5; ++i) {
        KeyResult dn = handleKey(st, chr('-'), d, 1000);
        TEST_ASSERT_TRUE(dn.intent.arg2 >= 1);           // never 0: that is "off"
    }
}

void test_fog_needs_a_second_key_press(void) {
    // The single most dangerous thing this remote can do. One stray press in
    // a pocket must not be enough.
    Dash d = makeDash();
    UiState st;
    st.screen = Screen::Fog;
    KeyResult first = handleKey(st, enterKey(), d, 1000);
    TEST_ASSERT_FALSE(first.intent.valid);               // nothing sent yet
    TEST_ASSERT_TRUE(st.confirming);

    KeyResult second = handleKey(st, enterKey(), d, 1000);
    TEST_ASSERT_TRUE(second.intent.valid);
    TEST_ASSERT_EQUAL_STRING("fog", second.intent.target);
    TEST_ASSERT_EQUAL_STRING("on", second.intent.action);
    TEST_ASSERT_FALSE(st.confirming);
}

void test_any_other_key_cancels_the_fog_prompt(void) {
    Dash d = makeDash();
    UiState st;
    st.screen = Screen::Fog;
    handleKey(st, enterKey(), d, 1000);
    KeyResult r = handleKey(st, chr('n'), d, 1000);
    TEST_ASSERT_FALSE(r.intent.valid);
    TEST_ASSERT_FALSE(st.confirming);
}

void test_while_confirming_no_other_key_does_anything(void) {
    // Otherwise a digit press would navigate away and leave a live prompt.
    Dash d = makeDash();
    UiState st;
    st.screen = Screen::Fog;
    handleKey(st, enterKey(), d, 1000);
    handleKey(st, chr('3'), d, 1000);
    TEST_ASSERT_TRUE(st.screen == Screen::Fog);          // did not navigate
}

void test_turning_fog_off_is_never_gated(void) {
    Dash d = makeDash();
    d.fog.on = true;
    UiState st;
    st.screen = Screen::Fog;
    KeyResult r = handleKey(st, enterKey(), d, 1000);
    TEST_ASSERT_TRUE(r.intent.valid);
    TEST_ASSERT_EQUAL_STRING("off", r.intent.action);
    TEST_ASSERT_FALSE(st.confirming);
}

void test_goodnight_is_confirmed_too(void) {
    Dash d = makeDash();
    UiState st;
    KeyResult first = handleKey(st, chr('g'), d, 1000);
    TEST_ASSERT_FALSE(first.intent.valid);
    KeyResult second = handleKey(st, enterKey(), d, 1000);
    TEST_ASSERT_EQUAL_STRING("macro", second.intent.target);
    TEST_ASSERT_EQUAL_STRING("goodnight", second.intent.action);
}

void test_console_opens_and_runs_a_command(void) {
    Dash d = makeDash();
    UiState st;
    handleKey(st, chr('/'), d, 1000);
    TEST_ASSERT_TRUE(st.screen == Screen::Console);
    for (const char* p = "flur aus"; *p; ++p) handleKey(st, chr(*p), d, 1000);
    TEST_ASSERT_EQUAL_STRING("flur aus", st.input);
    KeyResult r = handleKey(st, enterKey(), d, 1000);
    TEST_ASSERT_TRUE(r.intent.valid);
    TEST_ASSERT_EQUAL(85, r.intent.arg);
    TEST_ASSERT_TRUE(st.screen == Screen::Home);
}

void test_console_fog_command_still_asks(void) {
    // Typing is fast, and a fast typo must not ignite anything.
    Dash d = makeDash();
    UiState st;
    handleKey(st, chr('/'), d, 1000);
    for (const char* p = "nebel"; *p; ++p) handleKey(st, chr(*p), d, 1000);
    KeyResult r = handleKey(st, enterKey(), d, 1000);
    TEST_ASSERT_FALSE(r.intent.valid);
    TEST_ASSERT_TRUE(st.confirming);
}

void test_console_backspace_and_escape(void) {
    Dash d = makeDash();
    UiState st;
    handleKey(st, chr('/'), d, 1000);
    for (const char* p = "abc"; *p; ++p) handleKey(st, chr(*p), d, 1000);
    Key del; del.del = true;
    handleKey(st, del, d, 1000);
    TEST_ASSERT_EQUAL_STRING("ab", st.input);
    handleKey(st, escKey(), d, 1000);
    TEST_ASSERT_TRUE(st.screen == Screen::Home);
    TEST_ASSERT_EQUAL_STRING("", st.input);
}

void test_tab_completes_a_room_name(void) {
    Dash d = makeDash();
    UiState st;
    handleKey(st, chr('/'), d, 1000);
    for (const char* p = "wohn"; *p; ++p) handleKey(st, chr(*p), d, 1000);
    Key tab; tab.tab = true;
    handleKey(st, tab, d, 1000);
    TEST_ASSERT_EQUAL_STRING("Wohnzimmer", st.input);
}

void test_console_input_cannot_overflow(void) {
    Dash d = makeDash();
    UiState st;
    handleKey(st, chr('/'), d, 1000);
    for (int i = 0; i < 200; ++i) handleKey(st, chr('x'), d, 1000);
    TEST_ASSERT_TRUE(st.inputLen < (int)sizeof(st.input));
    TEST_ASSERT_EQUAL(0, st.input[sizeof(st.input) - 1]);
}

void test_an_unknown_command_reports_instead_of_acting(void) {
    Dash d = makeDash();
    UiState st;
    handleKey(st, chr('/'), d, 1000);
    for (const char* p = "xyzzy"; *p; ++p) handleKey(st, chr(*p), d, 1000);
    KeyResult r = handleKey(st, enterKey(), d, 1000);
    TEST_ASSERT_FALSE(r.intent.valid);
    TEST_ASSERT_TRUE(st.screen == Screen::Console);      // stays, so you can fix it
    TEST_ASSERT_TRUE(toastVisible(st, 1000));
}

void test_teufel_transport_can_be_switched_to_ir(void) {
    Dash d = makeDash();
    UiState st;
    st.screen = Screen::Teufel;
    KeyResult before = handleKey(st, chr('+'), d, 1000);
    TEST_ASSERT_FALSE(before.viaIr);
    handleKey(st, chr('w'), d, 1000);
    KeyResult after = handleKey(st, chr('+'), d, 1000);
    TEST_ASSERT_TRUE(after.viaIr);
    TEST_ASSERT_TRUE(after.intent.valid);
}

void test_teufel_mute_warns_about_the_known_quirk(void) {
    // 0x28 reaches the box and does nothing. Saying so beats letting the user
    // conclude the remote is broken.
    Dash d = makeDash();
    UiState st;
    st.screen = Screen::Teufel;
    handleKey(st, chr('m'), d, 1000);
    TEST_ASSERT_TRUE(strstr(st.toast, "wirkungslos") != nullptr);
}

void test_a_toast_expires(void) {
    UiState st;
    toast(st, "hallo", 1000);
    TEST_ASSERT_TRUE(toastVisible(st, 1000 + kToastMs - 1));
    TEST_ASSERT_FALSE(toastVisible(st, 1000 + kToastMs));
}

void test_keys_that_mean_nothing_here_do_nothing(void) {
    Dash d = makeDash();
    UiState st;
    st.screen = Screen::Disco;
    KeyResult r = handleKey(st, chr('z'), d, 1000);
    TEST_ASSERT_FALSE(r.intent.valid);
    TEST_ASSERT_TRUE(st.screen == Screen::Disco);
}
