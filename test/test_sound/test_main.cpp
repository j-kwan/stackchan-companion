// =============================================================================
// test_sound — StackChan-Companion : direction du bruit (SoundDirection)
// =============================================================================
// Buffers stéréo synthétiques (carrés ±amplitude) : porte ambiante,
// signe/lissage de l'imbalance, absorption d'un fond sonore continu.
// Exécution : .\scripts\gates\test-native.ps1 test_sound
// =============================================================================

#include <unity.h>
#include <cstdint>
#include "behavior/SoundDirection.h"

using namespace sce;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

static constexpr size_t FRAMES = 256;
static constexpr float  THR    = 900.0f;

// Bloc stéréo entrelacé : onde carrée (RMS = amp). Mapping HW K151
// (2026-07-17) : canal 1 (premier échantillon) = micro DROIT, canal 2 =
// micro GAUCHE — les paramètres suivent ce mapping.
static void fill(int16_t* lr, int16_t ampR, int16_t ampL) {
    for (size_t i = 0; i < FRAMES; i++) {
        int16_t s = (i & 1) ? 1 : -1;
        lr[2 * i]     = (int16_t)(s * ampR);
        lr[2 * i + 1] = (int16_t)(s * ampL);
    }
}

// Silence (sous le seuil ET sous 2× l'ambiant) → jamais d'événement
static void test_sound_silence_no_event() {
    SoundDirection sd;
    int16_t buf[FRAMES * 2];
    fill(buf, 60, 60);
    for (int i = 0; i < 50; i++) {
        auto r = sd.feed(buf, FRAMES, THR);
        TEST_ASSERT_FALSE(r.event);
    }
}

// Bruit franc à DROITE (canal R fort) → événement, imbalance > 0 ;
// à GAUCHE → imbalance < 0 (le signe suit le canal dominant)
static void test_sound_direction_sign() {
    SoundDirection sd;
    int16_t buf[FRAMES * 2];

    fill(buf, 8000, 800);                      // droite dominante (canal 1)
    auto r = sd.feed(buf, FRAMES, THR);
    TEST_ASSERT_TRUE(r.event);
    TEST_ASSERT_TRUE(r.imbalance > 0.3f);

    SoundDirection sd2;
    fill(buf, 800, 8000);                      // gauche dominante (canal 2)
    r = sd2.feed(buf, FRAMES, THR);
    TEST_ASSERT_TRUE(r.event);
    TEST_ASSERT_TRUE(r.imbalance < -0.3f);
}

// Bruit franc ÉQUILIBRÉ (en face) → événement mais imbalance ~0 :
// c'est la condition d'arrêt du suivi (« même niveau sur les deux micros »)
static void test_sound_balanced_centered() {
    SoundDirection sd;
    int16_t buf[FRAMES * 2];
    fill(buf, 6000, 6000);
    auto r = sd.feed(buf, FRAMES, THR);
    TEST_ASSERT_TRUE(r.event);
    TEST_ASSERT_TRUE(r.imbalance > -0.1f && r.imbalance < 0.1f);
}

// Fond sonore CONTINU : le plancher ambiant l'absorbe (EMA lente) —
// les événements finissent par cesser, puis le silence re-arme la porte
static void test_sound_ambient_absorbs_constant_noise() {
    SoundDirection sd;
    int16_t buf[FRAMES * 2];
    fill(buf, 4000, 4000);
    bool still = true;
    for (int i = 0; i < 800 && still; i++) still = sd.feed(buf, FRAMES, THR).event;
    TEST_ASSERT_FALSE(still);                  // absorbé

    fill(buf, 60, 60);                         // retour au calme
    for (int i = 0; i < 400; i++) sd.feed(buf, FRAMES, THR);
    fill(buf, 4000, 8000);                     // nouveau bruit franc
    TEST_ASSERT_TRUE(sd.feed(buf, FRAMES, THR).event);
}

// Retour doux : après l'événement, l'imbalance décroît vers 0 au silence
static void test_sound_imbalance_decays() {
    SoundDirection sd;
    int16_t buf[FRAMES * 2];
    fill(buf, 8000, 800);
    sd.feed(buf, FRAMES, THR);
    fill(buf, 40, 40);
    float last = 1.0f;
    for (int i = 0; i < 60; i++) last = sd.feed(buf, FRAMES, THR).imbalance;
    TEST_ASSERT_TRUE(last < 0.05f);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_sound_silence_no_event);
    RUN_TEST(test_sound_direction_sign);
    RUN_TEST(test_sound_balanced_centered);
    RUN_TEST(test_sound_ambient_absorbs_constant_noise);
    RUN_TEST(test_sound_imbalance_decays);
    return UNITY_END();
}
