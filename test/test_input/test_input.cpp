// =============================================================================
// test_input - the button backend of flight-radar (M5Stack Fire)
// =============================================================================
// The Fire has three buttons and NO touchscreen, so every gesture this bin was
// built on had to be re-expressed. The state machine that does it is pure
// (firmware/flight-radar/input.h), and it is worth testing for one blunt
// reason: on target, a short/long press bug looks exactly like a flaky button.
// You press, nothing happens or the wrong thing happens, and you blame the
// switch. Here it fails on a PC in a millisecond.
//
// The case that motivates most of this file is the double-fire: a long press
// must NOT also emit a short press when the finger finally comes up. Every
// naive implementation ships with that bug, and on this UI it would mean
// "force a refresh" followed immediately by "next view" - the refresh landing
// on a screen you are no longer looking at.
//
// Run: pio test -e native
// =============================================================================
#include <unity.h>
#include "../../firmware/flight-radar/input.h"

using namespace fr;

void setUp(void) {}
void tearDown(void) {}

// ------------------------------------------------------------------ helpers
// Drives the FSM from t0 to t0+durMs with the given levels, stepping 10 ms at
// a time the way loop() would, and collects everything that fires.
struct Collected {
    BtnEvent ev[8];
    int      n = 0;
};

static void feed(ButtonFsm& f, Collected& c, uint32_t& now, uint32_t durMs,
                 bool a, bool b = false, bool cc = false) {
    const bool lvl[3] = { a, b, cc };
    // `< end`, not `<= end`: with the inclusive form every stated duration was
    // silently one tick longer than written, which is the kind of slack that
    // makes a threshold test pass for the wrong reason.
    const uint32_t end = now + durMs;
    while (now < end) {
        BtnEvent e{};
        if (f.update(now, lvl, &e) && c.n < 8) c.ev[c.n++] = e;
        now += 10;
    }
}

// Establishes the RESTING baseline before a test presses anything.
//
// Not ceremony: the FSM treats a button that is already down on its very first
// sample as one held through boot and deliberately swallows it, because those
// two situations are indistinguishable from the inside. Real hardware always
// provides this — loop() runs for hundreds of milliseconds before a finger
// arrives — but a test that opens with `feed(..., true)` does not, and would
// be measuring the boot rule instead of the press it meant to test.
static void idle(ButtonFsm& f, Collected& c, uint32_t& now) {
    feed(f, c, now, 100, false, false, false);
}

// ONE update() call, with the three levels given. This is what `feed` cannot
// express: a collision needs two buttons changing in the SAME call, and every
// `feed`-based sequence separates them by at least one tick.
static void tick(ButtonFsm& f, Collected& c, uint32_t& now,
                 bool a, bool b, bool cc) {
    const bool lvl[3] = { a, b, cc };
    BtnEvent e{};
    if (f.update(now, lvl, &e) && c.n < 8) c.ev[c.n++] = e;
    now += 10;
}

// ------------------------------------------------------------- short press
// A press well under the threshold fires ONCE, on RELEASE, as short.
static void test_appui_court_une_seule_fois_au_relachement(void) {
    ButtonFsm f; Collected c; uint32_t now = 1000;
    idle(f, c, now);
    feed(f, c, now, 200, true);     // held 200 ms
    TEST_ASSERT_EQUAL_INT(0, c.n);  // nothing while it is down
    feed(f, c, now, 100, false);    // released
    TEST_ASSERT_EQUAL_INT(1, c.n);
    TEST_ASSERT_EQUAL_UINT8(BTN_A, c.ev[0].btn);
    TEST_ASSERT_FALSE(c.ev[0].isLong);
}

// ------------------------------------------------------------- long press
// Fires WHILE STILL HELD - the screen must answer before the finger lifts.
static void test_appui_long_pendant_le_maintien(void) {
    ButtonFsm f; Collected c; uint32_t now = 1000;
    idle(f, c, now);
    feed(f, c, now, ButtonFsm::LONG_MS - 100, false, true);
    TEST_ASSERT_EQUAL_INT(0, c.n);
    feed(f, c, now, 200, false, true);          // crosses the threshold
    TEST_ASSERT_EQUAL_INT(1, c.n);
    TEST_ASSERT_EQUAL_UINT8(BTN_B, c.ev[0].btn);
    TEST_ASSERT_TRUE(c.ev[0].isLong);
}

