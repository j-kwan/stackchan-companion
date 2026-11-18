// =============================================================================
// test_behavior — StackChan-Companion v2 : machines d'états comportementales
// =============================================================================
// Tests natifs (FakeClock + Rng seedé — déterministes) des modules purs P2 :
//   BlinkController : autoblink, gel borné Surprised, Dead bloqué, wink,
//                     lag œil droit, machine Sleepy (droop/chute/plafond)
//   IdleBehavior    : fixation stable → saccade sèche → nouvelle fixation,
//                     amplitude bornée, jitter seulement sur fixation longue
//   EmotionRoulette : cadence d'intervalle, verrou, poids nuls jamais tirés
//
// C'est exactement la classe de bugs v1 (falling-edges, états qui fuient,
// AUTO-1..4) que ces tests attrapent avant le flash.
//
// Exécution : .\scripts\gates\test-native.ps1 test_behavior
// =============================================================================

#include <unity.h>
#include "engine/Clock.h"
#include "engine/Rng.h"
#include "behavior/BlinkController.h"
#include "behavior/IdleBehavior.h"
#include "behavior/EmotionRoulette.h"

using namespace sce;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// Avance le temps par pas de 10 ms (tick Brain 100 Hz) en appelant update()
template <typename F>
static void runFor(FakeClock& clk, uint32_t ms, F tickFn) {
    for (uint32_t t = 0; t < ms; t += 10) {
        clk.advanceMs(10);
        tickFn();
    }
}

// -----------------------------------------------------------------------
// BlinkController
// -----------------------------------------------------------------------

// L'autoblink finit toujours par se produire, et l'œil se referme puis rouvre
static void test_blink_autoblink_happens() {
    FakeClock clk; Rng rng(42);
    Tuning tn; BlinkController bc(clk, rng, tn);
    float minOpen = 1.0f;
    runFor(clk, 12000, [&]() { bc.update(); if (bc.openL() < minOpen) minOpen = bc.openL(); });
    TEST_ASSERT_TRUE(minOpen < 0.1f);                    // au moins un blink complet
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, bc.openL());   // et il s'est rouvert
}

// Dead : plus jamais de blink
static void test_blink_dead_blocks() {
    FakeClock clk; Rng rng(7);
    Tuning tn; BlinkController bc(clk, rng, tn);
    bc.setEmotion(Dead, true);
    runFor(clk, 200, [&]() { bc.update(); });   // laisser un éventuel blink finir
    float minOpen = 1.0f;
    runFor(clk, 20000, [&]() { bc.update(); if (bc.openL() < minOpen) minOpen = bc.openL(); });
    TEST_ASSERT_TRUE(minOpen > 0.9f);
}

// Surprised : gel 2-4 s (pas de blink), PUIS reprise (blinks lents)
static void test_blink_surprised_freeze_then_resume() {
    FakeClock clk; Rng rng(1234);
    Tuning tn; BlinkController bc(clk, rng, tn);
    runFor(clk, 100, [&]() { bc.update(); });
    bc.setEmotion(Surprised, true);

    float minDuringFreeze = 1.0f;
    runFor(clk, 1900, [&]() { bc.update(); if (bc.openL() < minDuringFreeze) minDuringFreeze = bc.openL(); });
    TEST_ASSERT_TRUE(minDuringFreeze > 0.9f);   // gel : écarquillé

    float minAfter = 1.0f;
    runFor(clk, 40000, [&]() { bc.update(); if (bc.openL() < minAfter) minAfter = bc.openL(); });
    TEST_ASSERT_TRUE(minAfter < 0.1f);          // reprise : il recligne
}

// Wink : un seul œil se ferme, l'autre reste ouvert
static void test_blink_wink_single_eye() {
    FakeClock clk; Rng rng(99);
    Tuning tn; BlinkController bc(clk, rng, tn);
    bc.requestWink(true);   // gauche
    float minL = 1.0f, minR = 1.0f;
    runFor(clk, 200, [&]() {
        bc.update();
        if (bc.openL() < minL) minL = bc.openL();
        if (bc.openR() < minR) minR = bc.openR();
    });
    TEST_ASSERT_TRUE(minL < 0.1f);
    TEST_ASSERT_TRUE(minR > 0.5f);   // lag 80 ms mais pas de fermeture wink
}

