// =============================================================================
// test_units — StackChan-Companion v2 : tests natifs de engine/Units.h + Clock.h
// =============================================================================
// Premier test de la chaîne native (phase P0) : vérifie que les conversions
// normatives de docs/CONVENTIONS-V2.md sont encodées correctement, et que
// FakeClock se comporte comme attendu par les futures machines d'états.
//
// Exécution : pio test -e native
// Framework : Unity (fourni par PlatformIO).
//
// Ces tests documentent AUSSI les conventions par l'exemple : chaque cas
// nomme le scénario physique (« tête à gauche observateur ») pour servir de
// référence quand un doute de signe apparaît en P2-P4.
// =============================================================================

#include <unity.h>
#include "engine/Units.h"
#include "engine/Clock.h"

using namespace sce;
using namespace sce::units;

// Hooks Unity requis (framework C) — vides : aucun état partagé entre tests
extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// -----------------------------------------------------------------------
// gazeFromHead — accompagnement tête → yeux (convention CONVENTIONS-V2 §4)
// -----------------------------------------------------------------------

// Tête au centre, horizontale → regard neutre
static void test_gazeFromHead_neutral() {
    Vec2f g = gazeFromHead(YAW_CENTER, PITCH_NEUTRAL);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, g.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, g.y);
}

// Tête tournée à fond vers la GAUCHE observateur (yaw = centre - 45°)
// → les yeux accompagnent : gaze.x = -1 (gauche observateur, repère §1)
static void test_gazeFromHead_headLeft() {
    Vec2f g = gazeFromHead(YAW_CENTER - 45, PITCH_NEUTRAL);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -1.0f, g.x);
}

// Tête relevée de 25° (pitch = neutre - 25) → yeux vers le HAUT : gaze.y = +1
static void test_gazeFromHead_headUp() {
    Vec2f g = gazeFromHead(YAW_CENTER, PITCH_NEUTRAL - 25);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, g.y);
}

// Clamp : au-delà de ±YAW_FULL_GAZE le gaze sature à ±1
static void test_gazeFromHead_clamps() {
    Vec2f g = gazeFromHead(YAW_CENTER - 90, PITCH_NEUTRAL);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -1.0f, g.x);
}

// -----------------------------------------------------------------------
// headFromGaze — inverse exacte (head-follow §3.0)
// -----------------------------------------------------------------------

// Aller-retour EXACT tant que le regard n'est pas saturé, c'est-à-dire dans
// ±YAW_FULL_GAZE — et PAS sur toute la plage servo.
//
// Ce test affirmait l'aller-retour sur ±YAW_RANGE, ce qui n'était vrai que
// parce que YAW_RANGE (40) tombait SOUS YAW_FULL_GAZE (45) : une coïncidence
// de valeurs, pas une propriété. En élargissant le lacet à 130 le 2026-08-01,
// il a échoué immédiatement — et il avait raison de le faire, c'est la vraie
// conséquence du changement.
//
// Ce que cela veut dire, et ce n'est pas un défaut : le HEAD-FOLLOW suit le
// regard, le regard est borné à ±1, donc il ne commandera jamais au-delà de
// ±45°. La plage élargie sert aux CHORÉGRAPHIES, qui commandent le lacet
// directement sans passer par le regard.
static void test_head_gaze_roundtrip() {
    for (int yaw = YAW_CENTER - (int)YAW_FULL_GAZE;
         yaw <= YAW_CENTER + (int)YAW_FULL_GAZE; yaw += 5) {
        Vec2f g = gazeFromHead((float)yaw, PITCH_NEUTRAL);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)yaw, headYawFromGaze(g.x));
    }
    // Au-delà : le regard SATURE, donc l'inverse ramène à la butée du regard
    // — bornée, jamais un débordement ni un repli.
    for (int yaw = YAW_CENTER + (int)YAW_FULL_GAZE + 1;
         yaw <= YAW_CENTER + YAW_RANGE; yaw += 5) {
        Vec2f g = gazeFromHead((float)yaw, PITCH_NEUTRAL);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, g.x);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)YAW_CENTER + YAW_FULL_GAZE,
                                 headYawFromGaze(g.x));
    }
    // Pitch : plage utile [PITCH_NEUTRAL - 25, PITCH_NEUTRAL] (le servo ne
    // descend pas sous l'horizon → gaze.y ∈ [0, 1] côté tête)
    for (int pitch = PITCH_NEUTRAL - 25; pitch <= PITCH_NEUTRAL; pitch += 5) {
        Vec2f g = gazeFromHead(YAW_CENTER, (float)pitch);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)pitch, headPitchFromGaze(g.y));
    }
}