// THE regression this file exists for: the release after a long press must
// stay silent. Otherwise B-long = refresh is immediately followed by
// B-short = next view, and the refresh you asked for lands off-screen.
static void test_le_relachement_apres_un_long_ne_refire_pas(void) {
    ButtonFsm f; Collected c; uint32_t now = 1000;
    idle(f, c, now);
    feed(f, c, now, 1200, false, true);    // long fires here
    TEST_ASSERT_EQUAL_INT(1, c.n);
    feed(f, c, now, 300, false, false);    // release
    TEST_ASSERT_EQUAL_INT(1, c.n);         // still ONE event
    TEST_ASSERT_TRUE(c.ev[0].isLong);
}

// And a long press fires only ONCE however long it is held: no repeat.
static void test_maintien_prolonge_ne_repete_pas(void) {
    ButtonFsm f; Collected c; uint32_t now = 1000;
    idle(f, c, now);
    feed(f, c, now, 5000, false, false, true);
    TEST_ASSERT_EQUAL_INT(1, c.n);
    TEST_ASSERT_EQUAL_UINT8(BTN_C, c.ev[0].btn);
}

// --------------------------------------------------------------- debounce
// A contact that chatters for a few ms must not read as several presses.
// Levels alternate every tick, far below DEBOUNCE_MS, so NOTHING settles.
static void test_rebond_mecanique_ignore(void) {
    ButtonFsm f; Collected c; uint32_t now = 1000;
    idle(f, c, now);
    const bool up[3]   = { false, false, false };
    const bool down[3] = { true,  false, false };
    for (int i = 0; i < 20; i++) {           // 200 ms of 10 ms chatter
        BtnEvent e{};
        if (f.update(now, (i & 1) ? down : up, &e) && c.n < 8) c.ev[c.n++] = e;
        now += 10;
    }
    TEST_ASSERT_EQUAL_INT(0, c.n);
}

// A real press still gets through the debounce: the filter must not be a wall.
static void test_le_filtre_laisse_passer_un_vrai_appui(void) {
    ButtonFsm f; Collected c; uint32_t now = 1000;
    idle(f, c, now);
    feed(f, c, now, ButtonFsm::DEBOUNCE_MS * 4, true);
    feed(f, c, now, ButtonFsm::DEBOUNCE_MS * 4, false);
    TEST_ASSERT_EQUAL_INT(1, c.n);
    TEST_ASSERT_FALSE(c.ev[0].isLong);
}

// ---------------------------------------------------------------- arming
// The on-screen prompt arms partway through the hold, and stops arming once
// the long press has actually fired (otherwise the banner would stay lit).
static void test_armement_du_bandeau(void) {
    ButtonFsm f; Collected c; uint32_t now = 1000;
    idle(f, c, now);
    feed(f, c, now, 100, false, true);
    TEST_ASSERT_FALSE(f.arming(now, BTN_B, 250));   // too early
    feed(f, c, now, 250, false, true);
    TEST_ASSERT_TRUE(f.arming(now, BTN_B, 250));    // armed
    feed(f, c, now, 500, false, true);              // long fires
    TEST_ASSERT_EQUAL_INT(1, c.n);
    TEST_ASSERT_FALSE(f.arming(now, BTN_B, 250));   // no longer arming
}

// ---------------------------------------------------- the mapping itself
// The table is the contract with the user, so it is asserted literally.
//
// THREE SCREENS AND NOT TWO since 08-03, and the third is the whole point of
// this section: `buttonEvent` used to take a bool, so it answered PagePrev /
// PageNext for every screen that was not the radar - including the METAR and
// the TAF, which have no pages. These very tests asserted that, and they were
// asserting a promise the firmware did not keep: applyEvent() dropped the
// event unless `viewLevel == VIEW_NOTAM`, so A and C were dead keys on two
// screens out of four while the table said otherwise. A pure function that
// only knows "radar / not radar" CANNOT tell the truth here - which is why
// the fix was the signature, not the wording.
static void test_table_radar(void) {
    const Screen s = Screen::Radar;
    TEST_ASSERT_TRUE(UiEvent::TrackPrev == buttonEvent({BTN_A, false}, s));
    TEST_ASSERT_TRUE(UiEvent::NextView  == buttonEvent({BTN_B, false}, s));
    TEST_ASSERT_TRUE(UiEvent::TrackNext == buttonEvent({BTN_C, false}, s));
    TEST_ASSERT_TRUE(UiEvent::NetInfo   == buttonEvent({BTN_A, true},  s));
    TEST_ASSERT_TRUE(UiEvent::Refresh   == buttonEvent({BTN_B, true},  s));
    TEST_ASSERT_TRUE(UiEvent::ZoomCycle == buttonEvent({BTN_C, true},  s));
}

