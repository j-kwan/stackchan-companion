// =============================================================================
// test_geometry — StackChan-Companion v2 : tests natifs de engine/EyeGeometry.h
// =============================================================================
// Vérifie le correctif §2.3 du plan (« ronds des coins qui débordent ») via
// les invariants du contrat normalize() (I1..I5) :
//   - cas nominaux : les presets valides ne sont PAS modifiés
//   - cas de débordement v1 reproduits : contrainte inter-yeux (Width clampé
//     sans les rayons), blink (Width interpolé ≠ courbe des rayons)
//   - balayage brut : pour un large éventail de configs (y compris absurdes),
//     les invariants tiennent TOUJOURS — c'est la version native du
//     « balayage blink×gaze×transition » demandé en validation P1.
//
// Exécution : .\scripts\gates\test-native.ps1 test_geometry
// =============================================================================

#include <unity.h>
#include "engine/EyeConfig.h"
#include "engine/EyeGeometry.h"

using namespace sce;
using namespace sce::eyegeom;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

// Équivalent Preset_Normal v1 (100×100, rayons 25) — référence valide
static EyeConfig presetNormal() {
    EyeConfig c;
    c.Width = 100; c.Height = 100;
    c.Radius_Top = 25; c.Radius_Bottom = 25;
    return c;
}

// Vérifie les invariants I1..I5 du contrat normalize()
static void assertInvariants(const EyeConfig& c) {
    TEST_ASSERT_GREATER_OR_EQUAL_INT16(0, c.Width);                       // I1
    TEST_ASSERT_GREATER_OR_EQUAL_INT16(0, c.Height);
    TEST_ASSERT_GREATER_OR_EQUAL_INT16(0, c.Radius_Top);                  // I5
    TEST_ASSERT_GREATER_OR_EQUAL_INT16(0, c.Radius_Bottom);
    TEST_ASSERT_GREATER_OR_EQUAL_INT16(0, c.Inverse_Radius_Top);
    TEST_ASSERT_GREATER_OR_EQUAL_INT16(0, c.Inverse_Radius_Bottom);
    if (c.Radius_Top + c.Radius_Bottom > 0) {                             // I2
        int32_t th = totalHeight(c);
        TEST_ASSERT_LESS_OR_EQUAL_INT32(th > 0 ? th - 1 : 0,
                                        c.Radius_Top + c.Radius_Bottom);
    }
    int32_t halfW = c.Width / 2;
    TEST_ASSERT_LESS_OR_EQUAL_INT32(halfW, c.Radius_Top);                 // I3
    TEST_ASSERT_LESS_OR_EQUAL_INT32(halfW, c.Radius_Bottom);
    TEST_ASSERT_LESS_OR_EQUAL_INT32(halfW, c.Inverse_Radius_Top);         // I4
    TEST_ASSERT_LESS_OR_EQUAL_INT32(halfW, c.Inverse_Radius_Bottom);
}

// -----------------------------------------------------------------------
// Cas nominal : un preset valide traverse normalize() sans modification
// -----------------------------------------------------------------------
static void test_valid_preset_unchanged() {
    EyeConfig c = presetNormal();
    normalize(c);
    TEST_ASSERT_EQUAL_INT16(100, c.Width);
    TEST_ASSERT_EQUAL_INT16(100, c.Height);
    TEST_ASSERT_EQUAL_INT16(25,  c.Radius_Top);
    TEST_ASSERT_EQUAL_INT16(25,  c.Radius_Bottom);
}

// -----------------------------------------------------------------------
// Reproduction du bug v1 n°1 : contrainte inter-yeux — GAP-1 clampait
// Width sans toucher aux rayons → Radius(25) > Width/2(15) → débordement.
// -----------------------------------------------------------------------
static void test_interEyeClamp_case() {
    EyeConfig c = presetNormal();
    c.Width = 30;                       // clampé par la contrainte inter-yeux
    normalize(c);
    assertInvariants(c);
    TEST_ASSERT_LESS_OR_EQUAL_INT16(15, c.Radius_Top);     // ≤ Width/2
    TEST_ASSERT_EQUAL_INT16(c.Radius_Top, c.Radius_Bottom); // proportionnalité conservée
}