// -----------------------------------------------------------------------
// pxFromGaze — projection écran (seul point gaze → pixels)
// -----------------------------------------------------------------------

// gaze.x = +1 (droite observateur) → +GAZE_PX_X px (X écran croît vers la droite)
// gaze.y = +1 (haut)               → -GAZE_PX_Y px (Y écran croît vers le BAS)
static void test_pxFromGaze_signs() {
    Vec2f p = pxFromGaze({ 1.0f, 1.0f });
    TEST_ASSERT_FLOAT_WITHIN(1e-5f,  GAZE_PX_X, p.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -GAZE_PX_Y, p.y);
}

// -----------------------------------------------------------------------
// clampGaze — amplitudes maximales
// -----------------------------------------------------------------------
static void test_clampGaze() {
    Vec2f g = clampGaze({ 2.0f, -3.0f });
    TEST_ASSERT_FLOAT_WITHIN(1e-5f,  GAZE_MAX_X, g.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -GAZE_MAX_Y, g.y);
}

// -----------------------------------------------------------------------
// gazeFromLegacyConvention — import de données historiques
// convention historique : gazeH > 0 = GAUCHE observateur → gaze.x = -gazeH
// -----------------------------------------------------------------------
static void test_gazeFromLegacyConvention() {
    Vec2f g = gazeFromLegacyConvention(0.5f, -0.3f);  // mi-gauche, léger bas
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -0.5f, g.x);  // mi-gauche = -X
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -0.3f, g.y);  // V inchangé
}

// -----------------------------------------------------------------------
// FakeClock — support des futures machines d'états
// -----------------------------------------------------------------------
static void test_fakeClock_advance() {
    FakeClock clk;
    TEST_ASSERT_EQUAL_UINT32(0, clk.ms());
    clk.advanceMs(3500);
    TEST_ASSERT_EQUAL_UINT32(3500, clk.ms());
    TEST_ASSERT_EQUAL_UINT64(3500000ULL, clk.us());
    clk.advanceUs(250);
    TEST_ASSERT_EQUAL_UINT32(3500, clk.ms());   // 250 µs < 1 ms
    TEST_ASSERT_EQUAL_UINT64(3500250ULL, clk.us());
}

// Pattern wrap-safe : `now - start >= dur` reste vrai à travers le wrap 32-bit
static void test_wrapSafe_duration_pattern() {
    uint32_t start = 0xFFFFFF00u;             // 256 ms avant le wrap
    uint32_t now   = 0x00000100u;             // 256 ms après le wrap
    uint32_t elapsed = now - start;           // arithmétique non signée
    TEST_ASSERT_EQUAL_UINT32(512, elapsed);   // 256 + 256 — le wrap est transparent
}

// -----------------------------------------------------------------------
// Bandeau minuteur : l icone et les chiffres partagent UNE geometrie
// -----------------------------------------------------------------------
// Ces deux fonctions ne dessinent rien et ne lisent aucun ecran : elles
// DECIDENT ou est l icone de phase, et depuis le 08-25 elles decident aussi
// ce qu un tap voulait dire (a gauche des chiffres = l icone = changer la
// forme de la session ; sur les chiffres = depart/pause). Peintre et doigt
// lisent la meme source -- ce test est ce qui empeche l un des deux de
// repartir avec sa propre copie de la formule.
static void test_bandeau_icone_ne_touche_jamais_les_chiffres() {
    using namespace sce::units;
    // Tous les textes que le bandeau peut reellement afficher : "05:00" (5),
    // "4/4 FINI" (8), "1/4 25:00" (9), "1/4 120:00" (10) -- et deux extremes
    // pour border le raisonnement.
    for (int n = 4; n <= 12; n++) {
        const int icon = bandIconX0(n);
        const int text = bandTextX0(n);
        // L icone tient ENTIEREMENT a gauche du premier chiffre. Si elle
        // mordait dessus, le tap "icone" prendrait un bout des chiffres et le
        // dessin se superposerait -- une seule inegalite dit les deux.
        TEST_ASSERT_TRUE(icon + BAND_ICON_W <= text);
        // ...et pas collee : les 12 px d air voulus (30 = 18 + 12).
        TEST_ASSERT_EQUAL_INT(12, text - (icon + BAND_ICON_W));
    }
}