// The NOTAM deck, and ONLY it: paging is offered where there are cards.
static void test_table_pont_de_cartes(void) {
    const Screen s = Screen::Deck;
    TEST_ASSERT_TRUE(UiEvent::PagePrev == buttonEvent({BTN_A, false}, s));
    TEST_ASSERT_TRUE(UiEvent::NextView == buttonEvent({BTN_B, false}, s));
    TEST_ASSERT_TRUE(UiEvent::PageNext == buttonEvent({BTN_C, false}, s));
    TEST_ASSERT_TRUE(UiEvent::NetInfo  == buttonEvent({BTN_A, true},  s));
    TEST_ASSERT_TRUE(UiEvent::Refresh  == buttonEvent({BTN_B, true},  s));
    // No radius to cycle away from the radar - and NOT silently mapped to
    // something else, because a button that does an unrelated thing depending
    // on the screen is worse than one that does nothing.
    TEST_ASSERT_TRUE(UiEvent::None     == buttonEvent({BTN_C, true},  s));
}

// METAR, TAF, and a NOTAM screen with nothing in force: one card, no deck.
// A and C mean "back to the radar" here, which is what the touch path already
// answers to every gesture on a reading screen that is not a page turn.
// THE ASSERTION THAT MATTERS: they are NOT PagePrev/PageNext. That is the
// promise the table used to make and the firmware used to drop.
static void test_table_ecran_de_lecture_sans_pages(void) {
    const Screen s = Screen::Reading;
    TEST_ASSERT_TRUE(UiEvent::Back     == buttonEvent({BTN_A, false}, s));
    TEST_ASSERT_TRUE(UiEvent::NextView == buttonEvent({BTN_B, false}, s));
    TEST_ASSERT_TRUE(UiEvent::Back     == buttonEvent({BTN_C, false}, s));
    TEST_ASSERT_TRUE(UiEvent::NetInfo  == buttonEvent({BTN_A, true},  s));
    TEST_ASSERT_TRUE(UiEvent::Refresh  == buttonEvent({BTN_B, true},  s));
    TEST_ASSERT_TRUE(UiEvent::None     == buttonEvent({BTN_C, true},  s));
}

// NO SCREEN PAGES A DECK IT IS NOT ON. Stated as its own test because it is
// the constraint that outlives the table: whatever A and C come to mean on a
// page-less screen, they must never move the NOTAM index there.
static void test_pas_de_pagination_hors_du_pont(void) {
    const Screen noDeck[2] = { Screen::Radar, Screen::Reading };
    for (const Screen s : noDeck) {
        TEST_ASSERT_FALSE(UiEvent::PagePrev == buttonEvent({BTN_A, false}, s));
        TEST_ASSERT_FALSE(UiEvent::PageNext == buttonEvent({BTN_C, false}, s));
    }
}

// B must reach the radar from anywhere: it is the only screen-INDEPENDENT key,
// and on the deck it is the only way out at all (A and C page there). If B
// ever stopped being screen-independent, the Fire could strand a user.
static void test_b_est_la_sortie_universelle(void) {
    const Screen all[3] = { Screen::Radar, Screen::Deck, Screen::Reading };
    for (const Screen s : all)
        TEST_ASSERT_TRUE(UiEvent::NextView == buttonEvent({BTN_B, false}, s));
}

// ------------------------------------------------- independence of buttons
// Pressing A then C without releasing A must yield both, not one swallowed.
static void test_deux_boutons_ne_s_avalent_pas(void) {
    ButtonFsm f; Collected c; uint32_t now = 1000;
    idle(f, c, now);
    feed(f, c, now, 100, true, false, false);          // A down
    feed(f, c, now, 100, true, false, true);           // C down too
    feed(f, c, now, 100, true, false, false);          // C up -> C short
    feed(f, c, now, 100, false, false, false);         // A up -> A short
    TEST_ASSERT_EQUAL_INT(2, c.n);
    TEST_ASSERT_EQUAL_UINT8(BTN_C, c.ev[0].btn);
    TEST_ASSERT_EQUAL_UINT8(BTN_A, c.ev[1].btn);
}

