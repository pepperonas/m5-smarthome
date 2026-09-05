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
