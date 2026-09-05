// The control lists: what each screen offers, read from the snapshot.
#include <unity.h>

#include <cstring>

#include "controls.h"
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

static const Control* find(const ControlList& l, Bind b) {
    for (int i = 0; i < l.count; ++i) if (l.items[i].bind == b) return &l.items[i];
    return nullptr;
}

void test_every_list_screen_builds_within_the_slot_limit(void) {
    Dash d = makeDash();
    UiState st;
    const Screen screens[] = {Screen::Home, Screen::Rooms, Screen::Room, Screen::Lichtwerk,
                              Screen::Yamaha, Screen::Teufel, Screen::Disco, Screen::Fog,
                              Screen::Climate};
    for (Screen s : screens) {
        ControlList l;
        st.roomId = 81;
        buildScreen(s, d, st, l);
        TEST_ASSERT_TRUE(l.count > 0);
        TEST_ASSERT_TRUE(l.count <= kMaxControls);
    }
}

void test_home_rows_carry_the_house_status(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l;
    buildScreen(Screen::Home, d, st, l);
    TEST_ASSERT_EQUAL(7, l.count);
    TEST_ASSERT_EQUAL_STRING("5 an", find(l, Bind::HomeRooms)->text);
    TEST_ASSERT_EQUAL_STRING("-28.0 Spotify", find(l, Bind::HomeYamaha)->text);
    TEST_ASSERT_EQUAL_STRING("aus  Tank 48%", find(l, Bind::HomeFog)->text);
    TEST_ASSERT_EQUAL_STRING("22.7 / 16.3 C", find(l, Bind::HomeClimate)->text);
}

void test_the_room_list_is_one_link_per_room(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l;
    buildScreen(Screen::Rooms, d, st, l);
    TEST_ASSERT_EQUAL(d.hue.count, l.count);
    TEST_ASSERT_TRUE(l.items[2].kind == ControlKind::Link);
    TEST_ASSERT_EQUAL_STRING("Kueche", l.items[2].label);
    TEST_ASSERT_EQUAL(83, l.items[2].key);
    TEST_ASSERT_EQUAL_STRING("100%", l.items[2].text);      // bri 254
    TEST_ASSERT_EQUAL_STRING("aus", l.items[5].text);       // Garten off
}

void test_the_room_screen_reads_its_room(void) {
    Dash d = makeDash();
    UiState st;
    st.roomId = 85;                                          // Flur, on, bri 24
    ControlList l;
    buildScreen(Screen::Room, d, st, l);
    TEST_ASSERT_EQUAL(1, find(l, Bind::RoomOn)->value);
    TEST_ASSERT_EQUAL(24, find(l, Bind::RoomBri)->value);
    TEST_ASSERT_EQUAL(85, find(l, Bind::RoomBri)->key);
}

void test_levels_and_choices_show_the_current_value(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l;
    buildScreen(Screen::Yamaha, d, st, l);
    TEST_ASSERT_EQUAL(-280, find(l, Bind::YamVol)->value);
    TEST_ASSERT_EQUAL_STRING("Spotify", find(l, Bind::YamInput)->text);
    TEST_ASSERT_EQUAL(0, find(l, Bind::YamMute)->value);

    buildScreen(Screen::Teufel, d, st, l);
    TEST_ASSERT_EQUAL_STRING("Netz", find(l, Bind::TfPath)->text);
    st.teufelUseIr = true;
    buildScreen(Screen::Teufel, d, st, l);
    TEST_ASSERT_EQUAL_STRING("IR (blind)", find(l, Bind::TfPath)->text);
    TEST_ASSERT_EQUAL(29, find(l, Bind::TfVol)->value);
    TEST_ASSERT_EQUAL(1, find(l, Bind::TfMute)->value);
}

void test_strip_controls_are_disabled_while_warn_owns_the_strip(void) {
    Dash d = makeDash();                     // lw.warn == true in the fixture
    UiState st;
    ControlList l;
    buildScreen(Screen::Lichtwerk, d, st, l);
    TEST_ASSERT_FALSE(find(l, Bind::LwEffect)->enabled);
    TEST_ASSERT_FALSE(find(l, Bind::LwBri)->enabled);
    TEST_ASSERT_TRUE(find(l, Bind::LwOn)->enabled);       // power still yours
    d.lw.warnOwned = false;
    buildScreen(Screen::Lichtwerk, d, st, l);
    TEST_ASSERT_TRUE(find(l, Bind::LwEffect)->enabled);
}

