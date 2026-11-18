// =============================================================================
// test_pickup — StackChan-Companion v2 : détection soulevé/posé (PickupDetector)
// =============================================================================
// Profils accéléromètre synthétiques (ROADMAP §2.2 PickupReaction).
// Exécution : .\scripts\gates\test-native.ps1 test_pickup
// =============================================================================

#include <unity.h>
#include "engine/Clock.h"
#include "engine/Tuning.h"
#include "behavior/PickupDetector.h"

using namespace sce;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// Tick 10 ms pendant `ms`, compte les événements
static void feed(FakeClock& clk, PickupDetector& pd, uint32_t ms,
                 float accG, float gyro, int& lifts, int& downs) {
    for (uint32_t t = 0; t < ms; t += 10) {
        clk.advanceMs(10);
        auto ev = pd.update(accG, gyro);
        if (ev.lifted)  lifts++;
        if (ev.putDown) downs++;
    }
}

// Levée franche (1,4 g soutenu) → LIFTED une seule fois ;
// repos stable 1 s → posé une seule fois
static void test_pickup_lift_then_putdown() {
    FakeClock clk;
    Tuning tun;
    PickupDetector pd(clk, tun);
    int lifts = 0, downs = 0;

    feed(clk, pd, 500, 1.0f, 2.0f, lifts, downs);    // posé, calme
    TEST_ASSERT_EQUAL_INT(0, lifts);

    feed(clk, pd, 400, 1.4f, 10.0f, lifts, downs);   // levée + transport
    TEST_ASSERT_EQUAL_INT(1, lifts);
    TEST_ASSERT_TRUE(pd.isLifted());

    feed(clk, pd, 600, 1.02f, 2.0f, lifts, downs);   // calme mais < 1 s
    TEST_ASSERT_EQUAL_INT(0, downs);
    feed(clk, pd, 600, 1.02f, 2.0f, lifts, downs);   // calme > 1 s → posé
    TEST_ASSERT_EQUAL_INT(1, downs);
    TEST_ASSERT_FALSE(pd.isLifted());
    TEST_ASSERT_EQUAL_INT(1, lifts);                 // pas de re-déclenchement
}

// Impulsion brève (< 200 ms) → PAS de détection (choc, vibration)
static void test_pickup_short_bump_ignored() {
    FakeClock clk;
    Tuning tun;
    PickupDetector pd(clk, tun);
    int lifts = 0, downs = 0;
    feed(clk, pd, 100, 1.5f, 5.0f, lifts, downs);    // bump 100 ms
    feed(clk, pd, 500, 1.0f, 2.0f, lifts, downs);
    TEST_ASSERT_EQUAL_INT(0, lifts);
}

// Secousse violente (gyro dominant) → PAS un pickup (c'est le job de Scared)
static void test_pickup_shake_guard() {
    FakeClock clk;
    Tuning tun;
    PickupDetector pd(clk, tun);
    int lifts = 0, downs = 0;
    feed(clk, pd, 800, 1.6f, 120.0f, lifts, downs);  // secoué fort
    TEST_ASSERT_EQUAL_INT(0, lifts);
}

// Transport agité : une fois LIFTED, les variations ne re-déclenchent rien,
// et le posé n'arrive que sur du calme PROLONGÉ
static void test_pickup_carry_stability() {
    FakeClock clk;
    Tuning tun;
    PickupDetector pd(clk, tun);
    int lifts = 0, downs = 0;
    feed(clk, pd, 400, 1.3f, 20.0f, lifts, downs);   // levée
    TEST_ASSERT_EQUAL_INT(1, lifts);
    // Transport : alternance calme bref / mouvement — jamais 1 s de calme
    for (int i = 0; i < 10; i++) {
        feed(clk, pd, 400, 1.02f, 5.0f, lifts, downs);  // calme 400 ms
        feed(clk, pd, 300, 1.25f, 15.0f, lifts, downs); // remous
    }
    TEST_ASSERT_EQUAL_INT(0, downs);
    TEST_ASSERT_EQUAL_INT(1, lifts);
    TEST_ASSERT_TRUE(pd.isLifted());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_pickup_lift_then_putdown);
    RUN_TEST(test_pickup_short_bump_ignored);
    RUN_TEST(test_pickup_shake_guard);
    RUN_TEST(test_pickup_carry_stability);
    return UNITY_END();
}
