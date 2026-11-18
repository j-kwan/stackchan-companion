// =============================================================================
// test_entsel — ha-remote: which entities survive a table that is too small
// =============================================================================
// `MAX_ENT = 64` and a real Home Assistant serves several hundred entities.
// The rule that decides who stays is pure (firmware/ha-remote/entsel.h) and it
// is worth testing for a blunt reason: when it is wrong, NOTHING looks wrong on
// target. The screen shows four counters, all plausible, and a category that
// was silently emptied is indistinguishable from a category you really do not
// own. The failure has no symptom — which is exactly the kind that has to fail
// on a PC instead.
//
// The case that motivates the file is the one from the backlog: an installation
// that serves four hundred `light.*` before the first `cover.*`. Filling in
// arrival order kept zero covers.
//
// Run: pio test -e native
// =============================================================================

#include <unity.h>
#include "../../firmware/ha-remote/entsel.h"

using namespace ha;

// Same four categories as the bin, same order (it is the tie-breaker when the
// budget cannot be split evenly).
enum { COVER = 0, LIGHT, SWITCH, CAMERA, NCAT };

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// ---------------------------------------------------------------- helpers
// Declares `n` candidates of one (category, tier) in pass 1.
static void offerN(Selector& s, int cat, Tier t, int n) {
    for (int i = 0; i < n; i++) s.offer(cat, t);
}
// Replays pass 2 for `n` identical candidates and returns how many got in.
static int admitN(Selector& s, int cat, Tier t, int n) {
    int k = 0;
    for (int i = 0; i < n; i++) if (s.admit(cat, t)) k++;
    return k;
}

// ------------------------------------------------------------------ isStale
void test_stale_etats_non_repondants(void) {
    TEST_ASSERT_TRUE(isStale("unavailable"));
    TEST_ASSERT_TRUE(isStale("unknown"));
    TEST_ASSERT_TRUE(isStale("none"));
    // An absent state is a broken entity, not a discreet one: Home Assistant
    // always publishes one.
    TEST_ASSERT_TRUE(isStale(""));
    TEST_ASSERT_TRUE(isStale(nullptr));
}

void test_stale_etats_ordinaires(void) {
    TEST_ASSERT_FALSE(isStale("on"));
    TEST_ASSERT_FALSE(isStale("off"));
    TEST_ASSERT_FALSE(isStale("open"));
    TEST_ASSERT_FALSE(isStale("closed"));
    TEST_ASSERT_FALSE(isStale("idle"));
}

// ----------------------------------------------------------------- isPinned
void test_pin_liste_vide_ne_retient_rien(void) {
    TEST_ASSERT_FALSE(isPinned("", "cover.salon"));
    TEST_ASSERT_FALSE(isPinned(nullptr, "cover.salon"));
    TEST_ASSERT_FALSE(isPinned("   ", "cover.salon"));
}

void test_pin_identifiant_exact(void) {
    TEST_ASSERT_TRUE (isPinned("cover.salon", "cover.salon"));
    TEST_ASSERT_FALSE(isPinned("cover.salon", "cover.chambre"));
}

void test_pin_prefixe_epingle_un_domaine(void) {
    // The whole point of the prefix rule: "my covers matter" in one token
    // instead of forty identifiers.
    TEST_ASSERT_TRUE(isPinned("cover.", "cover.salon"));
    TEST_ASSERT_TRUE(isPinned("cover.", "cover.chambre"));
    TEST_ASSERT_FALSE(isPinned("cover.", "light.salon"));
}

void test_pin_liste_multiple_et_espaces(void) {
    const char* l = " cover.salon , light.cuisine ;camera.portail ";
    TEST_ASSERT_TRUE(isPinned(l, "cover.salon"));
    TEST_ASSERT_TRUE(isPinned(l, "light.cuisine"));
    TEST_ASSERT_TRUE(isPinned(l, "camera.portail"));
    TEST_ASSERT_FALSE(isPinned(l, "switch.prise"));
}