void test_readouts_are_not_selectable(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l;
    buildScreen(Screen::Fog, d, st, l);
    TEST_ASSERT_TRUE(selectable(l.items[0]));             // the toggle
    TEST_ASSERT_FALSE(selectable(*find(l, Bind::FogTank)));
    TEST_ASSERT_EQUAL_STRING("48% (120 ml)", find(l, Bind::FogTank)->text);
}

void test_accelerators_come_from_the_table(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l;
    buildScreen(Screen::Yamaha, d, st, l);
    TEST_ASSERT_EQUAL('m', find(l, Bind::YamMute)->accel);
    TEST_ASSERT_EQUAL('i', find(l, Bind::YamInput)->accel);
    TEST_ASSERT_EQUAL(0, find(l, Bind::YamVol)->accel);
}

void test_the_cursor_skips_readouts_and_wraps(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l;
    buildScreen(Screen::Fog, d, st, l);          // Toggle, Readout, Readout
    TEST_ASSERT_EQUAL(0, firstSelectable(l));
    TEST_ASSERT_EQUAL(0, nextSelectable(l, 0, +1));   // nothing else selectable: stays
    TEST_ASSERT_EQUAL(0, nextSelectable(l, 0, -1));

    buildScreen(Screen::Yamaha, d, st, l);       // four selectables
    TEST_ASSERT_EQUAL(1, nextSelectable(l, 0, +1));
    TEST_ASSERT_EQUAL(3, nextSelectable(l, 0, -1));   // wraps to the last
    TEST_ASSERT_EQUAL(0, nextSelectable(l, 3, +1));
}

void test_a_screen_of_readouts_has_no_cursor(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l;
    buildScreen(Screen::Climate, d, st, l);
    TEST_ASSERT_EQUAL(-1, firstSelectable(l));
}

void test_scrolling_keeps_the_cursor_visible(void) {
    ControlList l;
    l.count = 12;
    l.visibleRows = 7;
    for (int i = 0; i < l.count; ++i) l.items[i].kind = ControlKind::Toggle;
    TEST_ASSERT_EQUAL(0, firstVisible(l, 0, 0));
    TEST_ASSERT_EQUAL(0, firstVisible(l, 6, 0));      // still on screen
    TEST_ASSERT_EQUAL(1, firstVisible(l, 7, 0));      // one past the bottom
    TEST_ASSERT_EQUAL(5, firstVisible(l, 11, 1));     // last row
    TEST_ASSERT_EQUAL(2, firstVisible(l, 2, 5));      // cursor above the window
}

void test_the_primary_toggle_is_the_first_toggle(void) {
    // Every real screen happens to lead with its power switch, so a synthetic
    // list is what proves the search actually looks past row 0 rather than
    // returning the first selectable row.
    ControlList l;
    l.count = 3;
    l.items[0].kind = ControlKind::Choice;
    l.items[1].kind = ControlKind::Level;
    l.items[2].kind = ControlKind::Toggle;
    TEST_ASSERT_EQUAL(2, primaryToggle(l));

    Dash d = makeDash();
    UiState st;
    buildScreen(Screen::Teufel, d, st, l);       // the real one leads with power
    TEST_ASSERT_EQUAL(0, primaryToggle(l));
    TEST_ASSERT_TRUE(l.items[0].bind == Bind::TfOn);
    buildScreen(Screen::Climate, d, st, l);
    TEST_ASSERT_EQUAL(-1, primaryToggle(l));     // all readouts
}

void test_accelerators_find_their_control(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l;
    buildScreen(Screen::Yamaha, d, st, l);
    TEST_ASSERT_EQUAL(3, findAccel(l, 'm'));
    TEST_ASSERT_EQUAL(-1, findAccel(l, 'z'));
}

void test_the_cursor_can_start_from_nowhere(void) {
    // firstSelectable() returns -1 on a screen of readouts, and that -1 is
    // what the caller then holds as its cursor. Moving from there must not
    // invent a selection out of nothing.
    Dash d = makeDash();
    UiState st;
    ControlList l;
    buildScreen(Screen::Climate, d, st, l);          // five readouts
    TEST_ASSERT_EQUAL(-1, nextSelectable(l, -1, +1));
    TEST_ASSERT_EQUAL(-1, nextSelectable(l, -1, -1));

    buildScreen(Screen::Yamaha, d, st, l);           // four selectable rows
    TEST_ASSERT_EQUAL(0, nextSelectable(l, -1, +1)); // forward lands on the first
}

