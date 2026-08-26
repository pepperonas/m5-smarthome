// Key mapping — the layer that was "too thin to test" and held the bug that
// made every single key do nothing.
#include <unity.h>

#include <cstring>
#include <initializer_list>

#include "keymap.h"

using namespace core;

static KeyReport report(const char* word, bool changed = true,
                        int pressed = 1) {
    KeyReport r;
    r.changed = changed;
    r.pressedCount = pressed;
    r.word = word;
    r.wordLen = word ? strlen(word) : 0;
    return r;
}

void test_no_event_when_nothing_changed(void) {
    Key k;
    TEST_ASSERT_FALSE(mapKey(report(";", /*changed=*/false), k));
}

void test_no_event_when_no_key_is_down(void) {
    // A release also changes the count; it must not be read as a press.
    Key k;
    TEST_ASSERT_FALSE(mapKey(report(";", true, /*pressed=*/0), k));
}

void test_a_changed_press_is_an_event(void) {
    Key k;
    TEST_ASSERT_TRUE(mapKey(report(";"), k));
}

void test_the_printed_arrows_map_to_directions(void) {
    // The Cardputer has no arrow keys; they are printed on ; . , / and the
    // library reports those characters, with no Fn involved.
    Key k;
    mapKey(report(";"), k);
    TEST_ASSERT_TRUE(k.up);
    mapKey(report("."), k);
    TEST_ASSERT_TRUE(k.down);
    mapKey(report(","), k);
    TEST_ASSERT_TRUE(k.left);
    mapKey(report("/"), k);
    TEST_ASSERT_TRUE(k.right);
}

void test_slash_also_arrives_as_a_character(void) {
    // The home screen opens the console on '/', so it must be both.
    Key k;
    mapKey(report("/"), k);
    TEST_ASSERT_TRUE(k.right);
    TEST_ASSERT_EQUAL('/', k.ch);
}

void test_backtick_is_escape(void) {
    Key k;
    mapKey(report("`"), k);
    TEST_ASSERT_TRUE(k.esc);
    TEST_ASSERT_EQUAL(0, k.ch);
}

void test_enter_arrives_with_an_empty_word(void) {
    // The library handles Enter as a flag and pushes nothing into `word`.
    // Requiring a character here would have swallowed every Enter.
    KeyReport r = report("");
    r.enter = true;
    Key k;
    TEST_ASSERT_TRUE(mapKey(r, k));
    TEST_ASSERT_TRUE(k.enter);
}

void test_a_null_word_is_not_a_crash(void) {
    KeyReport r = report(nullptr);
    r.enter = true;
    Key k;
    TEST_ASSERT_TRUE(mapKey(r, k));
    TEST_ASSERT_TRUE(k.enter);
}

void test_ordinary_characters_pass_through(void) {
    Key k;
    for (const char* c : {"1", "7", "g", "a", "u", "w"}) {
        mapKey(report(c), k);
        TEST_ASSERT_EQUAL(c[0], k.ch);
    }
}

void test_digits_are_not_swallowed_by_the_arrow_mapping(void) {
    Key k;
    mapKey(report("3"), k);
    TEST_ASSERT_EQUAL('3', k.ch);
    TEST_ASSERT_FALSE(k.up || k.down || k.left || k.right);
}

void test_only_the_first_character_is_used(void) {
    Key k;
    mapKey(report(";x"), k);
    TEST_ASSERT_TRUE(k.up);
    TEST_ASSERT_EQUAL(0, k.ch);
}

void test_the_output_is_untouched_when_there_is_no_event(void) {
    // A caller that ignores the return value must not act on stale data.
    Key k;
    k.ch = 'Z';
    TEST_ASSERT_FALSE(mapKey(report("1", false), k));
    TEST_ASSERT_EQUAL('Z', k.ch);
}
