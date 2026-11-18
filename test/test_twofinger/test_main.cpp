// =============================================================================
// test_twofinger — one hold, one answer
// =============================================================================
// The gesture this covers had every individual part working and still looked
// dead. Measured on the panel during ONE deliberate 8.9 s two-finger hold: the
// ">= 2 points" condition started five separate times, the glass read empty in
// between, the gesture latched twice and fired twice — and because it toggles
// a setting, the second answer undid the first.
//
// So the assertion that matters here is not "a long hold fires". It is "a long
// hold fires ONCE, even when the panel loses sight of the fingers in the
// middle" — and its twin, "a candidate that never really had two fingers does
// not latch at all", which is the trap the tolerance would reintroduce if it
// were granted one step earlier.
// =============================================================================

#include <unity.h>
#include "interact/TwoFingerLatch.h"

using sce::TwoFingerLatch;
using Event = sce::TwoFingerLatch::Event;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// loop() polls the panel at this cadence; the timings only mean something at
// the rate the real caller feeds them.
static constexpr uint32_t PASS_MS = 30;

struct Run { int latched = 0; int fired = 0; };

// Drives the latch over a span of time, asking the caller how many points the
// panel reports at each instant — so a test can script a dropout without
// unrolling two hundred calls.
template <typename F>
static Run drive(TwoFingerLatch& l, uint32_t fromMs, uint32_t toMs, F points,
                 bool eligible = true) {
    Run r;
    for (uint32_t t = fromMs; t <= toMs; t += PASS_MS) {
        const Event e = l.update(points(t), t);
        if      (e == Event::Latched) { r.latched++; l.setEligible(eligible); }
        else if (e == Event::Fire)    { r.fired++; }
    }
    return r;
}

static uint8_t always_two(uint32_t)  { return 2; }
static uint8_t always_one(uint32_t)  { return 1; }
static uint8_t always_none(uint32_t) { return 0; }

// ---- THE REGRESSION: one hold, one answer ----------------------------------
static void test_un_decrochage_ne_relance_pas_le_geste(void) {
    TwoFingerLatch l;
    // ONE gesture, with the panel losing the contact ENTIRELY for a few passes
    // in the middle. Before the fix this ended the gesture and started a second
    // one inside it: two latches, two fires, and a toggle back to where it
    // began.
    Run r = drive(l, 0, 6000, [](uint32_t t) -> uint8_t {
        return (t >= 1000 && t < 1100) ? 0 : 2;
    });
    TEST_ASSERT_EQUAL_INT(1, r.latched);
    TEST_ASSERT_EQUAL_INT(1, r.fired);
}

// ---- ...and it does not answer twice just for being held longer ------------
static void test_un_maintien_interminable_ne_repond_qu_une_fois(void) {
    TwoFingerLatch l;
    Run r = drive(l, 0, 30000, always_two);   // half a minute of two fingers
    TEST_ASSERT_EQUAL_INT(1, r.latched);
    TEST_ASSERT_EQUAL_INT(1, r.fired);
}

// ---- the tolerance must NOT reach the candidate ----------------------------
static void test_le_talon_de_relachement_ne_verrouille_pas(void) {
    TwoFingerLatch l;
    // The panel reports a lifting finger as two points for a couple of passes.
    // That is shorter than armMs, so it must produce NOTHING — latching on it
    // is what used to swallow every band gesture that followed.
    Run r = drive(l, 0, 4000, [](uint32_t t) -> uint8_t {
        return (t < 90) ? 2 : (t < 200 ? 1 : 0);
    });
    TEST_ASSERT_EQUAL_INT(0, r.latched);
    TEST_ASSERT_EQUAL_INT(0, r.fired);
}

// ---- one finger is never this gesture, however long it stays ---------------
static void test_un_seul_doigt_ne_verrouille_jamais(void) {
    TwoFingerLatch l;
    Run r = drive(l, 0, 20000, always_one);
    TEST_ASSERT_EQUAL_INT(0, r.latched);
    TEST_ASSERT_EQUAL_INT(0, r.fired);
    TEST_ASSERT_FALSE(l.engaged());
}

// ---- a second gesture, after the hand has really gone ----------------------
static void test_la_main_levee_termine_le_geste(void) {
    TwoFingerLatch l;
    Run r1 = drive(l, 0, 4000, always_two);
    TEST_ASSERT_EQUAL_INT(1, r1.fired);
    // Empty for longer than clearMs: THIS is what ends a gesture.
    drive(l, 4030, 5000, always_none);
    TEST_ASSERT_FALSE(l.engaged());
    // ...and the next one is allowed to answer in its turn.
    Run r3 = drive(l, 5030, 9000, always_two);
    TEST_ASSERT_EQUAL_INT(1, r3.latched);
    TEST_ASSERT_EQUAL_INT(1, r3.fired);
}

// ---- the caller's veto stops the answer, not the takeover ------------------
static void test_des_doigts_hors_zone_ne_declenchent_rien(void) {
    TwoFingerLatch l;
    Run r = drive(l, 0, 8000, always_two, /*eligible=*/false);
    // It STILL takes the screen — otherwise the one-finger path would keep
    // dispatching taps underneath a gesture that is merely aimed badly.
    TEST_ASSERT_EQUAL_INT(1, r.latched);
    TEST_ASSERT_TRUE(l.engaged());
    // But it never answers.
    TEST_ASSERT_EQUAL_INT(0, r.fired);
}

// ---- the one-finger path stays suppressed THROUGH the dropout --------------
static void test_le_geste_tient_l_ecran_pendant_le_decrochage(void) {
    TwoFingerLatch l;
    drive(l, 0, 900, always_two);
    TEST_ASSERT_TRUE(l.engaged());
    // Glass momentarily empty — shorter than clearMs. If engaged() dropped
    // here, the release that ends the gesture would be dispatched as a tap.
    drive(l, 930, 1200, always_none);
    TEST_ASSERT_TRUE(l.engaged());
}

// ---- the hold is timed from the SECOND finger, not from the arming ---------
static void test_le_maintien_part_du_deuxieme_doigt(void) {
    TwoFingerLatch l;
    // Just under three seconds after the second point landed: nothing yet.
    Run early = drive(l, 0, 2900, always_two);
    TEST_ASSERT_EQUAL_INT(0, early.fired);
    // The gesture asks for three seconds, not three plus the arming debounce.
    Run late = drive(l, 2930, 3100, always_two);
    TEST_ASSERT_EQUAL_INT(1, late.fired);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_un_decrochage_ne_relance_pas_le_geste);
    RUN_TEST(test_un_maintien_interminable_ne_repond_qu_une_fois);
    RUN_TEST(test_le_talon_de_relachement_ne_verrouille_pas);
    RUN_TEST(test_un_seul_doigt_ne_verrouille_jamais);
    RUN_TEST(test_la_main_levee_termine_le_geste);
    RUN_TEST(test_des_doigts_hors_zone_ne_declenchent_rien);
    RUN_TEST(test_le_geste_tient_l_ecran_pendant_le_decrochage);
    RUN_TEST(test_le_maintien_part_du_deuxieme_doigt);
    return UNITY_END();
}
