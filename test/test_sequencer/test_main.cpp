// =============================================================================
// test_sequencer — StackChan-Companion v2 : timeline des danses (P4b)
// =============================================================================
// Vérifie : ordre des keyframes, invariant hold ≥ servo (fix PANIC v1),
// abort réflexe immédiat, biais de regard par keyframe, fin propre,
// intégrité des données de danses (Dances.h).
// Exécution : .\scripts\gates\test-native.ps1 test_sequencer
// =============================================================================

#include <unity.h>
#include "engine/Clock.h"
#include "engine/Units.h"
#include "behavior/Sequencer.h"
#include "behavior/Dances.h"

using namespace sce;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// Trois keyframes simples pour les tests de mécanique
static const DanceKey SEQ3[] = {
    { 10, 0, 100, 200, Happy, 1 },
    { -10, 0, 100, 150, EMOTIONS_COUNT, 0, -0.5f },
    { 0, 0, 100, 50, Normal },   // holdMs 50 < servoMs 100 → clamp attendu
};

// Ordre + timing : chaque keyframe appliquée UNE fois, au bon moment
static void test_seq_order_and_timing() {
    FakeClock clk;
    Sequencer seq(clk);
    seq.play(SEQ3, 3);

    const DanceKey* k = seq.update();            // t=0 : keyframe 0
    TEST_ASSERT_NOT_NULL(k);
    TEST_ASSERT_EQUAL_INT(Happy, k->emotion);

    clk.advanceMs(100);                          // < hold 200 → rien
    TEST_ASSERT_NULL(seq.update());
    clk.advanceMs(100);                          // t=200 → keyframe 1
    k = seq.update();
    TEST_ASSERT_NOT_NULL(k);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -10.0f, k->yawOff);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -0.5f, seq.gazeYBias());

    clk.advanceMs(150);                          // t=350 → keyframe 2
    k = seq.update();
    TEST_ASSERT_NOT_NULL(k);
    TEST_ASSERT_EQUAL_INT(Normal, k->emotion);
    TEST_ASSERT_TRUE(seq.isActive());

    // Invariant hold ≥ servo : la keyframe 2 (hold 50 < servo 100) tient 100 ms
    clk.advanceMs(60);
    TEST_ASSERT_NULL(seq.update());
    TEST_ASSERT_TRUE(seq.isActive());            // pas finie à 60 ms
    clk.advanceMs(50);                           // 110 ms ≥ servo 100 → fin
    TEST_ASSERT_NULL(seq.update());
    TEST_ASSERT_FALSE(seq.isActive());
    TEST_ASSERT_EQUAL_UINT32(1, seq.clampCount());
}

// Abort réflexe : coupure immédiate, plus aucune keyframe ensuite
static void test_seq_abort_immediate() {
    FakeClock clk;
    Sequencer seq(clk);
    seq.play(SEQ3, 3);
    seq.update();
    clk.advanceMs(50);
    seq.abort();
    TEST_ASSERT_FALSE(seq.isActive());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, seq.gazeYBias());
    clk.advanceMs(500);
    TEST_ASSERT_NULL(seq.update());
}

// play() préempte la séquence en cours (redémarre à la keyframe 0)
static void test_seq_play_preempts() {
    FakeClock clk;
    Sequencer seq(clk);
    seq.play(SEQ3, 3);
    seq.update();
    clk.advanceMs(200);
    seq.update();                                // keyframe 1 en cours
    seq.play(SEQ3, 3);                           // préemption
    const DanceKey* k = seq.update();
    TEST_ASSERT_NOT_NULL(k);
    TEST_ASSERT_EQUAL_INT(Happy, k->emotion);    // repartie de 0
}

// Intégrité des données : chaque danse finit Normal à la pose neutre
// (leçons v1 REVIEW-22/GREET-1) et reste dans les limites K151
static void test_dances_data_integrity() {
    const dances::Entry* t = dances::table();
    int n = 0;
    for (int i = 0; t[i].name; i++) {
        n++;
        TEST_ASSERT_TRUE(t[i].count >= 2);
        const DanceKey& last = t[i].keys[t[i].count - 1];
        TEST_ASSERT_EQUAL_INT(Normal, last.emotion);          // reprise roulette
        TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, last.yawOff);   // pose neutre
        TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, last.pitchOff);
        for (int j = 0; j < t[i].count; j++) {
            const DanceKey& k = t[i].keys[j];
            TEST_ASSERT_TRUE(fabsf(k.yawOff) <= units::YAW_RANGE + 0.01f);
            // pitchOff : négatif = relevé (jusqu'à PITCH_MIN), positif =
            // tête BAISSÉE (home 93 depuis 2026-07-16 — jamais sous
            // l'horizon PITCH_MAX 103)
            TEST_ASSERT_TRUE(units::PITCH_NEUTRAL + k.pitchOff
                             <= units::PITCH_MAX + 0.01f);
            TEST_ASSERT_TRUE(units::PITCH_NEUTRAL + k.pitchOff
                             >= units::PITCH_MIN - 0.01f);
        }
    }
    TEST_ASSERT_EQUAL_INT(15, n);                             // 15 danses (+cry 2026-07-16)
    TEST_ASSERT_EQUAL_INT(n, dances::count());
    TEST_ASSERT_EQUAL_INT(2, dances::indexOf("PANIC"));       // lookup insensible casse
    TEST_ASSERT_EQUAL_INT(-1, dances::indexOf("inconnu"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_seq_order_and_timing);
    RUN_TEST(test_seq_abort_immediate);
    RUN_TEST(test_seq_play_preempts);
    RUN_TEST(test_dances_data_integrity);
    return UNITY_END();
}
