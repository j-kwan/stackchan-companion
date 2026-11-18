// =============================================================================
// test_personality_feel — the two FEEL dials, and the floor under them
// =============================================================================
// `clampTransitionScale`/`clampPitchBiasScale`/`scaledDurationMs` exist as pure
// functions in Personalities.h for exactly one reason: `Brain` cannot be
// natively tested at all (its constructor opens a FreeRTOS queue and pins a
// task), so the bound that protects the animation from an extreme personality
// value has to live somewhere a PC binary can reach it, or nothing ever
// verifies it. This file is that verification.
//
// The line these dials sit on (Personalities.h): a personality may change what
// the robot FEELS, never what the hardware TOLERATES. Neither dial can name a
// servo angle or a frame budget, so the test that matters here is not "the
// clamp returns the right number" (arithmetic) but "there is NO input, however
// deliberately hostile, that produces a transition duration or a pitch-bias
// SCALE outside the declared envelope" (a property, checked by sweeping it).
// =============================================================================

#include <unity.h>
#include <cmath>
#include "behavior/Personalities.h"

using namespace sce;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// ---- 1. THE BOUNDS THEMSELVES ----------------------------------------------
static void test_transition_scale_reste_dans_l_intervalle_declare(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.3f, clampTransitionScale(0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.3f, clampTransitionScale(-50.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, clampTransitionScale(3.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, clampTransitionScale(1000.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, clampTransitionScale(1.0f));  // unscaled untouched
}

static void test_pitch_bias_scale_accepte_zero_comme_valeur_reelle(void) {
    // 0 is not "close to the floor" here, it IS the floor, and it is a
    // deliberately reachable value: a stoic character whose head never tilts
    // with its mood. A clamp that nudged 0 up to some epsilon would quietly
    // take that character away from whoever asked for it.
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, clampPitchBiasScale(0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, clampPitchBiasScale(-9.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, clampPitchBiasScale(2.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, clampPitchBiasScale(500.0f));
}

// ---- 2. THE PROPERTY: NO INPUT ESCAPES THE ENVELOPE ------------------------
// Swept rather than sampled at a few points — the boundary is exactly where a
// hand-picked set of test values is most likely to miss the one input that
// slips through, which is the whole failure mode this dial exists to prevent.
static void test_aucune_echelle_de_transition_ne_produit_une_duree_hors_bornes(void) {
    const uint32_t bases[] = { 0, 1, 40, 180, 220, 300, 400, 3000, 60000 };
    for (uint32_t base : bases) {
        for (int i = -50; i <= 50; i++) {
            const float raw = (float)i * 2.0f;   // sweeps -100..100
            const uint32_t d = scaledDurationMs(base, raw);
            TEST_ASSERT_TRUE(d >= 40);
            TEST_ASSERT_TRUE(d <= 3000);
        }
    }
}

// A DELIBERATELY HOSTILE personality: NaN, infinity, the largest and smallest
// representable floats. `Personality` carries no validation of its own on these
// fields (Personalities.h states them as plain floats) precisely because the
// dial's SAFETY has to hold regardless of what reaches it — a corrupt card
// file, a client sending garbage, a future caller that forgets to sanitise.
static void test_les_entrees_degenerees_restent_a_l_interieur(void) {
    const float hostile[] = {
        NAN, -NAN, INFINITY, -INFINITY,
        3.4e38f, -3.4e38f, 1e-30f, -1e-30f,
    };
    for (float v : hostile) {
        const float ts = clampTransitionScale(v);
        const float pb = clampPitchBiasScale(v);
        // NaN compares false to everything, including itself — a clamp that
        // let one through would fail EVERY comparison below silently rather
        // than loudly, so it is named explicitly rather than trusted to the
        // range checks alone.
        TEST_ASSERT_FALSE(std::isnan(ts));
        TEST_ASSERT_FALSE(std::isnan(pb));
        TEST_ASSERT_TRUE(ts >= 0.3f && ts <= 3.0f);
        TEST_ASSERT_TRUE(pb >= 0.0f && pb <= 2.0f);
        TEST_ASSERT_TRUE(scaledDurationMs(220, v) >= 40);
        TEST_ASSERT_TRUE(scaledDurationMs(220, v) <= 3000);
    }
}

// ---- 3. THE COMPILED TABLE ITSELF IS ALREADY SANE --------------------------
// Not a safety net (fill() sets both to exactly 1.0, unscaled, for every
// compiled character) but a regression guard: a future character added
// without setting these would silently inherit whatever memset left behind if
// `fill()` ever stopped setting them explicitly.
static void test_les_personnalites_compilees_partent_non_mises_a_l_echelle(void) {
    for (int i = 0; i < personalities::count(); i++) {
        const Personality& p = personalities::at(i);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, p.transitionScale);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, p.pitchBiasScale);
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_transition_scale_reste_dans_l_intervalle_declare);
    RUN_TEST(test_pitch_bias_scale_accepte_zero_comme_valeur_reelle);
    RUN_TEST(test_aucune_echelle_de_transition_ne_produit_une_duree_hors_bornes);
    RUN_TEST(test_les_entrees_degenerees_restent_a_l_interieur);
    RUN_TEST(test_les_personnalites_compilees_partent_non_mises_a_l_echelle);
    return UNITY_END();
}
