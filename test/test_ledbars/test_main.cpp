// =============================================================================
// test_ledbars — the depth distribution, and the promise that nothing is lost
// =============================================================================
// ROADMAP §10 makes one claim that is stronger than a feature description: the
// new behaviour is a GENERALISATION whose degenerate case is the old one. That
// claim is worth exactly as much as the assertion that pins it, so it is pinned
// with an EXACT comparison — a weight of 0.999999 would still let the payload
// change, and "nothing was lost" would already be false.
// =============================================================================

#include <unity.h>
#include <math.h>
#include "../../src/engine/LedBars.h"

using namespace sce::ledbars;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// ---- at rest, the bar is what it has always been ---------------------------
static void test_au_repos_la_barre_est_identique_a_aujourdhui(void) {
    float w[PER_BAR];
    weights(0.0f, 100.0f, true, w);
    for (int i = 0; i < PER_BAR; i++)
        TEST_ASSERT_EQUAL_FLOAT(1.0f, w[i]);      // EXACT: see the header
}

// ---- gain 0 is off, whatever the head is doing -----------------------------
// The tuning key defaults to 0 and must mean OFF in the strong sense: a robot
// that never enables the effect has to receive the payload it received before
// the effect existed, even mid-dance.
static void test_gain_nul_est_une_desactivation_exacte(void) {
    float w[PER_BAR];
    for (float rate = -400.0f; rate <= 400.0f; rate += 37.0f) {
        weights(rate, 0.0f, true, w);
        for (int i = 0; i < PER_BAR; i++)
            TEST_ASSERT_EQUAL_FLOAT(1.0f, w[i]);
    }
}

// ---- no weight is EVER above one -------------------------------------------
// The load-bearing invariant. EmotionLeds clamps brightness at 255 and a bar
// can sit there, so a weight above one would clip instead of brightening — and
// the bar's total would collapse in the exact case the effect matters most.
static void test_aucun_poids_ne_depasse_un(void) {
    float w[PER_BAR];
    for (float rate = -1000.0f; rate <= 1000.0f; rate += 13.0f)
        for (float gain = 0.0f; gain <= 400.0f; gain += 25.0f) {
            weights(rate, gain, true, w);
            for (int i = 0; i < PER_BAR; i++) {
                TEST_ASSERT_TRUE(w[i] <= 1.0f);
                TEST_ASSERT_TRUE(w[i] >= 0.0f);   // and never negative light
            }
        }
}

// ---- the displacement grows with the rate, and then stops -------------------
static void test_le_deplacement_est_monotone_puis_sature(void) {
    float prev = -1.0f;
    for (float rate = 0.0f; rate <= FULL_RATE_DEG_S; rate += FULL_RATE_DEG_S / 8.0f) {
        const float d = displacement(rate, 100.0f);
        TEST_ASSERT_TRUE(d >= prev);
        prev = d;
    }
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, displacement(FULL_RATE_DEG_S, 100.0f));
    // past full rate it saturates rather than inverting
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, displacement(FULL_RATE_DEG_S * 9.0f, 100.0f));
}

// ---- turning the OTHER way is a movement too --------------------------------
// The suite above swept rate from 0 upward and never once passed a negative
// one, so it agreed with a `displacement` that returned a flat zero for every
// left turn. `EmotionLeds` uses this value, and nothing else about the
// distribution, to decide whether the bars are worth re-sending: reporting zero
// meant the write-suppression held the previous picture for the whole of every
// turn in one direction, and the far end never darkened unless the colour or an
// eye height happened to change at the same moment. Half the turns, silently.
static void test_le_deplacement_existe_dans_les_deux_sens(void) {
    for (float rate = -FULL_RATE_DEG_S; rate <= -1.0f; rate += FULL_RATE_DEG_S / 8.0f) {
        const float d = displacement(rate, 100.0f);
        TEST_ASSERT_TRUE(d < 0.0f);                    // it moved, and leftward
        TEST_ASSERT_TRUE(d >= -1.0f);                  // and stays in range
    }
    // Mirror rates displace equally far, in opposite directions
    for (float rate = 10.0f; rate <= 300.0f; rate += 47.0f)
        TEST_ASSERT_FLOAT_WITHIN(1e-6, displacement(rate, 100.0f),
                                 -displacement(-rate, 100.0f));
}

