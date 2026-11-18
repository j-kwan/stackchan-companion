// =============================================================================
// test_yawrange — the yaw amplitude must stay REACHABLE, not merely plausible
// =============================================================================
// `YAW_RANGE` spent months at 40 with no recorded justification, under a
// heading that claimed a hardware measurement. It was widened to 130 on
// 2026-08-01 once three things were established: the vendor states no X-axis
// restriction, the head turns freely by hand through several full revolutions,
// and the real bound is OUR OWN command path — `ServoMotion::writeDeg` maps
// 0-300 deg onto the servo's position register and clamps there.
//
// That last one is the only bound a test can hold, and it is precisely the one
// that would break silently: raise YAW_RANGE past the reachable envelope and
// nothing fails to compile, nothing throws — the servo just stops short of
// where the choreography says it should be, and every dance authored against
// the simulator quietly under-runs.
// =============================================================================
#include <unity.h>
#include "engine/Units.h"

using namespace sce::units;

void setUp(void) {}
void tearDown(void) {}

// The centre plus the amplitude must stay inside what writeDeg can address.
static void test_amplitude_atteignable_des_deux_cotes(void) {
    TEST_ASSERT_TRUE(YAW_CENTER - YAW_RANGE >= 0);
    TEST_ASSERT_TRUE(YAW_CENTER + YAW_RANGE <= YAW_SPAN_DEG);
}

// The binding side is the SMALLER of the two, and the constant must not exceed
// it. With the centre at 166 over a 300 deg span that is +134.
static void test_borne_par_le_cote_le_plus_court(void) {
    const int up   = YAW_SPAN_DEG - YAW_CENTER;   // 134
    const int down = YAW_CENTER;                  // 166
    const int tightest = up < down ? up : down;
    TEST_ASSERT_EQUAL_INT(134, tightest);
    TEST_ASSERT_TRUE(YAW_RANGE <= tightest);
}

// A margin is kept against the conversion clamp so a commanded extreme never
// lands exactly on it.
static void test_marge_contre_le_clamp(void) {
    const int up = YAW_SPAN_DEG - YAW_CENTER;
    TEST_ASSERT_TRUE(up - YAW_RANGE >= 1);
}

// The widening must be REAL: the old value is the regression to guard against.
static void test_bien_plus_large_que_l_ancien_40(void) {
    TEST_ASSERT_TRUE(YAW_RANGE > 40);
}

// gazeFromHead saturates at YAW_FULL_GAZE and must stay CLAMPED, never wrap or
// overflow, now that yaw can travel far beyond it.
static void test_le_regard_sature_sans_deborder(void) {
    const float g1 = gazeFromHead((float)(YAW_CENTER + YAW_RANGE), (float)PITCH_NEUTRAL).x;
    const float g2 = gazeFromHead((float)(YAW_CENTER - YAW_RANGE), (float)PITCH_NEUTRAL).x;
    TEST_ASSERT_FLOAT_WITHIN(0.001f,  1.0f, g1);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, g2);
    // Centre still reads as "straight ahead".
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f,
        gazeFromHead((float)YAW_CENTER, (float)PITCH_NEUTRAL).x);
}

// Le REFLEXE et la CHOREGRAPHIE n'ont pas la meme enveloppe, et c'est le point.
// Elargir YAW_RANGE a 130 a aussi elargi celle des mouvements RELATIFS
// cumulatifs (suivi sonore, virage post-sursaut) qui n'ont aucune borne propre :
// un bruit lateral soutenu emmenait la tete a 130 deg du centre, ecran detourne.
static void test_enveloppe_reflexe_plus_etroite(void) {
    TEST_ASSERT_TRUE(YAW_REFLEX_RANGE < YAW_RANGE);
    // Le clamp reflexe borne bien des deux cotes...
    TEST_ASSERT_EQUAL_FLOAT((float)(YAW_CENTER + YAW_REFLEX_RANGE),
                            clampReflexYaw((float)(YAW_CENTER + 1000)));
    TEST_ASSERT_EQUAL_FLOAT((float)(YAW_CENTER - YAW_REFLEX_RANGE),
                            clampReflexYaw((float)(YAW_CENTER - 1000)));
    // ...et laisse passer ce qui est dedans.
    TEST_ASSERT_EQUAL_FLOAT((float)YAW_CENTER, clampReflexYaw((float)YAW_CENTER));
    // Il reste ATTEIGNABLE : un reflexe ne doit jamais viser hors de la plage
    // que moveTo accepte.
    TEST_ASSERT_TRUE(YAW_CENTER + YAW_REFLEX_RANGE <= YAW_CENTER + YAW_RANGE);
    TEST_ASSERT_TRUE(YAW_CENTER - YAW_REFLEX_RANGE >= YAW_CENTER - YAW_RANGE);
}

// ---------------------------------------------------------------- harness
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_amplitude_atteignable_des_deux_cotes);
    RUN_TEST(test_borne_par_le_cote_le_plus_court);
    RUN_TEST(test_marge_contre_le_clamp);
    RUN_TEST(test_bien_plus_large_que_l_ancien_40);
    RUN_TEST(test_le_regard_sature_sans_deborder);
    RUN_TEST(test_enveloppe_reflexe_plus_etroite);
    return UNITY_END();
}
