// =============================================================================
// test_fluidinput — led-fluid's interaction vocabulary
// =============================================================================
// The Fire port's whole claim is that the same INTENT reaches two very
// different sets of hardware. That claim lives in one pure header, so it can be
// checked without a board: what a swipe means, what a button means, and that
// the cursor a button build carries can actually reach every row.
//
// The mechanics (debounce, boot priming, one event per call) are ButtonFsm's
// and are tested by test_input / test_spaceinput — rule 17, one implementation.
// What is tested here is only what is led-fluid's own.
// =============================================================================

#include <unity.h>
#include "../../firmware/led-fluid/input.h"

using namespace flu;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// ---- the swipes keep the meaning a CoreS3 user already knows ---------------
// The port must not cost anything to somebody holding the board it was written
// for. Right opens the physics panel, left the colours — the two gestures this
// bin shipped with.
static void test_les_glissements_gardent_leur_sens(void) {
    TEST_ASSERT_EQUAL(UiEvent::OpenSettings,
                      swipeEvent(View::Fluid, Swipe{ 90, 0 }));
    TEST_ASSERT_EQUAL(UiEvent::OpenColour,
                      swipeEvent(View::Fluid, Swipe{ -90, 0 }));
    // and from inside a panel, either one leaves it
    TEST_ASSERT_EQUAL(UiEvent::Back, swipeEvent(View::Settings, Swipe{ 90, 0 }));
    TEST_ASSERT_EQUAL(UiEvent::Back, swipeEvent(View::Settings, Swipe{ -90, 0 }));
    TEST_ASSERT_EQUAL(UiEvent::Back, swipeEvent(View::Colour,   Swipe{ 90, 0 }));
}

// ---- the downward swipe is NOT ours ----------------------------------------
// It belongs to SceGuest on every guest bin: it is how the robot goes back to
// the companion. A bin that claimed it would be a bin you cannot leave.
static void test_le_glissement_vers_le_bas_reste_a_sceguest(void) {
    TEST_ASSERT_EQUAL(UiEvent::None, swipeEvent(View::Fluid,    Swipe{ 0, 120 }));
    TEST_ASSERT_EQUAL(UiEvent::None, swipeEvent(View::Settings, Swipe{ 0, 120 }));
    TEST_ASSERT_EQUAL(UiEvent::None, swipeEvent(View::Colour,   Swipe{ 0, 120 }));
}

// ---- a tap is distinguishable from a swipe that meant nothing --------------
// Both come back as None, and the caller has to tell them apart: reading None
// as "not a swipe" turns a real drag into a tap AT THE FINGER'S ORIGIN, which
// pokes the liquid somewhere nobody pointed. `isSwipe` is the second question.
static void test_une_tape_ne_se_confond_pas_avec_un_glissement(void) {
    const Swipe tap{ 2, 3 }, up{ 0, -120 };
    TEST_ASSERT_FALSE(isSwipe(tap));
    TEST_ASSERT_TRUE(isSwipe(up));
    // an upward swipe means nothing HERE, but it is still a swipe
    TEST_ASSERT_EQUAL(UiEvent::None, swipeEvent(View::Fluid, up));
}

// ---- one button, one job ---------------------------------------------------
// The mapping does not depend on the view — that is the rule the other two bins
// follow, and the reason the table can be read at all. Asserted as a whole so
// that moving one entry has to be a deliberate edit here too.
static void test_chaque_bouton_a_un_seul_role(void) {
    TEST_ASSERT_EQUAL(UiEvent::Prev, buttonEvent(Button::A, false));
    TEST_ASSERT_EQUAL(UiEvent::Act,  buttonEvent(Button::B, false));
    TEST_ASSERT_EQUAL(UiEvent::Next, buttonEvent(Button::C, false));
    TEST_ASSERT_EQUAL(UiEvent::OpenColour,   buttonEvent(Button::A, true));
    TEST_ASSERT_EQUAL(UiEvent::Back,         buttonEvent(Button::B, true));
    TEST_ASSERT_EQUAL(UiEvent::OpenSettings, buttonEvent(Button::C, true));
}

