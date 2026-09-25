// =============================================================================
// test_spaceinput — the space bin's interaction vocabulary (docs/guests/SPACE.md)
// =============================================================================
// The button state machine and the gesture classification are pure, so the
// hardware session never has to discover that a long press also fired a short
// one, or that a swipe down stole SceGuest's exit.
// =============================================================================

#include <unity.h>
#include "../../firmware/space/input.h"

using namespace spa;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// ---- the ring closes on itself ----------------------------------------------
static void test_anneau_de_vues(void) {
    View v = View::Iss;
    v = nextView(v); TEST_ASSERT_TRUE(v == View::Passes);
    v = nextView(v); TEST_ASSERT_TRUE(v == View::Moon);
    v = nextView(v); TEST_ASSERT_TRUE(v == View::Sky);
    v = nextView(v); TEST_ASSERT_TRUE(v == View::Launches);
    v = nextView(v); TEST_ASSERT_TRUE(v == View::Iss);      // wraps
    TEST_ASSERT_EQUAL_STRING("MOON", viewName(View::Moon));
}

// =============================================================================
// The button BANK (sce::ButtonFsm via spa) — driven through THIS bin's
// vocabulary. The mechanics themselves are exercised exhaustively by
// test_input; what these cases pin is the CONSUMPTION: the bank's BtnEvent
// mapped through spa::buttonEvent, the boot priming, and the A+C chord that
// used to be impossible here (three independent single-button FSMs meant A+C
// fired TWO long actions, and a button held through boot fired ~700 ms in).
// =============================================================================

// Drives the bank from `now` for durMs with the given levels, stepping 10 ms
// the way loop() would, and collects every MAPPED event that fires.
struct Collected {
    UiEvent ev[8];
    int     n = 0;
};

static void feed(ButtonFsm& f, Collected& c, uint32_t& now, uint32_t durMs,
                 bool a, bool b = false, bool cc = false) {
    const bool lvl[3] = { a, b, cc };
    const uint32_t end = now + durMs;
    while (now < end) {
        BtnEvent e{};
        if (f.update(now, lvl, &e) && c.n < 8) c.ev[c.n++] = buttonEvent(e);
        now += 10;
    }
}

// Establishes the RESTING baseline before a test presses anything: the bank
// treats a button already down on its very first sample as one held through
// boot and swallows it. Real hardware always provides this settling time; a
// test that opens with `feed(..., true)` would be measuring the boot rule
// instead of the press it meant to test.
static void idle(ButtonFsm& f, Collected& c, uint32_t& now) {
    feed(f, c, now, 100, false, false, false);
}

// ---- a long press does NOT also fire the short one --------------------------
static void test_appui_long_ne_double_pas(void) {
    ButtonFsm fsm; Collected c; uint32_t now = 1000;
    idle(fsm, c, now);
    // Held under the threshold: nothing yet.
    feed(fsm, c, now, ButtonFsm::LONG_MS - 100, false, true);
    TEST_ASSERT_EQUAL_INT(0, c.n);
    // Crossing 700 ms fires the LONG event immediately (felt, not waited out).
    feed(fsm, c, now, 200, false, true);
    TEST_ASSERT_EQUAL_INT(1, c.n);
    TEST_ASSERT_TRUE(c.ev[0] == UiEvent::Refresh);        // B long
    // Holding longer must not repeat it, and the RELEASE must not then fire
    // the short event (NextView) on top.
    feed(fsm, c, now, 800, false, true);
    feed(fsm, c, now, 200, false, false);
    TEST_ASSERT_EQUAL_INT(1, c.n);
}

// ---- a short press fires on RELEASE -----------------------------------------
static void test_appui_court_au_relachement(void) {
    ButtonFsm fsm; Collected c; uint32_t now = 5000;
    idle(fsm, c, now);
    feed(fsm, c, now, 120, false, true);
    TEST_ASSERT_EQUAL_INT(0, c.n);              // nothing while it is down
    feed(fsm, c, now, 100, false, false);
    TEST_ASSERT_EQUAL_INT(1, c.n);
    TEST_ASSERT_TRUE(c.ev[0] == UiEvent::NextView);       // B short
}

// ---- contact bounce does not produce two presses ----------------------------
// Levels alternate every tick, far below DEBOUNCE_MS, so NOTHING settles.
static void test_anti_rebond(void) {
    ButtonFsm fsm; Collected c; uint32_t now = 9000;
    idle(fsm, c, now);
    const bool up[3]   = { false, false, false };
    const bool down[3] = { true,  false, false };
    for (int i = 0; i < 20; i++) {              // 200 ms of 10 ms chatter
        BtnEvent e{};
        if (fsm.update(now, (i & 1) ? down : up, &e) && c.n < 8)
            c.ev[c.n++] = buttonEvent(e);
        now += 10;
    }
    TEST_ASSERT_EQUAL_INT(0, c.n);
    // A genuine press after the chatter still gets through: the filter must
    // not be a wall.
    feed(fsm, c, now, ButtonFsm::DEBOUNCE_MS * 4, true);
    feed(fsm, c, now, ButtonFsm::DEBOUNCE_MS * 4, false);
    TEST_ASSERT_EQUAL_INT(1, c.n);
    TEST_ASSERT_TRUE(c.ev[0] == UiEvent::PrevItem);       // A short
}

