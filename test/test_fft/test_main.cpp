// =============================================================================
// test_fft — StackChan-Companion : la FFT et le découpage en bandes du bandeau son
// =============================================================================
// Un spectre faux est faux d'une façon que personne ne voit : sur un bargraphe
// qui bouge, toutes les barres bougent quand même. Ces cas fixent donc le
// résultat AVANT que le code tourne — une sinusoïde tombe dans une seule case,
// le continu tombe en case 0, le silence reste silencieux.
// Exécution : .\scripts\gates\test-native.ps1 test_fft
// =============================================================================

#include <unity.h>
#include <cmath>
#include <cstdint>
#include "engine/Fft.h"

using namespace sce::dsp;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

static constexpr int N = 512;
static float x[N], im[N], mag[N / 2];

// L'indice de la case la plus forte.
static int argmax(const float* m, int n) {
    int best = 0;
    for (int k = 1; k < n; k++) if (m[k] > m[best]) best = k;
    return best;
}

// ---- une sinusoïde pile sur une case y tombe, et nulle part ailleurs -------
static void test_sine_lands_in_its_bin(void) {
    const int bin = 40;                       // 40 * 16000 / 512 = 1250 Hz
    for (int i = 0; i < N; i++)
        x[i] = 10000.0f * sinf(2.0f * (float)M_PI * (float)bin * (float)i / (float)N);
    TEST_ASSERT_TRUE(magnitudes(x, im, mag, N));
    TEST_ASSERT_EQUAL_INT(bin, argmax(mag, N / 2));
    // La fenêtre de Hann étale sur les voisines immédiates ; au-delà, plus
    // rien. C'est CE contrôle qui distingue une vraie FFT d'un tableau de
    // bruit : sans fenêtre, la case 200 vaudrait encore le dixième du pic.
    const float peak = mag[bin];
    for (int k = 0; k < N / 2; k++)
        if (k < bin - 3 || k > bin + 3)
            TEST_ASSERT_TRUE(mag[k] < peak * 0.02f);
}

// ---- le continu disparaît PARTOUT, pas seulement en case 0 -----------------
// Un micro est posé sur un offset continu qui écrase le signal. Annuler la
// case 0 ne suffit PAS : la fenêtre a un spectre à elle, donc une constante
// multipliée par une fenêtre de Hann atterrit aussi en cases 1 et 2. Ce cas
// vérifie l'ORDRE — moyenne retirée AVANT fenêtrage — et il a effectivement
// pris le code en défaut la première fois.
static void test_dc_is_removed(void) {
    for (int i = 0; i < N; i++) x[i] = 12000.0f;   // que du continu
    TEST_ASSERT_TRUE(magnitudes(x, im, mag, N));
    for (int k = 0; k < N / 2; k++) TEST_ASSERT_TRUE(mag[k] < 1.0f);
}

// Et un signal PORTE par un offset garde son ton, sans les deux barres
// parasites du bas.
static void test_dc_offset_does_not_pollute(void) {
    const int bin = 55;
    for (int i = 0; i < N; i++)
        x[i] = 9000.0f + 3000.0f * sinf(2.0f * (float)M_PI * (float)bin * (float)i / (float)N);
    TEST_ASSERT_TRUE(magnitudes(x, im, mag, N));
    TEST_ASSERT_EQUAL_INT(bin, argmax(mag, N / 2));
    TEST_ASSERT_TRUE(mag[1] < mag[bin] * 0.01f);
    TEST_ASSERT_TRUE(mag[2] < mag[bin] * 0.01f);
}

// ---- le silence reste silencieux ------------------------------------------
static void test_silence_stays_silent(void) {
    for (int i = 0; i < N; i++) x[i] = 0.0f;
    TEST_ASSERT_TRUE(magnitudes(x, im, mag, N));
    for (int k = 0; k < N / 2; k++) TEST_ASSERT_EQUAL_FLOAT(0.0f, mag[k]);
}

