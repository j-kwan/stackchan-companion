// =============================================================================
// test_facestate — StackChan-Companion v2 : tests natifs du TripleBuffer FaceState
// =============================================================================
// Vérifie le contrat de publication sans verrou (engine/FaceState.h) :
//   - le lecteur voit toujours le DERNIER snapshot publié
//   - sans publication, read() re-rend le même snapshot (stabilité)
//   - les publications multiples entre deux lectures ne rendent que la
//     plus récente (c'est voulu — les événements ponctuels passent par la
//     CommandQueue, pas par le bus d'état)
//   - le pattern « compteur d'événements » (TripleBuffer<uint32_t>) survit
//     aux snapshots sautés — propriété générique du buffer
//
// NB : le vrai test de concurrence (2 cœurs) n'est pas simulable en natif —
// la sûreté vient de l'algorithme (échanges atomiques, possession exclusive
// des slots) ; ici on valide la LOGIQUE d'indices sur un seul thread.
//
// Exécution : .\scripts\gates\test-native.ps1 test_facestate
// =============================================================================

#include <unity.h>
#include "engine/FaceState.h"

using namespace sce;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// Publication simple : le lecteur voit la valeur publiée
static void test_publish_then_read() {
    TripleBuffer<FaceState> bus;
    FaceState& w = bus.beginWrite();
    w.emotion = Happy;
    w.gaze    = { 0.3f, -0.1f };
    bus.publish();

    const FaceState& r = bus.read();
    TEST_ASSERT_EQUAL_INT(Happy, r.emotion);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.3f, r.gaze.x);
}

// Sans nouvelle publication, read() reste stable sur le même snapshot
static void test_read_stable_without_publish() {
    TripleBuffer<FaceState> bus;
    bus.beginWrite().emotion = Angry;
    bus.publish();
    TEST_ASSERT_EQUAL_INT(Angry, bus.read().emotion);
    TEST_ASSERT_EQUAL_INT(Angry, bus.read().emotion);  // relecture : identique
}

// Plusieurs publications entre deux lectures → seule la dernière est vue
static void test_multiple_publish_latest_wins() {
    TripleBuffer<FaceState> bus;
    for (int i = 0; i < 5; i++) {
        bus.beginWrite().emotion = (eEmotions)i;
        bus.publish();
    }
    TEST_ASSERT_EQUAL_INT(4, (int)bus.read().emotion);
}

// Alternance écrire/lire prolongée : jamais de valeur périmée ni corrompue
static void test_interleaved_sequence() {
    TripleBuffer<FaceState> bus;
    for (int i = 0; i < 100; i++) {
        FaceState& w = bus.beginWrite();
        w.emotion = (eEmotions)(i % EMOTIONS_COUNT);
        w.openL   = (float)i;
        bus.publish();
        const FaceState& r = bus.read();
        TEST_ASSERT_EQUAL_INT(i % EMOTIONS_COUNT, (int)r.emotion);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)i, r.openL);
    }
}

// Pattern « compteur d'événements » (propriété générique du TripleBuffer,
// démontrée sur un uint32_t) : même si des snapshots sont sautés, le delta
// du compteur porte l'événement
static void test_event_counter_survives_skips() {
    TripleBuffer<uint32_t> bus;
    uint32_t lastSeen = 0;
    int triggered = 0;

    // 3 publications sans lecture entre elles (snapshots sautés)
    for (uint32_t seq = 1; seq <= 3; seq++) {
        bus.beginWrite() = seq;
        bus.publish();
    }
    // Le lecteur ne voit que le dernier snapshot, mais le compteur a bougé
    uint32_t r = bus.read();
    if (r != lastSeen) { triggered++; lastSeen = r; }

    TEST_ASSERT_EQUAL_INT(1, triggered);             // 1 déclenchement…
    TEST_ASSERT_EQUAL_UINT32(3, lastSeen);           // …aligné sur le dernier état
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_publish_then_read);
    RUN_TEST(test_read_stable_without_publish);
    RUN_TEST(test_multiple_publish_latest_wins);
    RUN_TEST(test_interleaved_sequence);
    RUN_TEST(test_event_counter_survives_skips);
    return UNITY_END();
}