// ---- a button held AT BOOT stays mute until released ------------------------
// The bug the three independent FSMs actually shipped with: a finger resting
// on a button while the Fire powers up fired its LONG action ~700 ms into
// boot — on A that opens the settings URL banner nobody asked for. The bank's
// priming swallows the press already in progress; the button speaks again
// after one release.
static void test_bouton_tenu_au_boot_reste_muet(void) {
    ButtonFsm fsm; Collected c; uint32_t now = 1000;
    feed(fsm, c, now, 3000, true, false, false);   // held through boot
    TEST_ASSERT_EQUAL_INT(0, c.n);                 // no long, no short
    feed(fsm, c, now, 200, false, false, false);   // released
    TEST_ASSERT_EQUAL_INT(0, c.n);                 // and still nothing
    // ...but the button WORKS from then on.
    feed(fsm, c, now, 200, true, false, false);
    feed(fsm, c, now, 200, false, false, false);
    TEST_ASSERT_EQUAL_INT(1, c.n);
    TEST_ASSERT_TRUE(c.ev[0] == UiEvent::PrevItem);
}

// ---- A+C held emits NO individual event -------------------------------------
// The debug overlay's contract: while the chord is held (and polled, as
// handleButtons does every tick BEFORE update), neither A nor C may speak —
// not their long presses under the overlay, not their short presses when the
// fingers come up. With the old per-button FSMs this was IMPOSSIBLE: A+C held
// 700 ms fired Settings AND Select, the double-fire the mission notes.
static void test_accord_sans_evenement_individuel(void) {
    ButtonFsm fsm; Collected c; uint32_t now = 1000;
    idle(fsm, c, now);
    // A then C, both down, held well past LONG_MS with the chord polled
    // every tick, exactly like handleButtons().
    feed(fsm, c, now, 60, true, false, false);
    feed(fsm, c, now, 60, true, false, true);
    for (int i = 0; i < 30; i++) {                 // ~1.5 s
        TEST_ASSERT_TRUE(fsm.chord(BTN_A, BTN_C));
        feed(fsm, c, now, 50, true, false, true);
    }
    // Release both.
    feed(fsm, c, now, 200, false, false, false);
    TEST_ASSERT_FALSE(fsm.chord(BTN_A, BTN_C));
    TEST_ASSERT_EQUAL_INT(0, c.n);                 // NOTHING emitted
}

// ---- after the chord is released, the buttons speak again -------------------
static void test_apres_l_accord_les_boutons_reparlent(void) {
    ButtonFsm fsm; Collected c; uint32_t now = 1000;
    idle(fsm, c, now);
    feed(fsm, c, now, 100, true, false, true);     // chord held...
    TEST_ASSERT_TRUE(fsm.chord(BTN_A, BTN_C));
    feed(fsm, c, now, 200, false, false, false);   // ...and released: silent
    TEST_ASSERT_EQUAL_INT(0, c.n);
    // A alone now works, short and long alike.
    feed(fsm, c, now, 100, true, false, false);
    feed(fsm, c, now, 100, false, false, false);
    TEST_ASSERT_EQUAL_INT(1, c.n);
    TEST_ASSERT_TRUE(c.ev[0] == UiEvent::PrevItem);
    feed(fsm, c, now, ButtonFsm::LONG_MS + 200, false, false, true);
    TEST_ASSERT_EQUAL_INT(2, c.n);
    TEST_ASSERT_TRUE(c.ev[1] == UiEvent::Select);  // C long
    feed(fsm, c, now, 200, false, false, false);
    TEST_ASSERT_EQUAL_INT(2, c.n);                 // release stays silent
}

// ---- la cartographie est UNIFORME, et surtout elle est COMPLETE -------------
// Une cartographie qui depend de l'ecran est une cartographie qu'il faut
// apprendre. Et l'ancienne avait un TROU : C court valait Select sur les deux
// vues a liste, qui se retrouvaient donc SANS pas en avant — le curseur ne
// pouvait etre parcouru qu'a l'envers, precisement sur les ecrans batis autour
// d'un curseur. Ce cas fixe les deux sens.
static void test_cartographie_uniforme(void) {
    TEST_ASSERT_TRUE(buttonEvent(Button::A, false) == UiEvent::PrevItem);
    TEST_ASSERT_TRUE(buttonEvent(Button::B, false) == UiEvent::NextView);
    TEST_ASSERT_TRUE(buttonEvent(Button::C, false) == UiEvent::NextItem);
    TEST_ASSERT_TRUE(buttonEvent(Button::A, true)  == UiEvent::Settings);
    TEST_ASSERT_TRUE(buttonEvent(Button::B, true)  == UiEvent::Refresh);
    TEST_ASSERT_TRUE(buttonEvent(Button::C, true)  == UiEvent::Select);
}