void test_pin_insensible_a_la_casse(void) {
    // HA identifiers are lowercase; a list typed by hand is not always.
    TEST_ASSERT_TRUE(isPinned("Cover.Salon", "cover.salon"));
    TEST_ASSERT_TRUE(isPinned("cover.salon", "COVER.SALON"));
}

void test_pin_jeton_plus_long_que_id_ne_matche_pas(void) {
    // "cover.salon_nord" must not pin "cover.salon": the comparison stops at
    // the end of the identifier, and a partial walk is not a match.
    TEST_ASSERT_FALSE(isPinned("cover.salon_nord", "cover.salon"));
}

void test_pin_espace_seul_separe_les_jetons(void) {
    // THE bug: whitespace was skipped BETWEEN tokens but never ENDED one, so
    // `light. cover.` was one token of thirteen characters that matched
    // nothing and pinned nothing — silently. A space separates, like a comma.
    TEST_ASSERT_TRUE(isPinned("light. cover.", "light.cuisine"));
    TEST_ASSERT_TRUE(isPinned("light. cover.", "cover.salon"));
    TEST_ASSERT_FALSE(isPinned("light. cover.", "switch.prise"));
}

void test_pin_tabulations_et_espaces_multiples(void) {
    // Tabs and runs of spaces are separators too, and a list copied out of an
    // editor is full of both.
    const char* l = "\tcover.salon\t\tlight.cuisine   camera.portail\t";
    TEST_ASSERT_TRUE(isPinned(l, "cover.salon"));
    TEST_ASSERT_TRUE(isPinned(l, "light.cuisine"));
    TEST_ASSERT_TRUE(isPinned(l, "camera.portail"));
    TEST_ASSERT_FALSE(isPinned(l, "switch.prise"));
}

void test_pin_jetons_vides_et_separateurs_multiples(void) {
    // Empty tokens are skipped, not matched: an empty token compared as a
    // prefix would match EVERYTHING and pin the whole installation.
    TEST_ASSERT_TRUE (isPinned(",,; ;,cover.salon ,, ;", "cover.salon"));
    TEST_ASSERT_FALSE(isPinned(",,; ;,cover.salon ,, ;", "light.cuisine"));
    TEST_ASSERT_FALSE(isPinned(",", "cover.salon"));
    TEST_ASSERT_FALSE(isPinned(" ; , \t", "cover.salon"));
    TEST_ASSERT_FALSE(isPinned("\t\t", "cover.salon"));
}

// ---------------------------------------------------- capacity not reached
void test_sous_capacite_tout_passe(void) {
    Selector s;
    s.reset(64, NCAT);
    offerN(s, COVER,  TIER_LIVE, 5);
    offerN(s, LIGHT,  TIER_LIVE, 12);
    offerN(s, SWITCH, TIER_STALE, 3);
    s.plan();
    TEST_ASSERT_EQUAL_INT(20, s.seen());
    TEST_ASSERT_EQUAL_INT(20, s.kept());
    TEST_ASSERT_EQUAL_INT(0,  s.dropped());
    TEST_ASSERT_EQUAL_INT(5,  admitN(s, COVER,  TIER_LIVE, 5));
    TEST_ASSERT_EQUAL_INT(12, admitN(s, LIGHT,  TIER_LIVE, 12));
    TEST_ASSERT_EQUAL_INT(3,  admitN(s, SWITCH, TIER_STALE, 3));
}

