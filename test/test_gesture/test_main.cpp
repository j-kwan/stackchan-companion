// =============================================================================
// test_gesture — the ONE swipe classifier, and the band it has to live in
// =============================================================================
// The interesting assertion here is not that a long drag is a swipe: it is
// that the two distances agree with each other. A bin's own swipe threshold
// and SceGuest's exit threshold were written in different files, and the gap
// between them is where a real drag was read as a tap at the finger's origin.
// Making them neighbours is only half the fix; the other half is a test that
// fails when somebody closes the gap.
// =============================================================================

#include <unity.h>
#include "../../firmware/common/Gesture.h"

using namespace sce::gesture;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// ---- the band between "my swipe" and "leaving the bin" ----------------------
static void test_la_bande_entre_glissement_et_sortie(void) {
    // The exit must be clearly harder than any gesture a bin does on its own,
    // or leaving happens by accident. A margin, not merely an inequality.
    TEST_ASSERT_TRUE(EXIT_PX > SWIPE_PX);
    TEST_ASSERT_TRUE(EXIT_PX - SWIPE_PX >= 30);

    // INSIDE the band, downward: this is the shape that bit the space bin.
    // It IS a swipe (so a caller must not fall through to its tap branch),
    // and it is NOT the exit (so SceGuest will not consume it either).
    TEST_ASSERT_TRUE(isSwipe(0, 70));
    TEST_ASSERT_TRUE(classify(0, 70) == Dir::Down);
    TEST_ASSERT_TRUE(70 < EXIT_PX);
}

// ---- below the threshold, nothing at all ------------------------------------
static void test_un_doigt_qui_ne_bouge_pas(void) {
    TEST_ASSERT_FALSE(isSwipe(0, 0));
    TEST_ASSERT_TRUE(classify(0, 0) == Dir::None);
    // One pixel short on BOTH axes is still nothing, even though the diagonal
    // travel exceeds the threshold: the rule is per-axis, deliberately, so a
    // slow circular drift never becomes a swipe.
    TEST_ASSERT_FALSE(isSwipe(SWIPE_PX - 1, SWIPE_PX - 1));
    TEST_ASSERT_TRUE(classify(SWIPE_PX - 1, SWIPE_PX - 1) == Dir::None);
}

// ---- the threshold itself is INCLUSIVE --------------------------------------
static void test_le_seuil_est_atteint_pas_depasse(void) {
    TEST_ASSERT_TRUE(isSwipe(SWIPE_PX, 0));
    TEST_ASSERT_TRUE(classify(SWIPE_PX, 0) == Dir::Right);
    TEST_ASSERT_FALSE(isSwipe(SWIPE_PX - 1, 0));
}

// ---- the four directions, viewer-centric ------------------------------------
static void test_les_quatre_directions(void) {
    TEST_ASSERT_TRUE(classify(0, -80)  == Dir::Up);
    TEST_ASSERT_TRUE(classify(0,  80)  == Dir::Down);
    TEST_ASSERT_TRUE(classify(-80, 0)  == Dir::Left);
    TEST_ASSERT_TRUE(classify(80,  0)  == Dir::Right);
}

// ---- ONE tie-break rule, and it is stated -----------------------------------
static void test_l_egalite_va_au_vertical(void) {
    // An exact diagonal has to go somewhere. Three implementations disagreed:
    // one answered "neither", one "vertical", one "horizontal". Vertical, so a
    // 45-degree drag pages the view ring rather than doing nothing.
    TEST_ASSERT_TRUE(classify(80, -80) == Dir::Up);
    TEST_ASSERT_TRUE(classify(-80, 80) == Dir::Down);
    // One pixel either side of the tie resolves the ordinary way.
    TEST_ASSERT_TRUE(classify(81, -80) == Dir::Right);
    TEST_ASSERT_TRUE(classify(80, -81) == Dir::Up);
}

// ---- a caller may still ask for its own distance ----------------------------
static void test_un_seuil_explicite_reste_possible(void) {
    // The shared default is what a bin uses unless it has a reason; the
    // parameter exists so that a reason can be expressed as code rather than
    // as a second copy of the classifier.
    TEST_ASSERT_TRUE(isSwipe(30, 0, 20));
    TEST_ASSERT_FALSE(isSwipe(30, 0, 40));
    TEST_ASSERT_TRUE(classify(30, 0, 20) == Dir::Right);
}

// ---- the press constants hold together --------------------------------------
static void test_les_constantes_d_appui(void) {
    // The screen must acknowledge the press WELL before it fires, or the user
    // lifts the finger and cancels their own gesture (radar, user 08-01).
    TEST_ASSERT_TRUE(LONG_ARM_MS < LONG_MS);
    TEST_ASSERT_TRUE(LONG_MS - LONG_ARM_MS >= 300);
    // "Still" must be stricter than "moved": a press that tolerated as much
    // drift as a swipe needs would fire both.
    TEST_ASSERT_TRUE(LONG_SLOP_PX < SWIPE_PX);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_la_bande_entre_glissement_et_sortie);
    RUN_TEST(test_un_doigt_qui_ne_bouge_pas);
    RUN_TEST(test_le_seuil_est_atteint_pas_depasse);
    RUN_TEST(test_les_quatre_directions);
    RUN_TEST(test_l_egalite_va_au_vertical);
    RUN_TEST(test_un_seuil_explicite_reste_possible);
    RUN_TEST(test_les_constantes_d_appui);
    return UNITY_END();
}