// Sleepy : le cycle « lutte » descend sous 40 % (droop/chute) et ne remonte
// jamais à 100 % (plafond ~70 % hors sursaut complet à 95 %)
static void test_blink_sleepy_struggle() {
    FakeClock clk; Rng rng(2026);
    Tuning tn; BlinkController bc(clk, rng, tn);
    bc.setEmotion(Sleepy, false);
    float minOpen = 1.0f;
    uint32_t ticksFullyOpen = 0, ticks = 0;
    runFor(clk, 30000, [&]() {
        bc.update();
        ticks++;
        if (bc.openL() < minOpen) minOpen = bc.openL();
        if (bc.openL() > 0.97f) ticksFullyOpen++;
    });
    TEST_ASSERT_TRUE(minOpen < 0.1f);                    // les chutes ferment
    // Yeux grands ouverts < 15 % du temps (uniquement pics de sursaut)
    TEST_ASSERT_TRUE(ticksFullyOpen < ticks * 15 / 100);
}

// -----------------------------------------------------------------------
// IdleBehavior
// -----------------------------------------------------------------------

// Des saccades se produisent, le regard reste borné, et entre deux saccades
// le regard est stable (fixation — pas de flottement continu)
static void test_idle_fixation_saccade_pattern() {
    FakeClock clk; Rng rng(555);
    Tuning tn; IdleBehavior idle(clk, rng, tn);
    int saccades = 0;
    float maxX = 0.0f, maxY = 0.0f;
    runFor(clk, 30000, [&]() {
        if (idle.update()) saccades++;
        Vec2f g = idle.gaze();
        if (fabsf(g.x) > maxX) maxX = fabsf(g.x);
        if (fabsf(g.y) > maxY) maxY = fabsf(g.y);
    });
    TEST_ASSERT_TRUE(saccades >= 5);                     // ~7-30 en 30 s
    TEST_ASSERT_TRUE(maxX <= units::GAZE_MAX_X + 0.05f); // borné (+ overshoot)
    TEST_ASSERT_TRUE(maxY <= units::GAZE_MAX_Y + 0.05f);
}

// Pendant une FIXATION courte (< 2 s), le regard ne bouge pas (holds §3.0)
static void test_idle_fixation_is_stable() {
    FakeClock clk; Rng rng(31337);
    Tuning tn; IdleBehavior idle(clk, rng, tn);
    // Laisser passer la 1re saccade pour être en début de fixation
    while (!idle.update()) clk.advanceMs(10);
    runFor(clk, 300, [&]() { idle.update(); });   // finir saccade + overshoot
    Vec2f a = idle.gaze();
    runFor(clk, 400, [&]() { idle.update(); });   // fixation < 2 s : pas de jitter
    Vec2f b = idle.gaze();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, a.x, b.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, a.y, b.y);
}

// -----------------------------------------------------------------------
// EmotionRoulette
// -----------------------------------------------------------------------

// La roulette tire dans l'intervalle configuré, jamais une émotion de poids 0,
// et ne tire rien quand elle est verrouillée
static void test_roulette_interval_lock_weights() {
    FakeClock clk; Rng rng(777);
    EmotionRoulette r(clk, rng);
    r.setInterval(1000, 2000);

    // Poids exclusif sur Happy → seul tirage possible
    for (int i = 0; i < EMOTIONS_COUNT; i++) r.setWeight((eEmotions)i, 0.0f);
    r.setWeight(Happy, 1.0f);

    eEmotions cur = Normal, out;
    int draws = 0;
    runFor(clk, 20000, [&]() {
        if (r.update(cur, /*locked=*/false, out)) { draws++; cur = out; }
    });
    TEST_ASSERT_TRUE(draws >= 1);
    TEST_ASSERT_EQUAL_INT(Happy, cur);

    // Verrouillée : plus aucun tirage (mais le tick est consommé)
    int lockedDraws = 0;
    runFor(clk, 20000, [&]() {
        if (r.update(cur, /*locked=*/true, out)) lockedDraws++;
    });
    TEST_ASSERT_EQUAL_INT(0, lockedDraws);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_blink_autoblink_happens);
    RUN_TEST(test_blink_dead_blocks);
    RUN_TEST(test_blink_surprised_freeze_then_resume);
    RUN_TEST(test_blink_wink_single_eye);
    RUN_TEST(test_blink_sleepy_struggle);
    RUN_TEST(test_idle_fixation_saccade_pattern);
    RUN_TEST(test_idle_fixation_is_stable);
    RUN_TEST(test_roulette_interval_lock_weights);
    return UNITY_END();
}