// -------------------------------------------- the case from the backlog
void test_categorie_ecrasante_ne_vide_pas_les_autres(void) {
    // 400 lights served BEFORE 4 covers. Arrival order kept zero covers; the
    // fair share keeps all four, and the lights lose only what they must.
    Selector s;
    s.reset(64, NCAT);
    offerN(s, LIGHT, TIER_LIVE, 400);
    offerN(s, COVER, TIER_LIVE, 4);
    s.plan();
    TEST_ASSERT_EQUAL_INT(404, s.seen());
    TEST_ASSERT_EQUAL_INT(64,  s.kept());
    TEST_ASSERT_EQUAL_INT(340, s.dropped());
    // Pass 2 replays HA's order: the lights come first and must not be able to
    // eat the covers' slots.
    TEST_ASSERT_EQUAL_INT(60, admitN(s, LIGHT, TIER_LIVE, 400));
    TEST_ASSERT_EQUAL_INT(4,  admitN(s, COVER, TIER_LIVE, 4));
    TEST_ASSERT_EQUAL_INT(0,  s.droppedIn(COVER));
    TEST_ASSERT_EQUAL_INT(340, s.droppedIn(LIGHT));
}

void test_partage_equitable_quatre_categories(void) {
    // Everyone wants more than its share: 64 / 4 = 16 each, exactly.
    Selector s;
    s.reset(64, NCAT);
    for (int c = 0; c < NCAT; c++) offerN(s, c, TIER_LIVE, 30);
    s.plan();
    TEST_ASSERT_EQUAL_INT(64, s.kept());
    for (int c = 0; c < NCAT; c++) {
        TEST_ASSERT_EQUAL_INT(16, admitN(s, c, TIER_LIVE, 30));
        TEST_ASSERT_EQUAL_INT(14, s.droppedIn(c));
    }
}

void test_redistribution_des_restes(void) {
    // Two modest categories (2 and 3), two greedy ones. What the modest ones
    // do not use goes back to the others instead of being lost.
    Selector s;
    s.reset(20, NCAT);
    offerN(s, COVER,  TIER_LIVE, 2);
    offerN(s, LIGHT,  TIER_LIVE, 3);
    offerN(s, SWITCH, TIER_LIVE, 50);
    offerN(s, CAMERA, TIER_LIVE, 50);
    s.plan();
    TEST_ASSERT_EQUAL_INT(20, s.kept());
    TEST_ASSERT_EQUAL_INT(2,  admitN(s, COVER,  TIER_LIVE, 2));
    TEST_ASSERT_EQUAL_INT(3,  admitN(s, LIGHT,  TIER_LIVE, 3));
    // 15 left for two categories: 7 and 8 (the odd slot goes to the earlier
    // category, deterministically).
    int sw = admitN(s, SWITCH, TIER_LIVE, 50);
    int ca = admitN(s, CAMERA, TIER_LIVE, 50);
    TEST_ASSERT_EQUAL_INT(15, sw + ca);
    TEST_ASSERT_TRUE(sw >= 7 && ca >= 7);
}

void test_budget_plus_petit_que_le_nombre_de_categories(void) {
    // 2 slots, 4 claimants: one each in category order, and no double-serving.
    Selector s;
    s.reset(2, NCAT);
    for (int c = 0; c < NCAT; c++) offerN(s, c, TIER_LIVE, 10);
    s.plan();
    TEST_ASSERT_EQUAL_INT(2, s.kept());
    TEST_ASSERT_EQUAL_INT(1, admitN(s, COVER,  TIER_LIVE, 10));
    TEST_ASSERT_EQUAL_INT(1, admitN(s, LIGHT,  TIER_LIVE, 10));
    TEST_ASSERT_EQUAL_INT(0, admitN(s, SWITCH, TIER_LIVE, 10));
    TEST_ASSERT_EQUAL_INT(0, admitN(s, CAMERA, TIER_LIVE, 10));
}

// ------------------------------------------------------------------- tiers
void test_vivant_avant_indisponible(void) {
    // 4 slots, 4 unavailable lights and 4 answering ones. An unavailable entity
    // shows nothing and commands nothing: it yields.
    Selector s;
    s.reset(4, NCAT);
    offerN(s, LIGHT, TIER_STALE, 4);
    offerN(s, LIGHT, TIER_LIVE,  4);
    s.plan();
    TEST_ASSERT_EQUAL_INT(4, s.kept());
    TEST_ASSERT_EQUAL_INT(0, admitN(s, LIGHT, TIER_STALE, 4));
    TEST_ASSERT_EQUAL_INT(4, admitN(s, LIGHT, TIER_LIVE,  4));
    TEST_ASSERT_EQUAL_INT(4, s.droppedIn(LIGHT));
}