// ---- l'amplitude rendue est celle du signal -------------------------------
// Normalisation 2/N et fenêtre de Hann (gain cohérent 0,5) : une sinusoïde
// d'amplitude A rend A/2 dans sa case. On vérifie le RAPPORT, pas une
// constante magique.
static void test_amplitude_scales(void) {
    const int bin = 32;
    for (int i = 0; i < N; i++)
        x[i] = 8000.0f * sinf(2.0f * (float)M_PI * (float)bin * (float)i / (float)N);
    TEST_ASSERT_TRUE(magnitudes(x, im, mag, N));
    const float a = mag[bin];
    for (int i = 0; i < N; i++)
        x[i] = 4000.0f * sinf(2.0f * (float)M_PI * (float)bin * (float)i / (float)N);
    TEST_ASSERT_TRUE(magnitudes(x, im, mag, N));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 2.0f, a / mag[bin]);
}

// ---- deux tons simultanés donnent deux pics -------------------------------
static void test_two_tones(void) {
    const int b1 = 20, b2 = 90;
    for (int i = 0; i < N; i++) {
        const float t = 2.0f * (float)M_PI * (float)i / (float)N;
        x[i] = 6000.0f * sinf(t * (float)b1) + 6000.0f * sinf(t * (float)b2);
    }
    TEST_ASSERT_TRUE(magnitudes(x, im, mag, N));
    TEST_ASSERT_TRUE(mag[b1] > mag[b1 + 6] * 10.0f);
    TEST_ASSERT_TRUE(mag[b2] > mag[b2 + 6] * 10.0f);
}

// ---- une taille qui n'est pas une puissance de deux est REFUSÉE -----------
// Refusée et pas approximée : une passe de papillons à moitié faite rend un
// spectre plausible et dénué de sens.
static void test_bad_size_refused(void) {
    TEST_ASSERT_FALSE(fft(x, im, 100));
    TEST_ASSERT_FALSE(fft(x, im, 0));
    TEST_ASSERT_FALSE(magnitudes(x, im, mag, 300));
}

// ---- les bandes sont logarithmiques et strictement croissantes ------------
static void test_band_edges(void) {
    const int BANDS = 24;
    int edge[BANDS + 1];
    bandEdges(edge, BANDS, N, 16000.0f, 60.0f, 7000.0f);
    for (int b = 1; b <= BANDS; b++)
        TEST_ASSERT_TRUE(edge[b] > edge[b - 1]);   // aucune bande vide
    TEST_ASSERT_TRUE(edge[0] >= 1);                // jamais le continu
    TEST_ASSERT_TRUE(edge[BANDS] <= N / 2);
    // Logarithmique : la dernière bande couvre bien plus de cases que la
    // première. En linéaire, les voyelles et les basses tiendraient dans deux
    // barres et les trois quarts de l'écran montreraient le 4-8 kHz vide.
    const int lowSpan  = edge[1] - edge[0];
    const int highSpan = edge[BANDS] - edge[BANDS - 1];
    TEST_ASSERT_TRUE(highSpan > lowSpan * 4);
}

// ---- les bandes restent croissantes MEME quand le plafond mord ------------
// Demander un haut de bande AU-DESSUS de Nyquist est une erreur d'appelant qui
// n'a l'air de rien : les dernières arêtes se rabattent toutes sur la dernière
// case, le forçage vers le haut les repousse d'un cran, et le plafond les
// ramène — deux voisines retombent sur le MÊME indice. Une bande d'étendue
// nulle rend 0 pour toujours et ressemble EXACTEMENT à une pièce silencieuse :
// rien à l'écran ne la dénonce. C'est le cas qui prenait le code en défaut.
static void test_band_edges_survive_the_ceiling(void) {
    // 20 kHz demandé sur 16 kHz d'échantillonnage : Nyquist est à 8 kHz, donc
    // PLUSIEURS bandes hautes se rabattent ensemble sur la dernière case — une
    // seule ne suffit pas à provoquer la collision, il en faut deux.
    for (int bands = 8; bands <= 24; bands += 4) {
        int edge[25];
        bandEdges(edge, bands, N, 16000.0f, 60.0f, 20000.0f);
        for (int b = 1; b <= bands; b++)
            TEST_ASSERT_TRUE(edge[b] > edge[b - 1]);   // aucune bande vide
        TEST_ASSERT_TRUE(edge[0] >= 1);                // jamais le continu
        TEST_ASSERT_TRUE(edge[bands] <= N / 2);        // jamais au-delà
    }
    // Et le petit spectre, où le forçage des bandes basses court longtemps.
    int e2[17];
    bandEdges(e2, 16, 64, 16000.0f, 60.0f, 7900.0f);
    for (int b = 1; b <= 16; b++) TEST_ASSERT_TRUE(e2[b] > e2[b - 1]);
    TEST_ASSERT_TRUE(e2[16] <= 32);
}