// THE COLLISION, and the reason the test above was not enough: its two
// releases were 110 ms apart, so they never landed in the same update() call
// and the one-event-per-call rule was never exercised. Released TOGETHER, the
// first version of the FSM emitted A and lost C FOREVER — it cleared C's
// `down`/`settled` while refusing to report it, after which the level matched
// the stored one and no branch ever ran again.
// loop() reaches 50-100 ms on a redraw frame, which is exactly how two presses
// a human made separately arrive in one sample.
static void test_deux_relachements_simultanes_aucun_perdu(void) {
    ButtonFsm f; Collected c; uint32_t now = 1000;
    idle(f, c, now);
    feed(f, c, now, 200, true, false, true);           // A and C both held
    TEST_ASSERT_EQUAL_INT(0, c.n);
    // Both go up in the SAME tick, then time passes with nothing pressed.
    tick(f, c, now, false, false, false);
    feed(f, c, now, 200, false, false, false);
    TEST_ASSERT_EQUAL_INT(2, c.n);                     // BOTH, not one
    TEST_ASSERT_FALSE(c.ev[0].isLong);
    TEST_ASSERT_FALSE(c.ev[1].isLong);
    // Order is not part of the contract; the SET is.
    TEST_ASSERT_TRUE((c.ev[0].btn == BTN_A && c.ev[1].btn == BTN_C) ||
                     (c.ev[0].btn == BTN_C && c.ev[1].btn == BTN_A));
}

// Same rule for a long press colliding with another button's release: the
// long fires, and the short is deferred rather than dropped.
static void test_long_et_relachement_dans_le_meme_tick(void) {
    ButtonFsm f; Collected c; uint32_t now = 1000;
    idle(f, c, now);
    // A held towards its long press; B tapped so that it comes up right as
    // A crosses the threshold.
    feed(f, c, now, ButtonFsm::LONG_MS - 100, true, true, false);
    tick(f, c, now, true, false, false);               // B up, A still down
    feed(f, c, now, 300, true, false, false);          // A crosses LONG_MS
    TEST_ASSERT_EQUAL_INT(2, c.n);
    bool sawLongA = false, sawShortB = false;
    for (int i = 0; i < c.n; i++) {
        if (c.ev[i].btn == BTN_A && c.ev[i].isLong)  sawLongA  = true;
        if (c.ev[i].btn == BTN_B && !c.ev[i].isLong) sawShortB = true;
    }
    TEST_ASSERT_TRUE(sawLongA);
    TEST_ASSERT_TRUE(sawShortB);
}

// A release SAMPLED just before the threshold but still inside its debounce
// window must yield the SHORT press the user meant — not the long action the
// hold was released to avoid (review 08-04). The raw level is already low
// when the threshold tick arrives; only the BELIEVED state lags. The long
// check must read the raw level, or it fires on a press that has ended and
// the believed release is then swallowed as its echo.
static void test_relachement_en_fenetre_de_rebond_reste_court(void) {
    ButtonFsm f; Collected c; uint32_t now = 1000;
    idle(f, c, now);
    // Believed press lands ~30 ms into the hold, so the long threshold sits
    // at raw+730: a raw release at +720 leaves two ticks where down is still
    // believed, the threshold is crossed, and the release is not yet trusted.
    feed(f, c, now, ButtonFsm::LONG_MS + 20, true);
    feed(f, c, now, 100, false);
    TEST_ASSERT_EQUAL_INT(1, c.n);
    TEST_ASSERT_EQUAL_UINT8(BTN_A, c.ev[0].btn);
    TEST_ASSERT_FALSE(c.ev[0].isLong);      // court, pas long
}

// A button already held when the board powers up must NOT act. Otherwise a
// finger resting on C through boot cycles the radius and writes the yaml, and
// on B it forces a fetch — neither of which anyone asked for.
static void test_bouton_tenu_au_demarrage_est_ignore(void) {
    ButtonFsm f; Collected c; uint32_t now = 1000;
    feed(f, c, now, 3000, false, false, true);         // held through boot
    TEST_ASSERT_EQUAL_INT(0, c.n);                     // no long, no short
    feed(f, c, now, 200, false, false, false);         // released
    TEST_ASSERT_EQUAL_INT(0, c.n);                     // and still nothing
    // ...but the button WORKS from then on.
    feed(f, c, now, 200, false, false, true);
    feed(f, c, now, 200, false, false, false);
    TEST_ASSERT_EQUAL_INT(1, c.n);
    TEST_ASSERT_EQUAL_UINT8(BTN_C, c.ev[0].btn);
}


