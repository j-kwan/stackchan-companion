// =============================================================================
// test_vor — StackChan-Companion v2 : VestibularSystem avec profils gyro synthétiques
// =============================================================================
// Le VOR se teste SANS hardware : on injecte des vitesses angulaires connues
// et on vérifie la physique du réflexe (ROADMAP §2.2).
//
// Exécution : .\scripts\gates\test-native.ps1 test_vor
// =============================================================================

#include <unity.h>
#include "engine/Clock.h"
#include "engine/Tuning.h"
#include "behavior/VestibularSystem.h"

using namespace sce;
using namespace sce::units;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// Tick 10 ms pendant `ms` avec une vitesse de tête constante
template <typename F>
static void spin(FakeClock& clk, VestibularSystem& vor, uint32_t ms,
                 Vec2f headVel, F onTick) {
    for (uint32_t t = 0; t < ms; t += 10) {
        clk.advanceMs(10);
        bool sacc = vor.update(headVel, {});
        onTick(sacc);
    }
}

// Contre-rotation : tête qui tourne vers +X → les yeux partent vers -X,
// proportionnellement au gain et à l'intégrale de la vitesse
static void test_vor_counter_rotation() {
    FakeClock clk; Tuning tn;
    VestibularSystem vor(clk, tn);
    // 20 °/s pendant 200 ms = 4° de rotation → offset ≈ -4/45 × gain(0.9)
    spin(clk, vor, 200, { 20.0f, 0.0f }, [](bool) {});
    float expected = -(4.0f * DEG2GAZE_X) * tn.vor_gain;
    TEST_ASSERT_FLOAT_WITHIN(0.02f, expected, vor.offset().x);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.0f, vor.offset().y);
}

// Saturation : rotation longue → l'offset atteint le seuil → SACCADE de
// recentrage déclenchée, puis offset ramené à ~0
static void test_vor_saturation_triggers_saccade() {
    FakeClock clk; Tuning tn;
    VestibularSystem vor(clk, tn);
    int saccades = 0;
    // 60 °/s continu : sature en ~250 ms (seuil 0.75×0.40 = 0.30)
    spin(clk, vor, 1000, { 60.0f, 0.0f }, [&](bool s) { if (s) saccades++; });
    TEST_ASSERT_TRUE(saccades >= 1);
    // Après une saccade, l'offset repart de ~0 (recentrage effectué)
    // → à aucun moment il ne dépasse GAZE_MAX (clamp défensif)
    TEST_ASSERT_TRUE(fabsf(vor.offset().x) <= GAZE_MAX_X + 1e-4f);
}

// Tête stable avec offset résiduel → saccade de recentrage après ~300 ms,
// offset final ≈ 0
static void test_vor_recentre_when_still() {
    FakeClock clk; Tuning tn;
    VestibularSystem vor(clk, tn);
    // Construire un offset FRANC (sous la saturation 0.30, mais assez grand
    // pour que la dérive complémentaire ne le résorbe pas pendant la fenêtre
    // de 300 ms de stabilité) : 40°/s × 0,3 s = 12° → ≈ 0,24
    spin(clk, vor, 300, { 40.0f, 0.0f }, [](bool) {});
    TEST_ASSERT_TRUE(fabsf(vor.offset().x) > 0.15f);
    // Tête immobile → rattrapage saccadique vers la cible (0 ici)
    int saccades = 0;
    spin(clk, vor, 800, { 0.0f, 0.0f }, [&](bool s) { if (s) saccades++; });
    TEST_ASSERT_TRUE(saccades >= 1);
    TEST_ASSERT_FLOAT_WITHIN(0.03f, 0.0f, vor.offset().x);
}

// Copie d'efférence : si cmdVel == headVel (mouvement 100 % auto-généré),
// AUCUN offset ne s'accumule et aucune secousse n'est détectée
static void test_vor_efference_copy_cancels() {
    FakeClock clk; Tuning tn;
    VestibularSystem vor(clk, tn);
    for (uint32_t t = 0; t < 1000; t += 10) {
        clk.advanceMs(10);
        vor.update({ 40.0f, 0.0f }, {}, 1.0f, { 40.0f, 0.0f });
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, vor.offset().x);
    TEST_ASSERT_FALSE(vor.shakeDetected());
}

