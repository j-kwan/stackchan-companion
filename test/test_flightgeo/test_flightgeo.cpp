// =============================================================================
// test_flightgeo — flight-radar : géodésie, choix de tronçon, formatage
// =============================================================================
// Le bin invité `flight-radar` n'était couvert par AUCUN test : réseau, rendu
// et tactile ne sont pas testables hors cible, mais les fonctions qui portent
// la JUSTESSE (distances, relèvement, choix du tronçon d'une route
// multi-étapes, nettoyage des noms de villes) sont pures — elles vivent dans
// firmware/flight-radar/geo.h et se testent comme engine/ et behavior/.
//
// Le cas « pickLeg » est celui qui a motivé l'extraction : une route
// « LFPG-FIMP-FMEE » affichait toujours le premier tronçon, donc une origine
// et un ETA faux pour un avion déjà sur la seconde étape (revue 07-27).
//
// Exécution : pio test -e native
// =============================================================================

#include <unity.h>
#include "../../firmware/flight-radar/geo.h"

using namespace fr;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// Aéroports de référence (lat, lon)
static constexpr float CDG_LA =  49.0128f, CDG_LO =   2.5500f;   // Paris CDG
static constexpr float ORY_LA =  48.7233f, ORY_LO =   2.3794f;   // Paris Orly
static constexpr float JFK_LA =  40.6398f, JFK_LO = -73.7789f;   // New York
static constexpr float FIMP_LA = -20.4302f, FIMP_LO = 57.6836f;  // Maurice
static constexpr float FMEE_LA = -20.8871f, FMEE_LO = 55.5103f;  // Réunion

// ---------------------------------------------------------------- distances
void test_gcNm_courte_distance(void) {
    // CDG → ORY : ~34,5 km ≈ 18,6 nm (référence géodésique)
    float d = gcNm(CDG_LA, CDG_LO, ORY_LA, ORY_LO);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 18.6f, d);
}

void test_gcNm_transatlantique(void) {
    // CDG → JFK : 5 837 km ≈ 3 152 nm
    float d = gcNm(CDG_LA, CDG_LO, JFK_LA, JFK_LO);
    TEST_ASSERT_FLOAT_WITHIN(20.0f, 3152.0f, d);
}

void test_gcNm_symetrique_et_nul(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, gcNm(CDG_LA, CDG_LO, CDG_LA, CDG_LO));
    TEST_ASSERT_FLOAT_WITHIN(0.5f,
        gcNm(CDG_LA, CDG_LO, JFK_LA, JFK_LO),
        gcNm(JFK_LA, JFK_LO, CDG_LA, CDG_LO));
}

void test_planarNm_coherent_a_courte_portee(void) {
    // Sous 500 nm (portée max du radar) l'équirectangulaire doit rester
    // à quelques % de la haversine — c'est ce qui autorise son usage par frame.
    float g = gcNm(FIMP_LA, FIMP_LO, FMEE_LA, FMEE_LO);
    float p = planarNm(FIMP_LA, FIMP_LO, FMEE_LA, FMEE_LO);
    TEST_ASSERT_FLOAT_WITHIN(g * 0.02f, g, p);
}

// -------------------------------------------------------------- relèvement
void test_bearing_cardinal(void) {
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f,   bearingDeg(0.0f, 0.0f,  1.0f, 0.0f)); // N
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 90.0f,  bearingDeg(0.0f, 0.0f,  0.0f, 1.0f)); // E
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 180.0f, bearingDeg(0.0f, 0.0f, -1.0f, 0.0f)); // S
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 270.0f, bearingDeg(0.0f, 0.0f,  0.0f, -1.0f));// O
}

void test_bearing_toujours_positif(void) {
    // Cap vers l'ouest : atan2 rend un négatif, la fonction doit normaliser
    float b = bearingDeg(FMEE_LA, FMEE_LO, FIMP_LA, FIMP_LO);
    TEST_ASSERT_TRUE(b >= 0.0f && b < 360.0f);
}