void test_indisponible_garde_sa_place_s_il_reste_de_la_room(void) {
    // Not answering does not mean not yours: while there is room it stays
    // listed, otherwise a light switched off at the wall would disappear from
    // the remote that is supposed to switch it back on.
    Selector s;
    s.reset(64, NCAT);
    offerN(s, LIGHT, TIER_STALE, 4);
    offerN(s, LIGHT, TIER_LIVE,  4);
    s.plan();
    TEST_ASSERT_EQUAL_INT(8, s.kept());
    TEST_ASSERT_EQUAL_INT(4, admitN(s, LIGHT, TIER_STALE, 4));
}

void test_epingle_prime_sur_toute_heuristique(void) {
    // 2 slots. One pinned camera among a hundred live covers: the user's own
    // choice outranks the fair share AND the liveness rule.
    Selector s;
    s.reset(2, NCAT);
    offerN(s, COVER,  TIER_LIVE,   100);
    offerN(s, CAMERA, TIER_PINNED, 1);
    s.plan();
    TEST_ASSERT_EQUAL_INT(2, s.kept());
    TEST_ASSERT_EQUAL_INT(1, admitN(s, CAMERA, TIER_PINNED, 1));
    TEST_ASSERT_EQUAL_INT(1, admitN(s, COVER,  TIER_LIVE,   100));
}

void test_epingle_indisponible_reste_epingle(void) {
    // A pinned entity that is unavailable keeps its slot: the user asked for
    // it by name, and hiding it would look like it no longer exists in HA.
    Selector s;
    s.reset(1, NCAT);
    offerN(s, LIGHT, TIER_PINNED, 1);
    offerN(s, LIGHT, TIER_LIVE,   10);
    s.plan();
    TEST_ASSERT_EQUAL_INT(1, admitN(s, LIGHT, TIER_PINNED, 1));
    TEST_ASSERT_EQUAL_INT(0, admitN(s, LIGHT, TIER_LIVE,   10));
}

void test_epingles_au_dela_de_la_capacite(void) {
    // More pinned entities than slots: they are shared fairly among themselves
    // and NOTHING else gets in — but the count still says how many are missing.
    Selector s;
    s.reset(4, NCAT);
    offerN(s, COVER, TIER_PINNED, 10);
    offerN(s, LIGHT, TIER_PINNED, 10);
    offerN(s, LIGHT, TIER_LIVE,   10);
    s.plan();
    TEST_ASSERT_EQUAL_INT(4,  s.kept());
    TEST_ASSERT_EQUAL_INT(26, s.dropped());
    TEST_ASSERT_EQUAL_INT(2,  admitN(s, COVER, TIER_PINNED, 10));
    TEST_ASSERT_EQUAL_INT(2,  admitN(s, LIGHT, TIER_PINNED, 10));
    TEST_ASSERT_EQUAL_INT(0,  admitN(s, LIGHT, TIER_LIVE,   10));
}

// ------------------------------------------------------------------- edges
void test_capacite_nulle(void) {
    Selector s;
    s.reset(0, NCAT);
    offerN(s, LIGHT, TIER_PINNED, 3);
    s.plan();
    TEST_ASSERT_EQUAL_INT(0, s.kept());
    TEST_ASSERT_EQUAL_INT(3, s.dropped());
    TEST_ASSERT_FALSE(s.admit(LIGHT, TIER_PINNED));
}

void test_categorie_hors_bornes_ignoree(void) {
    // A miscount must not silently steal slots from a real category.
    Selector s;
    s.reset(10, NCAT);
    s.offer(-1, TIER_LIVE);
    s.offer(99, TIER_LIVE);
    offerN(s, COVER, TIER_LIVE, 2);
    s.plan();
    TEST_ASSERT_EQUAL_INT(2, s.seen());
    TEST_ASSERT_EQUAL_INT(2, s.kept());
    TEST_ASSERT_FALSE(s.admit(99, TIER_LIVE));
}