// -----------------------------------------------------------------------
// Reproduction du bug v1 n°2 : blink — Width→120 et Height→5 pendant que
// les rayons décroissent sur une AUTRE courbe. Milieu de blink typique :
// H=20, rayons encore à 12+12 > H-1 → règle hauteur ; fin de blink H=5.
// -----------------------------------------------------------------------
static void test_blink_midway_case() {
    EyeConfig c;
    c.Width = 110; c.Height = 20;
    c.Radius_Top = 12; c.Radius_Bottom = 12;   // 24 > 19 = H-1
    normalize(c);
    assertInvariants(c);
    TEST_ASSERT_LESS_OR_EQUAL_INT32(19, c.Radius_Top + c.Radius_Bottom);
}

// Fin de blink : hauteur quasi nulle → rayons quasi nuls, jamais négatifs
static void test_blink_closed_case() {
    EyeConfig c;
    c.Width = 120; c.Height = 5;
    c.Radius_Top = 3; c.Radius_Bottom = 3;
    normalize(c);
    assertInvariants(c);
}

// -----------------------------------------------------------------------
// Pentes (Slope) : la hauteur totale intègre les deltas de pente —
// une pente négative RÉDUIT la hauteur disponible pour les rayons.
// -----------------------------------------------------------------------
static void test_slope_reduces_height_budget() {
    EyeConfig c;
    c.Width = 100; c.Height = 60;
    c.Slope_Top = -0.5f;                 // paupière tombante : -15 px en haut
    c.Radius_Top = 30; c.Radius_Bottom = 30;
    normalize(c);
    assertInvariants(c);
}

// -----------------------------------------------------------------------
// Dimensions négatives (transitions agressives) → 0, pas de débordement signé
// -----------------------------------------------------------------------
static void test_negative_dimensions() {
    EyeConfig c;
    c.Width = -40; c.Height = -10;
    c.Radius_Top = 25; c.Radius_Bottom = -7;
    normalize(c);
    assertInvariants(c);
    TEST_ASSERT_FALSE(isDrawable(c));    // taille nulle → rien à dessiner
}

// -----------------------------------------------------------------------
// Coins rentrants : mêmes limites de largeur que les coins normaux
// -----------------------------------------------------------------------
static void test_inverse_radius_clamped() {
    EyeConfig c = presetNormal();
    c.Width = 40;
    c.Inverse_Radius_Top = 35;           // > Width/2 = 20
    normalize(c);
    assertInvariants(c);
    TEST_ASSERT_LESS_OR_EQUAL_INT16(20, c.Inverse_Radius_Top);
}

// -----------------------------------------------------------------------
// Balayage brut : 5 000 configs pseudo-aléatoires (déterministes — LCG à
// graine fixe) couvrant tout l'espace y compris les valeurs absurdes.
// Équivalent natif du balayage blink×gaze×transition de la validation P1 :
// les invariants doivent tenir pour TOUT input.
// -----------------------------------------------------------------------
static void test_fuzz_invariants() {
    uint32_t seed = 0xC0FFEE;
    auto next = [&seed]() {              // LCG minimal, déterministe
        seed = seed * 1664525u + 1013904223u;
        return (int32_t)(seed >> 16);
    };
    for (int i = 0; i < 5000; i++) {
        EyeConfig c;
        c.Width                 = (int16_t)(next() % 400 - 50);
        c.Height                = (int16_t)(next() % 400 - 50);
        c.Slope_Top             = (next() % 200 - 100) / 100.0f;
        c.Slope_Bottom          = (next() % 200 - 100) / 100.0f;
        c.Radius_Top            = (int16_t)(next() % 200 - 20);
        c.Radius_Bottom         = (int16_t)(next() % 200 - 20);
        c.Inverse_Radius_Top    = (int16_t)(next() % 200 - 20);
        c.Inverse_Radius_Bottom = (int16_t)(next() % 200 - 20);
        normalize(c);
        assertInvariants(c);
    }
}

// -----------------------------------------------------------------------
// isDrawable — état sûr par défaut (fix « bandes au boot » §2.1)
// -----------------------------------------------------------------------
static void test_default_config_not_drawable() {
    EyeConfig c;                         // tout à zéro par défaut (v2)
    TEST_ASSERT_FALSE(isDrawable(c));
    TEST_ASSERT_TRUE(isDrawable(presetNormal()));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_preset_unchanged);
    RUN_TEST(test_interEyeClamp_case);
    RUN_TEST(test_blink_midway_case);
    RUN_TEST(test_blink_closed_case);
    RUN_TEST(test_slope_reduces_height_budget);
    RUN_TEST(test_negative_dimensions);
    RUN_TEST(test_inverse_radius_clamped);
    RUN_TEST(test_fuzz_invariants);
    RUN_TEST(test_default_config_not_drawable);
    return UNITY_END();
}
