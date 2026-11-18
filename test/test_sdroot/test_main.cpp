// =============================================================================
// test_sdroot — the stackchan-eyes -> stackchan-companion SD migration decision
// =============================================================================
// shouldMigrate() is the one branch that decides whether a card gets touched
// at all. The failure mode worth pinning down is not "returns the wrong
// bool" in the abstract — it is "migrates INTO an already-populated new
// directory", which on a real FAT rename would either fail (both names
// exist) or, worse on some backends, silently merge/overwrite. The three
// cases below are exhaustive over the only two facts the decision has: does
// the new path exist, does the old one.
// =============================================================================

#include <unity.h>
#include "../../firmware/common/SdRoot.h"

using namespace sce;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

static void test_carte_neuve_rien_a_deplacer(void) {
    TEST_ASSERT_FALSE(shouldMigrate(/*newExists=*/false, /*oldExists=*/false));
}

static void test_deja_migree_le_vieux_chemin_reste_intouche(void) {
    // Even if the old directory somehow still exists (a partial migration,
    // a card shared between two cards' worth of history) — the new path
    // existing is what stops the migration cold, never a guess about
    // which one is "more current".
    TEST_ASSERT_FALSE(shouldMigrate(/*newExists=*/true, /*oldExists=*/false));
    TEST_ASSERT_FALSE(shouldMigrate(/*newExists=*/true, /*oldExists=*/true));
}

static void test_le_seul_cas_reel_migrer(void) {
    TEST_ASSERT_TRUE(shouldMigrate(/*newExists=*/false, /*oldExists=*/true));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_carte_neuve_rien_a_deplacer);
    RUN_TEST(test_deja_migree_le_vieux_chemin_reste_intouche);
    RUN_TEST(test_le_seul_cas_reel_migrer);
    return UNITY_END();
}