// ------------------------------------------------- choix du tronçon (BUG)
void test_pickLeg_route_simple(void) {
    const float la[2] = { CDG_LA, FIMP_LA };
    const float lo[2] = { CDG_LO, FIMP_LO };
    // Un seul tronçon : toujours l'index 0, où que soit l'avion
    TEST_ASSERT_EQUAL_INT(0, pickLeg(la, lo, 2, 0.0f, 30.0f));
}

void test_pickLeg_premier_troncon(void) {
    // LFPG → FIMP → FMEE, avion au-dessus de l'Afrique (1re étape)
    const float la[3] = { CDG_LA, FIMP_LA, FMEE_LA };
    const float lo[3] = { CDG_LO, FIMP_LO, FMEE_LO };
    TEST_ASSERT_EQUAL_INT(0, pickLeg(la, lo, 3, 10.0f, 25.0f));
}

void test_pickLeg_second_troncon(void) {
    // MÊME route, mais l'avion est ENTRE Maurice et la Réunion : c'est le
    // cas que l'ancien code attribuait au premier tronçon (origine Paris,
    // ETA calculé vers Maurice alors qu'il en venait).
    const float la[3] = { CDG_LA, FIMP_LA, FMEE_LA };
    const float lo[3] = { CDG_LO, FIMP_LO, FMEE_LO };
    float midLa = (FIMP_LA + FMEE_LA) / 2.0f;
    float midLo = (FIMP_LO + FMEE_LO) / 2.0f;
    TEST_ASSERT_EQUAL_INT(1, pickLeg(la, lo, 3, midLa, midLo));
}

void test_pickLeg_pile_sur_une_escale(void) {
    // Avion posé à Maurice (point commun aux deux tronçons) : le coût est
    // nul des deux côtés — on doit rendre un index valide, sans osciller.
    const float la[3] = { CDG_LA, FIMP_LA, FMEE_LA };
    const float lo[3] = { CDG_LO, FIMP_LO, FMEE_LO };
    int i = pickLeg(la, lo, 3, FIMP_LA, FIMP_LO);
    TEST_ASSERT_TRUE(i == 0 || i == 1);
}

// -------------------------------------------------------------- formatage
void test_cityClean_retire_non_ascii(void) {
    // « Arnavutköy » en UTF-8 : le « ö » (2 octets) doit disparaître, la
    // fonte 6×8 le rendait en « ?? » et débordait du panneau.
    char s[32] = "Arnavutk\xC3\xB6y";
    cityClean(s, 14);
    TEST_ASSERT_EQUAL_STRING("Arnavutky", s);
}

void test_cityClean_tronque_a_la_largeur(void) {
    char s[64] = "Saint-Denis-de-la-Reunion";
    cityClean(s, 14);
    TEST_ASSERT_EQUAL_INT(14, (int)strlen(s));
    TEST_ASSERT_EQUAL_STRING("Saint-Denis-de", s);
}

void test_cityClean_vide_et_nul(void) {
    char s[4] = "";
    cityClean(s, 14);
    TEST_ASSERT_EQUAL_STRING("", s);
    cityClean(nullptr, 14);            // ne doit pas planter
}


// ============================ tafGroupCovers ================================
// A TAF states its periods as DAY-OF-MONTH + HOUR UTC, with no month and no
// year. Every case below is a real shape taken off the wire, plus the two
// boundaries the format makes easy to get wrong: hour 24 and the month wrap.