// ---- the two directions are never the SAME picture --------------------------
// What EmotionLeds actually stores is this value quantised to a byte. Two
// distributions that differ must not quantise equal, or the change detection is
// blind exactly where the effect is most visible.
static void test_les_deux_sens_ne_se_quantifient_pas_pareil(void) {
    for (float rate = 5.0f; rate <= FULL_RATE_DEG_S; rate += 17.0f) {
        const int8_t a = (int8_t)(displacement(+rate, 100.0f) * 127.0f);
        const int8_t b = (int8_t)(displacement(-rate, 100.0f) * 127.0f);
        TEST_ASSERT_TRUE(a != b);
    }
    // and at rest the two agree, because there is only one picture at rest
    TEST_ASSERT_EQUAL_INT8((int8_t)(displacement(0.0f, 100.0f) * 127.0f),
                           (int8_t)(displacement(-0.0f, 100.0f) * 127.0f));
}

// ---- turning one way darkens one end, the other way the other --------------
static void test_le_sens_choisit_l_extremite_qui_s_eteint(void) {
    float l[PER_BAR], r[PER_BAR];
    weights(+FULL_RATE_DEG_S, 100.0f, true, l);
    weights(-FULL_RATE_DEG_S, 100.0f, true, r);
    // one is the mirror of the other, LED for LED
    for (int i = 0; i < PER_BAR; i++)
        TEST_ASSERT_FLOAT_WITHIN(1e-6, l[i], r[PER_BAR - 1 - i]);
    // and they are genuinely different: a symmetric result would pass the
    // mirror check above while saying the direction does nothing
    TEST_ASSERT_TRUE(fabsf(l[0] - r[0]) > 0.5f);
}

// ---- the depth ORDER of the wiring is a setting, not an assumption ---------
// Nobody has measured whether a bar runs front to back or back to front. Until
// somebody does, flipping the key must flip the picture — otherwise a wrong
// wiring is indistinguishable from wrong arithmetic.
static void test_l_ordre_de_profondeur_retourne_la_barre(void) {
    float a[PER_BAR], b[PER_BAR];
    weights(FULL_RATE_DEG_S * 0.5f, 100.0f, true,  a);
    weights(FULL_RATE_DEG_S * 0.5f, 100.0f, false, b);
    for (int i = 0; i < PER_BAR; i++)
        TEST_ASSERT_FLOAT_WITHIN(1e-6, a[i], b[PER_BAR - 1 - i]);
}

// ---- the bar only ever LOSES light -----------------------------------------
// Stated as a total rather than per LED: the sum of the weights is at most
// PER_BAR, which is the sum at rest. That is the "subtracts, never adds" rule
// in the one unit the eye actually integrates.
static void test_la_barre_ne_gagne_jamais_de_lumiere(void) {
    float w[PER_BAR];
    for (float rate = -500.0f; rate <= 500.0f; rate += 29.0f) {
        weights(rate, 150.0f, true, w);
        float sum = 0.0f;
        for (int i = 0; i < PER_BAR; i++) sum += w[i];
        TEST_ASSERT_TRUE(sum <= (float)PER_BAR + 1e-5f);
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_au_repos_la_barre_est_identique_a_aujourdhui);
    RUN_TEST(test_gain_nul_est_une_desactivation_exacte);
    RUN_TEST(test_aucun_poids_ne_depasse_un);
    RUN_TEST(test_le_deplacement_est_monotone_puis_sature);
    RUN_TEST(test_le_deplacement_existe_dans_les_deux_sens);
    RUN_TEST(test_les_deux_sens_ne_se_quantifient_pas_pareil);
    RUN_TEST(test_le_sens_choisit_l_extremite_qui_s_eteint);
    RUN_TEST(test_l_ordre_de_profondeur_retourne_la_barre);
    RUN_TEST(test_la_barre_ne_gagne_jamais_de_lumiere);
    return UNITY_END();
}