// ---- une bande vaut le PIC de ses cases, pas leur moyenne -----------------
static void test_band_value_is_peak(void) {
    const int BANDS = 8;
    int edge[BANDS + 1];
    bandEdges(edge, BANDS, N, 16000.0f, 60.0f, 7000.0f);
    for (int k = 0; k < N / 2; k++) mag[k] = 0.0f;
    const int k0 = edge[BANDS - 1];
    mag[k0] = 100.0f;                    // un ton seul dans une bande large
    const float v = bandValue(mag, edge, BANDS - 1);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, v);  // la moyenne l'aurait noyé
}

// ---- l'échelle en décibels ------------------------------------------------
static void test_db_norm(void) {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dbNorm(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, dbNorm(32768.0f));       // pleine échelle
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dbNorm(32768.0f / 256.0f));  // -48 dB = plancher
    // La moitié de la pleine échelle vaut -6 dB, soit 1 - 6/48 = 0,875 : une
    // échelle linéaire aurait rendu 0,5 et collé la parole au sol.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.875f, dbNorm(16384.0f));
}

// ---- les DEUX micros sortent d'UNE seule transformation --------------------
// L dans la partie réelle, R dans l'imaginaire : le spectre d'un signal réel
// est à symétrie conjuguée, donc les deux se séparent exactement après coup.
// Ce cas le prouve — sinon on aurait un mélange des deux canaux qui, sur un
// bargraphe, ressemblerait tout à fait à un vrai spectre stéréo.
static float xr[N];
static float m2L[N / 2], m2R[N / 2];
static void test_two_channels_one_transform(void) {
    const int bl = 24, br = 88;
    for (int i = 0; i < N; i++) {
        const float t = 2.0f * (float)M_PI * (float)i / (float)N;
        x[i]  = 9000.0f * sinf(t * (float)bl);      // gauche : un ton grave
        xr[i] = 5000.0f * sinf(t * (float)br);      // droite : un ton aigu
    }
    TEST_ASSERT_TRUE(magnitudes2(x, xr, m2L, m2R, N));
    TEST_ASSERT_EQUAL_INT(bl, argmax(m2L, N / 2));
    TEST_ASSERT_EQUAL_INT(br, argmax(m2R, N / 2));
    // AUCUNE fuite d'un canal dans l'autre : c'est tout l'enjeu.
    TEST_ASSERT_TRUE(m2L[br] < m2L[bl] * 0.02f);
    TEST_ASSERT_TRUE(m2R[bl] < m2R[br] * 0.02f);
    // Et l'amplitude de chacun est la sienne, pas la moyenne des deux.
    TEST_ASSERT_FLOAT_WITHIN(0.08f, 9000.0f / 5000.0f, m2L[bl] / m2R[br]);
}

// Un canal muet reste muet quand l'autre hurle.
static void test_two_channels_one_silent(void) {
    for (int i = 0; i < N; i++) {
        x[i]  = 12000.0f * sinf(2.0f * (float)M_PI * 50.0f * (float)i / (float)N);
        xr[i] = 0.0f;
    }
    TEST_ASSERT_TRUE(magnitudes2(x, xr, m2L, m2R, N));
    for (int k = 1; k < N / 2; k++) TEST_ASSERT_TRUE(m2R[k] < 1.0f);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_sine_lands_in_its_bin);
    RUN_TEST(test_dc_is_removed);
    RUN_TEST(test_dc_offset_does_not_pollute);
    RUN_TEST(test_silence_stays_silent);
    RUN_TEST(test_amplitude_scales);
    RUN_TEST(test_two_tones);
    RUN_TEST(test_bad_size_refused);
    RUN_TEST(test_band_edges);
    RUN_TEST(test_band_edges_survive_the_ceiling);
    RUN_TEST(test_band_value_is_peak);
    RUN_TEST(test_two_channels_one_transform);
    RUN_TEST(test_two_channels_one_silent);
    RUN_TEST(test_db_norm);
    return UNITY_END();
}