// --------------------------------------------------------- adjust/activate

static ControlList listFor(Screen s, const Dash& d, UiState& st) {
    ControlList l;
    buildScreen(s, d, st, l);
    return l;
}

static int idx(const ControlList& l, Bind b) {
    for (int i = 0; i < l.count; ++i) if (l.items[i].bind == b) return i;
    return -1;
}

void test_a_level_steps_and_clamps(void) {
    Dash d = makeDash();
    UiState st;
    st.roomId = 83;                                          // Kueche, bri 254
    ControlList l = listFor(Screen::Room, d, st);
    KeyResult up = adjust(l, idx(l, Bind::RoomBri), +1, d, st, 1000);
    TEST_ASSERT_TRUE(up.intent.valid);
    TEST_ASSERT_EQUAL_STRING("bri", up.intent.action);
    TEST_ASSERT_EQUAL(83, up.intent.arg);
    TEST_ASSERT_EQUAL(254, up.intent.arg2);                  // clamped, not 284
    TEST_ASSERT_EQUAL_STRING("Kueche 100%", up.intent.label);

    st.roomId = 85;                                          // Flur, bri 24
    l = listFor(Screen::Room, d, st);
    KeyResult dn = adjust(l, idx(l, Bind::RoomBri), -1, d, st, 1000);
    TEST_ASSERT_EQUAL(1, dn.intent.arg2);                    // never 0: that is "off"
}

void test_the_room_list_adjusts_brightness_straight_from_the_row(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l = listFor(Screen::Rooms, d, st);
    KeyResult r = adjust(l, 4, +1, d, st, 1000);            // Flur row
    TEST_ASSERT_EQUAL_STRING("hue", r.intent.target);
    TEST_ASSERT_EQUAL_STRING("bri", r.intent.action);
    TEST_ASSERT_EQUAL(85, r.intent.arg);
    TEST_ASSERT_EQUAL(54, r.intent.arg2);                    // 24 + 30
}

void test_receiver_volume_is_a_step_and_stops_at_the_ceiling(void) {
    Dash d = makeDash();                                     // raw -280
    UiState st;
    ControlList l = listFor(Screen::Yamaha, d, st);
    KeyResult r = adjust(l, idx(l, Bind::YamVol), +1, d, st, 1000);
    TEST_ASSERT_EQUAL_STRING("vol", r.intent.action);
    TEST_ASSERT_EQUAL(2, r.intent.arg);                      // +1 dB = 2 raw steps
    TEST_ASSERT_EQUAL_STRING("-27.0 dB", r.intent.label);

    d.yam.raw = -200;                                        // at the top
    l = listFor(Screen::Yamaha, d, st);
    r = adjust(l, idx(l, Bind::YamVol), +1, d, st, 1000);
    TEST_ASSERT_FALSE(r.intent.valid);                       // nothing past max
}

void test_a_choice_cycles_from_what_is_shown(void) {
    Dash d = makeDash();
    strcpy(d.yam.input, "HDMI2");
    UiState st;
    ControlList l = listFor(Screen::Yamaha, d, st);
    KeyResult r = adjust(l, idx(l, Bind::YamInput), +1, d, st, 1000);
    TEST_ASSERT_EQUAL_STRING("input", r.intent.action);
    TEST_ASSERT_EQUAL_STRING("HDMI3", r.intent.name);
    r = adjust(l, idx(l, Bind::YamInput), -1, d, st, 1000);
    TEST_ASSERT_EQUAL_STRING("HDMI1", r.intent.name);
}

void test_a_disabled_control_refuses_with_a_toast(void) {
    Dash d = makeDash();                                     // warnOwned
    UiState st;
    ControlList l = listFor(Screen::Lichtwerk, d, st);
    KeyResult r = adjust(l, idx(l, Bind::LwEffect), +1, d, st, 1000);
    TEST_ASSERT_FALSE(r.intent.valid);
    TEST_ASSERT_EQUAL_STRING("Strip-Warn aktiv", st.toast);
}