// Les deux sens du curseur existent, et ils sont sur des boutons DIFFERENTS :
// c'est exactement ce que l'ancienne cartographie avait perdu.
static void test_les_deux_sens_du_curseur(void) {
    UiEvent prev = buttonEvent(Button::A, false);
    UiEvent next = buttonEvent(Button::C, false);
    TEST_ASSERT_TRUE(prev == UiEvent::PrevItem);
    TEST_ASSERT_TRUE(next == UiEvent::NextItem);
    TEST_ASSERT_FALSE(prev == next);
    // Et les six actions du vocabulaire sont TOUTES atteignables au bouton,
    // sinon une carte sans dalle tactile a une fonction inaccessible.
    bool seen[8] = { false };
    for (int i = 0; i < 3; i++)
        for (int lp = 0; lp < 2; lp++)
            seen[(int)buttonEvent((Button)i, lp != 0)] = true;
    TEST_ASSERT_TRUE(seen[(int)UiEvent::PrevItem]);
    TEST_ASSERT_TRUE(seen[(int)UiEvent::NextItem]);
    TEST_ASSERT_TRUE(seen[(int)UiEvent::NextView]);
    TEST_ASSERT_TRUE(seen[(int)UiEvent::Refresh]);
    TEST_ASSERT_TRUE(seen[(int)UiEvent::Settings]);
    TEST_ASSERT_TRUE(seen[(int)UiEvent::Select]);
}

// ---- swipe DOWN stays SceGuest's, at every step -----------------------------
// The one gesture this bin must never claim: it is the way back to the
// companion on every guest bin (docs/guests/README.md).
static void test_swipe_bas_reste_a_sceguest(void) {
    TEST_ASSERT_TRUE(swipeEvent({ 0,  60 }, 40) == UiEvent::None);
    TEST_ASSERT_TRUE(swipeEvent({ 5, 120 }, 40) == UiEvent::None);
    TEST_ASSERT_TRUE(swipeEvent({ 0, 200 }, 40) == UiEvent::None);
}

// ...AND IT IS STILL A SWIPE. SceGuest only consumes the exit past 100 px, so
// between 40 and 100 a downward drag belongs to nobody: the caller used to
// read that None as "no gesture" and fall through to its tap branch, which
// resolves at the finger's ORIGIN and opened a pass or picked a planet. The
// two answers are what `isSwipe` tells apart.
static void test_swipe_bas_court_n_est_pas_un_tap(void) {
    TEST_ASSERT_TRUE(isSwipe({ 0,  60 }, 40));    // the dead-zone case
    TEST_ASSERT_TRUE(isSwipe({ 0,  99 }, 40));    // still under SceGuest's 100
    TEST_ASSERT_TRUE(isSwipe({ 0, 200 }, 40));
    // Every mapped swipe answers yes too — one classification, two questions.
    TEST_ASSERT_TRUE(isSwipe({ 0, -60 }, 40));
    TEST_ASSERT_TRUE(isSwipe({ -80, 5 }, 40));
    // A real tap does not.
    TEST_ASSERT_FALSE(isSwipe({ 10, 10 }, 40));
    TEST_ASSERT_FALSE(isSwipe({ 39, 39 }, 40));   // right below, both axes
    TEST_ASSERT_TRUE(isSwipe({ 40,  0 }, 40));    // exactly at it
}

// ---- the other swipes ------------------------------------------------------
static void test_swipes(void) {
    TEST_ASSERT_TRUE(swipeEvent({ 0, -60 }, 40) == UiEvent::NextView);
    TEST_ASSERT_TRUE(swipeEvent({ -80, 5 }, 40) == UiEvent::NextItem);
    TEST_ASSERT_TRUE(swipeEvent({  80, 5 }, 40) == UiEvent::PrevItem);
    // Below the threshold nothing happens — that is a tap, not a swipe.
    TEST_ASSERT_TRUE(swipeEvent({ 10, 10 }, 40) == UiEvent::None);
    // A diagonal resolves to its DOMINANT axis, and ties go vertical (the
    // ring is the primary navigation).
    TEST_ASSERT_TRUE(swipeEvent({ 50, -60 }, 40) == UiEvent::NextView);
    TEST_ASSERT_TRUE(swipeEvent({ 60, -50 }, 40) == UiEvent::PrevItem);
    TEST_ASSERT_TRUE(swipeEvent({ 50, -50 }, 40) == UiEvent::NextView);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_anneau_de_vues);
    RUN_TEST(test_swipe_bas_court_n_est_pas_un_tap);
    RUN_TEST(test_appui_long_ne_double_pas);
    RUN_TEST(test_appui_court_au_relachement);
    RUN_TEST(test_anti_rebond);
    RUN_TEST(test_bouton_tenu_au_boot_reste_muet);
    RUN_TEST(test_accord_sans_evenement_individuel);
    RUN_TEST(test_apres_l_accord_les_boutons_reparlent);
    RUN_TEST(test_cartographie_uniforme);
    RUN_TEST(test_les_deux_sens_du_curseur);
    RUN_TEST(test_swipe_bas_reste_a_sceguest);
    RUN_TEST(test_swipes);
    UNITY_END();
    return 0;
}