// L ASSEMBLAGE est centre, pas les chiffres (08-25). Avant, les chiffres
// etaient centres seuls et la vignette pendait a leur gauche : l objet
// REELLEMENT visible -- icone + air + horloge -- se retrouvait 15 px a gauche
// du centre, et la bande paraissait de travers. Le test compare les deux
// marges : c est la seule formulation qui ne se contente pas de reciter la
// formule qu elle est censee verifier.
static void test_bandeau_assemblage_centre() {
    using namespace sce::units;
    for (int n = 4; n <= 12; n++) {
        const int gauche = bandIconX0(n);
        const int droite = SCREEN_W - (bandTextX0(n) + n * BAND_CHAR_W);
        TEST_ASSERT_EQUAL_INT(gauche, droite);
    }
    // Et le decalage que drawDynText applique aux chiffres vaut EXACTEMENT
    // l ecart entre « centre tout seul » et « centre dans l assemblage » --
    // sinon le peintre et cette geometrie diraient deux endroits differents.
    for (int n = 4; n <= 12; n++)
        TEST_ASSERT_EQUAL_INT(bandTextX0(n),
                              (SCREEN_W - n * BAND_CHAR_W) / 2 + BAND_TEXT_DX);
}

// Le texte le plus LONG est celui qui pousse l icone le plus a gauche : c est
// lui qui dit si elle sort de l ecran. "1/4 120:00" (10) est le pire cas reel
// -- bloc de travail a 120 min, la borne haute de pomo_work_min.
static void test_bandeau_icone_reste_a_l_ecran() {
    using namespace sce::units;
    TEST_ASSERT_TRUE(bandIconX0(10) >= 0);
    TEST_ASSERT_TRUE(bandTextX0(10) + 10 * BAND_CHAR_W <= SCREEN_W);
    // L icone vit dans la BANDE, jamais dans la zone des yeux : c est ce qui
    // autorise le routage a ne consulter que startX() une fois startY() connu.
    TEST_ASSERT_TRUE(BAND_ICON_Y >= EYEZONE_H);
    TEST_ASSERT_TRUE(BAND_ICON_Y + BAND_ICON_H <= SCREEN_H);
}


// Le bandeau respire pareil en haut et en bas (08-25). La rangee de texte
// n est plus calee a 8 px du haut : elle est DERIVEE de ses deux voisins, la
// regle de progression au-dessus et la rangee de pastilles en dessous. Le
// test compare les deux ecarts plutot que de reciter la formule -- c est la
// seule formulation qui casse si quelqu un bouge un des deux rails sans
// repenser au troisieme.
static void test_bandeau_ecarts_verticaux_egaux() {
    using namespace sce::units;
    const int hautRegle = BAND_BAR_Y + BAND_BAR_H;      // 1re ligne libre
    const int encre0    = BAND_DYN_Y + BAND_TEXT_INK_DY;
    const int encre1    = encre0 + BAND_TEXT_INK_H - 1;
    const int dessus    = encre0 - hautRegle;
    const int dessous   = (BAND_STATUS_Y - 1) - encre1;
    // 35 rangees libres ne se coupent pas en deux : un ecart d UNE ligne est
    // la meilleure symetrie possible, davantage serait un desequilibre.
    const int d = dessus > dessous ? dessus - dessous : dessous - dessus;
    TEST_ASSERT_TRUE(d <= 1);
    // Et le texte tient entre les deux rails, sans mordre ni sur la regle ni
    // sur les pastilles.
    TEST_ASSERT_TRUE(encre0 >= hautRegle);
    TEST_ASSERT_TRUE(encre1 <  BAND_STATUS_Y);
}

// -----------------------------------------------------------------------
// main Unity
// -----------------------------------------------------------------------
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_gazeFromHead_neutral);
    RUN_TEST(test_gazeFromHead_headLeft);
    RUN_TEST(test_gazeFromHead_headUp);
    RUN_TEST(test_gazeFromHead_clamps);
    RUN_TEST(test_head_gaze_roundtrip);
    RUN_TEST(test_pxFromGaze_signs);
    RUN_TEST(test_clampGaze);
    RUN_TEST(test_gazeFromLegacyConvention);
    RUN_TEST(test_fakeClock_advance);
    RUN_TEST(test_wrapSafe_duration_pattern);
    RUN_TEST(test_bandeau_icone_ne_touche_jamais_les_chiffres);
    RUN_TEST(test_bandeau_assemblage_centre);
    RUN_TEST(test_bandeau_icone_reste_a_l_ecran);
    RUN_TEST(test_bandeau_ecarts_verticaux_egaux);
    return UNITY_END();
}