void test_the_teufel_path_is_local(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l = listFor(Screen::Teufel, d, st);
    KeyResult r = adjust(l, idx(l, Bind::TfPath), +1, d, st, 1000);
    TEST_ASSERT_FALSE(r.intent.valid);
    TEST_ASSERT_TRUE(st.teufelUseIr);
    l = listFor(Screen::Teufel, d, st);
    KeyResult v = adjust(l, idx(l, Bind::TfVol), +1, d, st, 1000);
    TEST_ASSERT_TRUE(v.viaIr);                               // now blind
    TEST_ASSERT_EQUAL_STRING("tf", v.intent.target);
}

void test_toggles_flip_from_the_current_state(void) {
    Dash d = makeDash();                                     // yam on, tf mute on
    UiState st;
    ControlList l = listFor(Screen::Yamaha, d, st);
    KeyResult r = activate(l, idx(l, Bind::YamOn), d, st, 1000);
    TEST_ASSERT_EQUAL_STRING("off", r.intent.action);
    d.yam.on = false;
    l = listFor(Screen::Yamaha, d, st);
    r = activate(l, idx(l, Bind::YamOn), d, st, 1000);
    TEST_ASSERT_EQUAL_STRING("on", r.intent.action);

    l = listFor(Screen::Teufel, d, st);
    r = activate(l, idx(l, Bind::TfMute), d, st, 1000);
    TEST_ASSERT_EQUAL_STRING("mute", r.intent.action);
    TEST_ASSERT_EQUAL_STRING("Mute: bekannt wirkungslos", st.toast);
}

void test_enter_on_a_level_or_choice_does_nothing(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l = listFor(Screen::Yamaha, d, st);
    TEST_ASSERT_FALSE(activate(l, idx(l, Bind::YamVol), d, st, 1000).intent.valid);
    TEST_ASSERT_FALSE(activate(l, idx(l, Bind::YamInput), d, st, 1000).intent.valid);
}

void test_links_navigate(void) {
    Dash d = makeDash();
    UiState st;
    ControlList l = listFor(Screen::Home, d, st);
    activate(l, idx(l, Bind::HomeYamaha), d, st, 1000);
    TEST_ASSERT_TRUE(st.screen == Screen::Yamaha);
    TEST_ASSERT_EQUAL(0, st.cursor);

    st.screen = Screen::Rooms;
    l = listFor(Screen::Rooms, d, st);
    activate(l, 2, d, st, 1000);                             // Kueche
    TEST_ASSERT_TRUE(st.screen == Screen::Room);
    TEST_ASSERT_EQUAL(83, st.roomId);
}

void test_fog_on_asks_and_fog_off_does_not(void) {
    Dash d = makeDash();                                     // fog off
    UiState st;
    ControlList l = listFor(Screen::Fog, d, st);
    KeyResult r = activate(l, idx(l, Bind::FogOn), d, st, 1000);
    TEST_ASSERT_FALSE(r.intent.valid);
    TEST_ASSERT_TRUE(st.confirming);
    TEST_ASSERT_EQUAL_STRING("on", st.pending.action);

    st = UiState();
    d.fog.on = true;
    l = listFor(Screen::Fog, d, st);
    r = activate(l, idx(l, Bind::FogOn), d, st, 1000);
    TEST_ASSERT_EQUAL_STRING("off", r.intent.action);
    TEST_ASSERT_FALSE(st.confirming);
}

void test_space_toggles_the_highlighted_room(void) {
    Dash d = makeDash();
    UiState st;
    KeyResult r = toggleRoom(d, 81, st, 1000);               // Wohnzimmer, on
    TEST_ASSERT_EQUAL_STRING("off", r.intent.action);
    TEST_ASSERT_EQUAL(81, r.intent.arg);
    TEST_ASSERT_EQUAL_STRING("Wohnzimmer aus", r.intent.label);
}

void test_escape_goes_up_one_level(void) {
    TEST_ASSERT_TRUE(parentScreen(Screen::Room) == Screen::Rooms);
    TEST_ASSERT_TRUE(parentScreen(Screen::Rooms) == Screen::Home);
    TEST_ASSERT_TRUE(parentScreen(Screen::Yamaha) == Screen::Home);
    TEST_ASSERT_TRUE(parentScreen(Screen::Home) == Screen::Home);
}