// The live FMEE forecast of 2026-08-01 17:00Z:
//   TAF FMEE 011700Z 0118/0224 ... PROB30 TEMPO 0118/0121 ...
//                                  PROB40 TEMPO 0215/0224 ...
static void test_taf_groupe_en_vigueur_et_hors_periode(void) {
    // 01 August, 19:00Z -> inside 0118/0121, outside 0215/0224
    TEST_ASSERT_TRUE (fr::tafGroupCovers("PROB30 TEMPO 0118/0121 3000 SHRA",
                                         2026, 8, 1, 19));
    TEST_ASSERT_FALSE(fr::tafGroupCovers("PROB40 TEMPO 0215/0224 3000 SHRA",
                                         2026, 8, 1, 19));
    // 02 August, 16:00Z -> the other way round
    TEST_ASSERT_FALSE(fr::tafGroupCovers("PROB30 TEMPO 0118/0121 3000 SHRA",
                                         2026, 8, 2, 16));
    TEST_ASSERT_TRUE (fr::tafGroupCovers("PROB40 TEMPO 0215/0224 3000 SHRA",
                                         2026, 8, 2, 16));
}

// Two consecutive groups share a boundary hour. An inclusive end lights both.
static void test_taf_fin_exclusive(void) {
    TEST_ASSERT_TRUE (fr::tafGroupCovers("BECMG 0112/0118", 2026, 8, 1, 17));
    TEST_ASSERT_FALSE(fr::tafGroupCovers("BECMG 0112/0118", 2026, 8, 1, 18));
    TEST_ASSERT_TRUE (fr::tafGroupCovers("TEMPO 0118/0124", 2026, 8, 1, 18));
}

// Hour 24 = midnight ENDING that day, not 00:00 starting it.
static void test_taf_heure_24(void) {
    TEST_ASSERT_TRUE (fr::tafGroupCovers("TEMPO 0215/0224", 2026, 8, 2, 23));
    // 03 August 00:00Z is the very end of 0224 -> excluded
    TEST_ASSERT_FALSE(fr::tafGroupCovers("TEMPO 0215/0224", 2026, 8, 3, 0));
}

// A TAF issued on the 31st runs into day 01 of the NEXT month.
static void test_taf_passage_de_mois(void) {
    // Issued 31 July, valid 3118/0124. At 31 July 20:00Z: in force.
    TEST_ASSERT_TRUE (fr::tafGroupCovers("TEMPO 3118/0124", 2026, 7, 31, 20));
    // At 01 August 10:00Z: still in force (day 01 = next month, not last).
    TEST_ASSERT_TRUE (fr::tafGroupCovers("TEMPO 3118/0124", 2026, 8, 1, 10));
    // At 01 August 23:00Z: past 0124 (= 02 August 00:00Z is the end)
    TEST_ASSERT_TRUE (fr::tafGroupCovers("TEMPO 3118/0124", 2026, 8, 1, 23));
    TEST_ASSERT_FALSE(fr::tafGroupCovers("TEMPO 3118/0124", 2026, 8, 2, 0));
}

// Nothing is guessed: a malformed or absent group is simply not a period.
static void test_taf_rien_a_lire(void) {
    TEST_ASSERT_FALSE(fr::tafGroupCovers(nullptr, 2026, 8, 1, 19));
    TEST_ASSERT_FALSE(fr::tafGroupCovers("", 2026, 8, 1, 19));
    TEST_ASSERT_FALSE(fr::tafGroupCovers("12012KT 9999 FEW025", 2026, 8, 1, 19));
    TEST_ASSERT_FALSE(fr::tafGroupCovers("TEMPO 011/0121", 2026, 8, 1, 19));
    TEST_ASSERT_FALSE(fr::tafGroupCovers("TEMPO 01180/0121", 2026, 8, 1, 19));
    // Reversed or empty period: not a period.
    TEST_ASSERT_FALSE(fr::tafGroupCovers("TEMPO 0121/0118", 2026, 8, 1, 19));
    TEST_ASSERT_FALSE(fr::tafGroupCovers("TEMPO 0118/0118", 2026, 8, 1, 19));
    // Impossible readings.
    TEST_ASSERT_FALSE(fr::tafGroupCovers("TEMPO 0025/0121", 2026, 8, 1, 19));
    TEST_ASSERT_FALSE(fr::tafGroupCovers("TEMPO 3218/3224", 2026, 8, 1, 19));
}