// ---- every event is reachable from a button --------------------------------
// A Fire has no other input. An event nobody can produce is a feature that does
// not exist on that board, and it would be invisible: the code compiles, the
// screen draws, and one thing simply cannot be done.
static void test_tout_evenement_est_atteignable_au_bouton(void) {
    bool seen[8] = { false };
    for (int b = 0; b < 3; b++)
        for (int lng = 0; lng < 2; lng++)
            seen[(int)buttonEvent((Button)b, lng != 0)] = true;
    TEST_ASSERT_TRUE(seen[(int)UiEvent::Prev]);
    TEST_ASSERT_TRUE(seen[(int)UiEvent::Next]);
    TEST_ASSERT_TRUE(seen[(int)UiEvent::Act]);
    TEST_ASSERT_TRUE(seen[(int)UiEvent::Back]);
    TEST_ASSERT_TRUE(seen[(int)UiEvent::OpenSettings]);
    TEST_ASSERT_TRUE(seen[(int)UiEvent::OpenColour]);
}

// ---- the two tabs are not the same length ----------------------------------
// Seven physics sliders, six render switches. A cursor that assumed one number
// would either skip the last switch or point past the end of the shorter tab.
static void test_les_deux_onglets_n_ont_pas_la_meme_longueur(void) {
    TEST_ASSERT_EQUAL_INT(7, rowsOnTab(0));
    TEST_ASSERT_EQUAL_INT(6, rowsOnTab(1));
    // and the hidden tab is drawn by nobody, so a cursor never visits it
    TEST_ASSERT_EQUAL_INT(-1, paramOnTab(0, 7));
    TEST_ASSERT_EQUAL_INT(-1, paramOnTab(1, 6));
}

// ---- the cursor reaches every drawn row, and comes home ---------------------
// THE load-bearing test of the port. With no dedicated tab button, walking off
// the end of a tab IS the tab control — so if the wrap is wrong, a whole tab is
// unreachable on a board whose only input is these three buttons.
static void test_le_curseur_atteint_chaque_ligne_puis_revient(void) {
    const int total = rowsOnTab(0) + rowsOnTab(1);
    bool visited[2][8] = { { false } };
    Cursor c;
    for (int i = 0; i < total; i++) {
        TEST_ASSERT_TRUE(c.tab == 0 || c.tab == 1);
        TEST_ASSERT_TRUE(c.row >= 0 && c.row < (int8_t)rowsOnTab(c.tab));
        TEST_ASSERT_FALSE(visited[c.tab][c.row]);      // no row twice per lap
        visited[c.tab][c.row] = true;
        // every visited row is a real parameter, never a hole
        TEST_ASSERT_TRUE(paramOnTab(c.tab, c.row) >= 0);
        c = nextRow(c);
    }
    // a full lap lands exactly where it started
    TEST_ASSERT_EQUAL_UINT8(0, c.tab);
    TEST_ASSERT_EQUAL_INT8(0, c.row);
    for (uint8_t t = 0; t < 2; t++)
        for (int r = 0; r < rowsOnTab(t); r++)
            TEST_ASSERT_TRUE(visited[t][r]);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_les_glissements_gardent_leur_sens);
    RUN_TEST(test_le_glissement_vers_le_bas_reste_a_sceguest);
    RUN_TEST(test_une_tape_ne_se_confond_pas_avec_un_glissement);
    RUN_TEST(test_chaque_bouton_a_un_seul_role);
    RUN_TEST(test_tout_evenement_est_atteignable_au_bouton);
    RUN_TEST(test_les_deux_onglets_n_ont_pas_la_meme_longueur);
    RUN_TEST(test_le_curseur_atteint_chaque_ligne_puis_revient);
    return UNITY_END();
}