void test_reset_efface_le_verdict_precedent(void) {
    // fetchStates() reuses the selector on every poll; a leftover demand would
    // shrink the next catalogue for no visible reason.
    Selector s;
    s.reset(4, NCAT);
    offerN(s, LIGHT, TIER_LIVE, 100);
    s.plan();
    TEST_ASSERT_EQUAL_INT(96, s.dropped());
    s.reset(64, NCAT);
    offerN(s, LIGHT, TIER_LIVE, 3);
    s.plan();
    TEST_ASSERT_EQUAL_INT(3, s.seen());
    TEST_ASSERT_EQUAL_INT(3, s.kept());
    TEST_ASSERT_EQUAL_INT(0, s.dropped());
    TEST_ASSERT_EQUAL_INT(0, s.droppedIn(LIGHT));
}

void test_seenIn_par_categorie(void) {
    // The home screen shows a "+n" per card: it comes from droppedIn(), and
    // kept + dropped must add back up to what Home Assistant really served.
    Selector s;
    s.reset(4, NCAT);
    offerN(s, COVER, TIER_LIVE,  6);
    offerN(s, LIGHT, TIER_STALE, 2);
    s.plan();
    TEST_ASSERT_EQUAL_INT(6, s.seenIn(COVER));
    TEST_ASSERT_EQUAL_INT(2, s.seenIn(LIGHT));
    TEST_ASSERT_EQUAL_INT(0, s.seenIn(SWITCH));
    // 4 slots: the 6 live covers take them all, the 2 unavailable lights are
    // dropped, and every card can state its own shortfall.
    int keptCover = admitN(s, COVER, TIER_LIVE,  6);
    int keptLight = admitN(s, LIGHT, TIER_STALE, 2);
    TEST_ASSERT_EQUAL_INT(6, keptCover + s.droppedIn(COVER));
    TEST_ASSERT_EQUAL_INT(2, keptLight + s.droppedIn(LIGHT));
    TEST_ASSERT_EQUAL_INT(4, keptCover + keptLight);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_stale_etats_non_repondants);
    RUN_TEST(test_stale_etats_ordinaires);
    RUN_TEST(test_pin_liste_vide_ne_retient_rien);
    RUN_TEST(test_pin_identifiant_exact);
    RUN_TEST(test_pin_prefixe_epingle_un_domaine);
    RUN_TEST(test_pin_liste_multiple_et_espaces);
    RUN_TEST(test_pin_insensible_a_la_casse);
    RUN_TEST(test_pin_jeton_plus_long_que_id_ne_matche_pas);
    RUN_TEST(test_pin_espace_seul_separe_les_jetons);
    RUN_TEST(test_pin_tabulations_et_espaces_multiples);
    RUN_TEST(test_pin_jetons_vides_et_separateurs_multiples);
    RUN_TEST(test_sous_capacite_tout_passe);
    RUN_TEST(test_categorie_ecrasante_ne_vide_pas_les_autres);
    RUN_TEST(test_partage_equitable_quatre_categories);
    RUN_TEST(test_redistribution_des_restes);
    RUN_TEST(test_budget_plus_petit_que_le_nombre_de_categories);
    RUN_TEST(test_vivant_avant_indisponible);
    RUN_TEST(test_indisponible_garde_sa_place_s_il_reste_de_la_room);
    RUN_TEST(test_epingle_prime_sur_toute_heuristique);
    RUN_TEST(test_epingle_indisponible_reste_epingle);
    RUN_TEST(test_epingles_au_dela_de_la_capacite);
    RUN_TEST(test_capacite_nulle);
    RUN_TEST(test_categorie_hors_bornes_ignoree);
    RUN_TEST(test_reset_efface_le_verdict_precedent);
    RUN_TEST(test_seenIn_par_categorie);
    return UNITY_END();
}