// The header validity is the same shape and must read the same way.
static void test_taf_validite_d_entete(void) {
    TEST_ASSERT_TRUE (fr::tafGroupCovers("TAF FMEE 011700Z 0118/0224 12012KT",
                                         2026, 8, 2, 3));
    TEST_ASSERT_FALSE(fr::tafGroupCovers("TAF FMEE 011700Z 0118/0224 12012KT",
                                         2026, 8, 2, 23 + 1));  // 03/00Z
}


// ---- ddhhRangeCovers: the BULLETIN's validity, not just the groups'.
// Factored out on 08-02 because the prevailing-conditions line carries NO
// range of its own: it lives in the header, stripped from the body. Without
// this it could never be marked in force, and the screen showed no bar at
// all once the TEMPO had passed — which is most of the time.
void test_ddhh_bulletin_en_vigueur(void) {
    // FMEE's real TAF of 2026-08-02: issued 021700Z, valid 0218/0324.
    TEST_ASSERT_FALSE(ddhhRangeCovers(2,18, 3,24, 2026,8,2,17));  // not yet
    TEST_ASSERT_TRUE (ddhhRangeCovers(2,18, 3,24, 2026,8,2,18));  // start incl.
    TEST_ASSERT_TRUE (ddhhRangeCovers(2,18, 3,24, 2026,8,3,23));
    TEST_ASSERT_FALSE(ddhhRangeCovers(2,18, 3,24, 2026,8,4, 0));  // end excl.
}

// Hour 24 is legal in a TAF and means midnight at the END of the day.
void test_ddhh_heure_24(void) {
    TEST_ASSERT_TRUE (ddhhRangeCovers(2,18, 2,24, 2026,8,2,23));
    TEST_ASSERT_FALSE(ddhhRangeCovers(2,18, 2,24, 2026,8,3, 0));
}

// Month rollover: a 3106/0112 range seen on the 1st of the next month.
void test_ddhh_passage_de_mois(void) {
    TEST_ASSERT_TRUE (ddhhRangeCovers(31,6, 1,12, 2026,8,1,10));
    TEST_ASSERT_FALSE(ddhhRangeCovers(31,6, 1,12, 2026,8,1,12));  // end excl.
}

// An empty or inverted range is not a period.
void test_ddhh_periode_invalide(void) {
    TEST_ASSERT_FALSE(ddhhRangeCovers(2,18, 2,18, 2026,8,2,18));
    TEST_ASSERT_FALSE(ddhhRangeCovers(2,20, 2,18, 2026,8,2,19));
    TEST_ASSERT_FALSE(ddhhRangeCovers(0,18, 2,24, 2026,8,2,19));  // day 0
    TEST_ASSERT_FALSE(ddhhRangeCovers(2,25, 2,24, 2026,8,2,19));  // hour 25
}

// tafGroupCovers MUST stay in agreement with the function it delegates to.
void test_ddhh_coherent_avec_tafGroupCovers(void) {
    const char* g = "TEMPO 0218/0306 3000 SHRA SCT020TCU";
    for (int h = 15; h <= 23; h++)
        TEST_ASSERT_EQUAL_INT(ddhhRangeCovers(2,18, 3,6, 2026,8,2,h),
                              tafGroupCovers(g, 2026,8,2,h));
}

// ---- legReversed: the announced leg flown the other way ---------------------
// Cruise: track pointing away from the announced destination convicts.
void test_legReversed_croisiere_a_contresens(void) {
    // SS636 case (07-28): real track 309, bearing to announced dest 130.
    TEST_ASSERT_TRUE (fr::legReversed(true, 450, 309, 130, 35000, 0, 2000, 5000));
    // Same aircraft flying TOWARD its announced destination: kept.
    TEST_ASSERT_FALSE(fr::legReversed(true, 450, 130, 130, 35000, 0, 2000, 5000));
}