// Biais gyro (v2.1) : un offset DC constant de 1,5 °/s (typique BMI270) est
// appris pendant l'immobilité — l'œil reste PARFAITEMENT stable (zéro dérive,
// zéro saccade de rattrapage parasite)
static void test_vor_gyro_bias_rejected() {
    FakeClock clk; Tuning tn;
    VestibularSystem vor(clk, tn);
    int saccades = 0;
    float maxOff = 0.0f;
    for (uint32_t t = 0; t < 20000; t += 10) {   // 20 s « immobile » biaisé
        clk.advanceMs(10);
        if (vor.update({ 1.5f, -0.8f }, {}, 1.0f)) saccades++;
        float o = vor.offset().length();
        if (o > maxOff) maxOff = o;
    }
    TEST_ASSERT_FLOAT_WITHIN(0.3f, 1.5f, vor.gyroBias().x);   // biais appris
    TEST_ASSERT_TRUE(maxOff < 0.06f);                          // œil quasi immobile
    TEST_ASSERT_EQUAL_INT(0, saccades);                        // aucun rattrapage parasite
}

// Zone morte : du bruit gyro sous 0,8 °/s (post-biais) n'accumule RIEN
static void test_vor_deadband() {
    FakeClock clk; Tuning tn;
    VestibularSystem vor(clk, tn);
    for (uint32_t t = 0; t < 5000; t += 10) {
        clk.advanceMs(10);
        // bruit alterné ±0,5 °/s — sous la zone morte
        float n = (t / 10 % 2) ? 0.5f : -0.5f;
        vor.update({ n, n }, {}, 1.0f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, vor.offset().x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, vor.offset().y);
}

// Secousse : > seuil (tuning.shake_gyro_thr, défaut 35 °/s — calibré HW
// 2026-07-11) soutenu > 500 ms → détectée ; brève → PAS détectée
static void test_vor_shake_detection() {
    FakeClock clk; Tuning tn;
    VestibularSystem vor(clk, tn);
    spin(clk, vor, 400, { 50.0f, 0.0f }, [](bool) {});
    TEST_ASSERT_FALSE(vor.shakeDetected());     // pas encore soutenu
    spin(clk, vor, 300, { 50.0f, 0.0f }, [](bool) {});
    TEST_ASSERT_TRUE(vor.shakeDetected());      // 700 ms > 500 ms
    spin(clk, vor, 100, { 0.0f, 0.0f }, [](bool) {});
    TEST_ASSERT_FALSE(vor.shakeDetected());     // retombe aussitôt
}

// VOR v3 : la secousse est jugée sur la magnitude gyro 3 AXES — une secousse
// sur un axe NON mappé (vel écran ≈ 0) est quand même détectée
static void test_vor_shake_3axis_magnitude() {
    FakeClock clk; Tuning tn;
    VestibularSystem vor(clk, tn);
    for (uint32_t t = 0; t < 700; t += 10) {
        clk.advanceMs(10);
        vor.update({ 0.0f, 0.0f }, {}, 1.0f, {}, /*gyroMag3=*/50.0f);
    }
    TEST_ASSERT_TRUE(vor.shakeDetected());
}

// VOR v3.1 : selfMotion inhibe le SHAKE uniquement (fix « Scared en fin de
// Laugh ») ; l'intégration reste active — l'efférence (validée §1.7) annule
// déjà le mouvement propre. Ici cmdVel == headVel → offset nul, pas de Scared.
static void test_vor_selfmotion_gates() {
    FakeClock clk; Tuning tn;
    VestibularSystem vor(clk, tn);
    for (uint32_t t = 0; t < 1000; t += 10) {
        clk.advanceMs(10);
        vor.update({ 40.0f, 0.0f }, {}, 1.0f, { 40.0f, 0.0f }, 40.0f,
                   /*selfMotion=*/true);
    }
    TEST_ASSERT_FALSE(vor.shakeDetected());
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.0f, vor.offset().x);
}

// Équilibre incliné : gyro nul + cible d'inclinaison → l'offset converge vers
// tilt × GAZE_MAX et S'Y TIENT (dérive complémentaire + saccade de rattrapage
// vers la CIBLE, pas vers zéro — les yeux tiennent la compensation au repos)
static void test_vor_drift_correction() {
    FakeClock clk; Tuning tn;
    VestibularSystem vor(clk, tn);
    for (uint32_t t = 0; t < 3000; t += 10) {
        clk.advanceMs(10);
        vor.update({}, { 0.5f, 0.0f });
    }
    // Après 3 s à 100 Hz, convergence quasi complète vers 0.5×GAZE_MAX_X…
    TEST_ASSERT_FLOAT_WITHIN(0.03f, 0.5f * GAZE_MAX_X, vor.offset().x);
}