// ------------------------------------------------------------------ chord
// A+C held = the debug overlay. The chord must SWALLOW both buttons: letting
// go of A and C after a chord must not also page the aircraft list.
static void test_accord_avale_les_deux_boutons(void) {
    ButtonFsm f; Collected c; uint32_t now = 1000;
    idle(f, c, now);
    // A then C, both down.
    feed(f, c, now, 60, true, false, false);
    feed(f, c, now, 60, true, false, true);
    TEST_ASSERT_TRUE(f.chord(BTN_A, BTN_C));
    feed(f, c, now, 200, true, false, true);
    TEST_ASSERT_TRUE(f.chord(BTN_A, BTN_C));
    // Release both.
    feed(f, c, now, 200, false, false, false);
    TEST_ASSERT_FALSE(f.chord(BTN_A, BTN_C));
    TEST_ASSERT_EQUAL_INT(0, c.n);          // NOTHING emitted
}

// Held long enough to cross LONG_MS, the chord must still emit nothing -
// otherwise A-long (the IP banner) would fire under the debug overlay.
static void test_accord_tenu_ne_declenche_pas_de_long(void) {
    ButtonFsm f; Collected c; uint32_t now = 1000;
    idle(f, c, now);
    feed(f, c, now, 60, true, false, true);
    for (int i = 0; i < 30; i++) {          // ~1,5 s, chord polled each tick
        f.chord(BTN_A, BTN_C);
        feed(f, c, now, 50, true, false, true);
    }
    feed(f, c, now, 200, false, false, false);
    TEST_ASSERT_EQUAL_INT(0, c.n);
}

// One button alone is NOT a chord, and must keep working normally.
static void test_un_seul_bouton_n_est_pas_un_accord(void) {
    ButtonFsm f; Collected c; uint32_t now = 1000;
    idle(f, c, now);
    feed(f, c, now, 100, true, false, false);
    TEST_ASSERT_FALSE(f.chord(BTN_A, BTN_C));
    feed(f, c, now, 100, false, false, false);
    TEST_ASSERT_EQUAL_INT(1, c.n);          // A short, untouched
    TEST_ASSERT_EQUAL_UINT8(BTN_A, c.ev[0].btn);
}

// A chord that is never polled behaves like two ordinary presses: the query
// is what suppresses, so a caller that does not ask is not silently changed.
static void test_accord_non_interroge_ne_change_rien(void) {
    ButtonFsm f; Collected c; uint32_t now = 1000;
    idle(f, c, now);
    feed(f, c, now, 100, true, false, true);
    feed(f, c, now, 200, false, false, false);
    TEST_ASSERT_EQUAL_INT(2, c.n);
}

// ---------------------------------------------------------------- harness
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_appui_court_une_seule_fois_au_relachement);
    RUN_TEST(test_appui_long_pendant_le_maintien);
    RUN_TEST(test_le_relachement_apres_un_long_ne_refire_pas);
    RUN_TEST(test_maintien_prolonge_ne_repete_pas);
    RUN_TEST(test_rebond_mecanique_ignore);
    RUN_TEST(test_le_filtre_laisse_passer_un_vrai_appui);
    RUN_TEST(test_armement_du_bandeau);
    RUN_TEST(test_table_radar);
    RUN_TEST(test_table_pont_de_cartes);
    RUN_TEST(test_table_ecran_de_lecture_sans_pages);
    RUN_TEST(test_pas_de_pagination_hors_du_pont);
    RUN_TEST(test_b_est_la_sortie_universelle);
    RUN_TEST(test_deux_boutons_ne_s_avalent_pas);
    RUN_TEST(test_deux_relachements_simultanes_aucun_perdu);
    RUN_TEST(test_long_et_relachement_dans_le_meme_tick);
    RUN_TEST(test_relachement_en_fenetre_de_rebond_reste_court);
    RUN_TEST(test_bouton_tenu_au_demarrage_est_ignore);
    RUN_TEST(test_accord_avale_les_deux_boutons);
    RUN_TEST(test_accord_tenu_ne_declenche_pas_de_long);
    RUN_TEST(test_un_seul_bouton_n_est_pas_un_accord);
    RUN_TEST(test_accord_non_interroge_ne_change_rien);
    return UNITY_END();
}