// The 150 kt floor and the missing-track flag both HOLD the track test.
void test_legReversed_garde_du_test_de_cap(void) {
    TEST_ASSERT_FALSE(fr::legReversed(true, 140, 309, 130, 35000, 0, 2000, 5000));
    TEST_ASSERT_FALSE(fr::legReversed(false, 450, 309, 130, 35000, 0, 2000, 5000));
}

// THE 08-04 REPORT: an aircraft descending onto RUN while the panel said
// RUN->Marseille. Approach speed sits under the track test's floor; the
// phase-of-flight test convicts on descent + distances alone.
void test_legReversed_atterrissage_sur_l_origine_annoncee(void) {
    // 130 kt final, 2500 ft, -700 ft/min, 8 nm from "origin", 5000 nm from
    // "destination": reversed, track unavailable or not.
    TEST_ASSERT_TRUE(fr::legReversed(false, 130, 0, 0, 2500, -700, 8, 5000));
    TEST_ASSERT_TRUE(fr::legReversed(true,  130, 310, 40, 2500, -700, 8, 5000));
}

// The mirror: climbing OUT of the announced destination.
void test_legReversed_decollage_de_la_destination_annoncee(void) {
    TEST_ASSERT_TRUE(fr::legReversed(false, 145, 0, 0, 4000, 1500, 5000, 12));
}

// What must NOT convict: a normal departure from the announced ORIGIN
// (climbing near origin), a normal arrival AT the announced destination
// (descending near dest), and level flight near the origin (no descent).
void test_legReversed_phases_normales_gardees(void) {
    TEST_ASSERT_FALSE(fr::legReversed(false, 145, 0, 0, 4000,  1500, 12, 5000));
    TEST_ASSERT_FALSE(fr::legReversed(false, 130, 0, 0, 2500,  -700, 5000, 8));
    TEST_ASSERT_FALSE(fr::legReversed(false, 130, 0, 0, 2500,     0, 8, 5000));
}

// ---------------------------------------------------------------- harness
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_gcNm_courte_distance);
    RUN_TEST(test_gcNm_transatlantique);
    RUN_TEST(test_gcNm_symetrique_et_nul);
    RUN_TEST(test_planarNm_coherent_a_courte_portee);
    RUN_TEST(test_bearing_cardinal);
    RUN_TEST(test_bearing_toujours_positif);
    RUN_TEST(test_pickLeg_route_simple);
    RUN_TEST(test_pickLeg_premier_troncon);
    RUN_TEST(test_pickLeg_second_troncon);
    RUN_TEST(test_pickLeg_pile_sur_une_escale);
    RUN_TEST(test_cityClean_retire_non_ascii);
    RUN_TEST(test_cityClean_tronque_a_la_largeur);
    RUN_TEST(test_cityClean_vide_et_nul);
    RUN_TEST(test_taf_groupe_en_vigueur_et_hors_periode);
    RUN_TEST(test_taf_fin_exclusive);
    RUN_TEST(test_taf_heure_24);
    RUN_TEST(test_taf_passage_de_mois);
    RUN_TEST(test_taf_rien_a_lire);
    RUN_TEST(test_taf_validite_d_entete);
    RUN_TEST(test_ddhh_bulletin_en_vigueur);
    RUN_TEST(test_ddhh_heure_24);
    RUN_TEST(test_ddhh_passage_de_mois);
    RUN_TEST(test_ddhh_periode_invalide);
    RUN_TEST(test_legReversed_croisiere_a_contresens);
    RUN_TEST(test_legReversed_garde_du_test_de_cap);
    RUN_TEST(test_legReversed_atterrissage_sur_l_origine_annoncee);
    RUN_TEST(test_legReversed_decollage_de_la_destination_annoncee);
    RUN_TEST(test_legReversed_phases_normales_gardees);
    RUN_TEST(test_ddhh_coherent_avec_tafGroupCovers);
    return UNITY_END();
}