// ---- Fusion magnétomètre (08-04) — l'observable que l'accéléro n'a pas.
// Une « rotation » gyro que le CAP ne confirme pas est une dérive : avec
// vor_mag_alpha, l'offset reste borné près de la cible et AUCUNE saccade de
// rattrapage ne part ; le même profil sans fusion sature et saccade.
static void test_vor_mag_corrige_fausse_rotation() {
    FakeClock clk; Tuning tn;
    tn.vor_mag_alpha = 0.10f;
    VestibularSystem vor(clk, tn);
    int saccades = 0;
    for (uint32_t t = 0; t < 10000; t += 10) {   // 10 s de fausse rotation
        clk.advanceMs(10);
        // 6 °/s : au-dessus de la zone morte ET du seuil « quiet » (3 °/s),
        // donc ni le biais ni la dérive accéléro ne peuvent aider — seul le
        // cap, constant, dit que la tête ne tourne PAS.
        if (vor.update({ 6.0f, 0.0f }, {}, 1.0f, {}, -1.0f, false, 100.0f))
            saccades++;
    }
    TEST_ASSERT_TRUE(fabsf(vor.offset().x) < 0.05f);
    TEST_ASSERT_EQUAL_INT(0, saccades);
    // Contrôle : alpha = 0 (défaut) → le même profil dérive et saccade.
    Tuning tn0; VestibularSystem vor0(clk, tn0);
    int sacc0 = 0;
    for (uint32_t t = 0; t < 10000; t += 10) {
        clk.advanceMs(10);
        if (vor0.update({ 6.0f, 0.0f }, {}, 1.0f, {}, -1.0f, false, 100.0f))
            sacc0++;
    }
    TEST_ASSERT_TRUE(sacc0 >= 1);
}

// Une VRAIE rotation (le cap suit l'intégrale du gyro) n'est PAS combattue :
// la contre-rotation reste celle du test de référence.
static void test_vor_mag_ne_combat_pas_une_vraie_rotation() {
    FakeClock clk; Tuning tn;
    tn.vor_mag_alpha = 0.10f;
    VestibularSystem vor(clk, tn);
    float hdg = 100.0f;
    for (uint32_t t = 0; t < 200; t += 10) {
        clk.advanceMs(10);
        hdg += 20.0f * 0.01f;                    // le cap suit la rotation
        vor.update({ 20.0f, 0.0f }, {}, 1.0f, {}, -1.0f, false, hdg);
    }
    float expected = -(4.0f * DEG2GAZE_X) * tn.vor_gain;
    TEST_ASSERT_FLOAT_WITHIN(0.02f, expected, vor.offset().x);
}

// Cap absent (-1) : la fusion est inerte même avec alpha > 0 — le même
// profil de fausse rotation dérive comme sans magnétomètre.
static void test_vor_mag_absent_est_inerte() {
    FakeClock clk; Tuning tn;
    tn.vor_mag_alpha = 0.10f;
    VestibularSystem vor(clk, tn);
    for (uint32_t t = 0; t < 3000; t += 10) {
        clk.advanceMs(10);
        vor.update({ 6.0f, 0.0f }, {}, 1.0f, {}, -1.0f, false, -1.0f);
    }
    TEST_ASSERT_TRUE(fabsf(vor.offset().x) > 0.05f);   // la dérive court
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_vor_counter_rotation);
    RUN_TEST(test_vor_saturation_triggers_saccade);
    RUN_TEST(test_vor_recentre_when_still);
    RUN_TEST(test_vor_efference_copy_cancels);
    RUN_TEST(test_vor_gyro_bias_rejected);
    RUN_TEST(test_vor_deadband);
    RUN_TEST(test_vor_shake_detection);
    RUN_TEST(test_vor_shake_3axis_magnitude);
    RUN_TEST(test_vor_selfmotion_gates);
    RUN_TEST(test_vor_drift_correction);
    RUN_TEST(test_vor_mag_corrige_fausse_rotation);
    RUN_TEST(test_vor_mag_ne_combat_pas_une_vraie_rotation);
    RUN_TEST(test_vor_mag_absent_est_inerte);
    return UNITY_END();
}
