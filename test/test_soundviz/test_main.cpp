// =============================================================================
// test_soundviz — StackChan-Companion : l'analyseur qui alimente le bandeau son
// =============================================================================
// `Fft.h` prouve que la transformation est juste. Ce fichier prouve ce qui
// l'entoure, et qui est PLUS facile a casser sans que rien ne se voie : l'ordre
// des canaux, le declenchement de la trace, la publication de l'etat « micro au
// repos », et la sensibilite.
//
// Le cas qui justifie le fichier a lui seul est `test_idle_publishes_at_boot` :
// le producteur ne publiait qu'a la transition vivant->repos, donc au demarrage
// — ou rien n'a jamais ete vivant, et ou le micro est eteint par defaut — le
// bandeau restait VIDE au lieu de dire pourquoi. Aucune assertion ne le
// couvrait, et un bandeau vide ressemble a un bandeau qui marche.
//
// Execution : .\scripts\gates\test-native.ps1 test_soundviz
// =============================================================================

#include <unity.h>
#include <cmath>
#include <cstdint>
#include "app/SoundViz.h"

using namespace sce;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

static constexpr int N = SoundViz::N;              // 512
static int16_t lr[N * 2];

// Remplit le bloc entrelace. RAPPEL : 2i = micro DROIT, 2i+1 = GAUCHE.
static void fill(float ampL, int binL, float ampR, int binR) {
    for (int i = 0; i < N; i++) {
        const float t = 2.0f * (float)M_PI * (float)i / (float)N;
        lr[2 * i]     = (int16_t)(ampR * sinf(t * (float)binR));
        lr[2 * i + 1] = (int16_t)(ampL * sinf(t * (float)binL));
    }
}

static int maxBand(const uint8_t* b) {
    int best = 0;
    for (int i = 1; i < SoundFrame::BAND_N; i++) if (b[i] > b[best]) best = i;
    return best;
}
static int peakOf(const uint8_t* b) {
    int m = 0;
    for (int i = 0; i < SoundFrame::BAND_N; i++) if (b[i] > m) m = b[i];
    return m;
}

// ---- au demarrage, le repos est DIT ----------------------------------------
// Le bandeau lit `hasEverPublished()` : sans une premiere publication il ne
// dessine rien du tout, pas meme le message. Or le micro est eteint par defaut
// et le reste 20 s apres le boot dans tous les cas.
static void test_idle_publishes_at_boot(void) {
    TripleBuffer<SoundFrame> bus;
    SoundViz v(bus);
    TEST_ASSERT_FALSE(bus.hasEverPublished());
    v.idle();                                   // premier appel, rien avant
    TEST_ASSERT_TRUE(bus.hasEverPublished());
    TEST_ASSERT_FALSE(bus.read().live);
}

// ...et il n'est pas REDIT trente fois par seconde : le consommateur garde la
// derniere trame lue, republier un neant inchange est du cout pur.
static void test_idle_is_said_once(void) {
    TripleBuffer<SoundFrame> bus;
    SoundViz v(bus);
    v.idle();
    SoundFrame& w = bus.beginWrite();           // marqueur : si idle() publie
    w.live = true;                              // a nouveau, il l'ecrase
    bus.publish();
    v.idle();
    TEST_ASSERT_TRUE(bus.read().live);          // intact = rien n'a ete publie
}

// Et apres du son, un retour au repos est DIT (le haut-parleur prend le bus
// I2S, ou l'option est coupee).
static void test_live_then_idle_is_said(void) {
    TripleBuffer<SoundFrame> bus;
    SoundViz v(bus);
    fill(9000.0f, 24, 9000.0f, 24);
    v.feed(lr);
    TEST_ASSERT_TRUE(bus.read().live);
    v.idle();
    TEST_ASSERT_FALSE(bus.read().live);
}

// ---- les deux micros restent deux ------------------------------------------
// Un canal grave a gauche, un canal aigu a droite : si l'ordre d'entrelacement
// se retourne, tout l'affichage est en miroir et l'image ne le dit pas.
static void test_channels_stay_apart(void) {
    TripleBuffer<SoundFrame> bus;
    SoundViz v(bus);
    fill(9000.0f, 8, 9000.0f, 120);             // gauche grave, droite aigu
    v.feed(lr);
    const SoundFrame& f = bus.read();
    TEST_ASSERT_TRUE(f.live);
    TEST_ASSERT_TRUE(maxBand(f.bandL) < maxBand(f.bandR));
}

// Un canal muet reste muet quand l'autre hurle — la fuite d'un canal dans
// l'autre ressemblerait, sur un bargraphe, a un vrai spectre stereo.
static void test_one_silent_channel(void) {
    TripleBuffer<SoundFrame> bus;
    SoundViz v(bus);
    fill(12000.0f, 20, 0.0f, 0);
    for (int k = 0; k < 20; k++) v.feed(lr);    // laisser l'enveloppe monter
    const SoundFrame& f = bus.read();
    TEST_ASSERT_TRUE(peakOf(f.bandL) > 100);
    TEST_ASSERT_EQUAL_INT(0, peakOf(f.bandR));
}

// ---- le silence ne dessine rien --------------------------------------------
static void test_silence_draws_nothing(void) {
    TripleBuffer<SoundFrame> bus;
    SoundViz v(bus);
    for (int i = 0; i < N * 2; i++) lr[i] = 0;
    v.feed(lr);
    const SoundFrame& f = bus.read();
    TEST_ASSERT_TRUE(f.live);                   // le micro ECOUTE, il n'entend rien
    TEST_ASSERT_EQUAL_INT(0, peakOf(f.bandL));
    for (int c = 0; c < SoundFrame::WAVE_N; c++) TEST_ASSERT_EQUAL_INT(0, f.waveL[c]);
}

// ---- la trace est DECLENCHEE -----------------------------------------------
// Sans declenchement, un ton stable commence a une phase differente a chaque
// bloc et la courbe glisse indefiniment. Avec, deux blocs du meme ton — decales
// dans le temps — rendent la MEME trace. C'est la seule facon de le prouver
// sans regarder l'ecran.
static void test_trace_is_triggered(void) {
    TripleBuffer<SoundFrame> bus;
    SoundViz v(bus);
    const int bin = 16;
    int8_t first[SoundFrame::WAVE_N];
    for (int shift = 0; shift < 2; shift++) {
        for (int i = 0; i < N; i++) {
            // Le second bloc est le meme signal demarre un quart de periode
            // plus tard : exactement ce que fait un ton continu entre deux
            // captures successives.
            const float ph = (shift ? (float)M_PI / 2.0f : 0.0f);
            const float t  = 2.0f * (float)M_PI * (float)bin * (float)i / (float)N;
            lr[2 * i] = lr[2 * i + 1] = (int16_t)(9000.0f * sinf(t + ph));
        }
        v.feed(lr);
        const SoundFrame& f = bus.read();
        if (!shift) for (int c = 0; c < SoundFrame::WAVE_N; c++) first[c] = f.waveL[c];
        else        for (int c = 0; c < SoundFrame::WAVE_N / 2; c++)
                        TEST_ASSERT_INT_WITHIN(12, first[c], f.waveL[c]);
    }
}

// La trace ne sort jamais de la bande : elle est ecretee AVANT le bandeau, qui
// n'a que 18 px de demi-hauteur et aucun tampon arriere pour rattraper.
static void test_trace_is_bounded(void) {
    TripleBuffer<SoundFrame> bus;
    SoundViz v(bus);
    fill(32000.0f, 30, 32000.0f, 30);           // pleine echelle
    v.feed(lr);
    const SoundFrame& f = bus.read();
    for (int c = 0; c < SoundFrame::WAVE_N; c++) {
        TEST_ASSERT_TRUE(f.waveL[c] >= -127 && f.waveL[c] <= 127);
        TEST_ASSERT_TRUE(f.waveR[c] >= -127 && f.waveR[c] <= 127);
    }
}

// ---- la sensibilite ---------------------------------------------------------
// Le meme son, plus de gain : les barres montent. Un gain qui ne change RIEN
// est un reglage qui ment, et sur un bargraphe qui bouge deja personne ne le
// verrait.
static void test_gain_raises_the_bars(void) {
    TripleBuffer<SoundFrame> bus;
    SoundViz lo(bus), hi(bus);
    fill(600.0f, 20, 600.0f, 20);               // une piece calme
    for (int k = 0; k < 20; k++) lo.feed(lr);
    const int quiet = peakOf(bus.read().bandL);
    hi.setGain(8.0f);
    for (int k = 0; k < 20; k++) hi.feed(lr);
    const int loud = peakOf(bus.read().bandL);
    TEST_ASSERT_TRUE(loud > quiet);
}

// Un gain absurde venu de /api/tuning est BORNE, pas obei : zero eteindrait le
// bandeau et un negatif retournerait la trace.
static void test_gain_is_bounded(void) {
    TripleBuffer<SoundFrame> bus;
    SoundViz v(bus);
    fill(9000.0f, 20, 9000.0f, 20);
    v.setGain(0.0f);                            // borne basse a 0,1
    v.feed(lr);
    // Un vrai test, pas une tautologie sur un uint8 : meme au gain plancher,
    // un ton de 9000 reste au-dessus du plancher de -48 dB, donc la barre
    // est NON NULLE - un zero ici voudrait dire que la borne a coupe le son.
    TEST_ASSERT_TRUE(peakOf(bus.read().bandL) > 0);
    v.setGain(-4.0f);                           // jamais une trace inversee
    v.feed(lr);
    const SoundFrame& f = bus.read();
    bool anyPositive = false;
    for (int c = 0; c < SoundFrame::WAVE_N; c++) if (f.waveL[c] > 0) anyPositive = true;
    TEST_ASSERT_TRUE(anyPositive);
}

// ---- la trace ignore le continu, comme le spectre --------------------------
// Un micro pose sur un offset continu ne croise jamais zero : sans retrait de
// la moyenne, le declencheur ne tirait JAMAIS (start=0 a chaque bloc, le ton
// glissait indefiniment - l artefact exact que le declencheur existe a
// empecher) et la trace montait d un cran. Le spectre retirait deja la
// moyenne ; les deux affichages doivent dire le meme fait.
static void test_trace_ignore_le_continu(void) {
    TripleBuffer<SoundFrame> bus;
    SoundViz v(bus);
    const int bin = 16;
    for (int i = 0; i < N; i++) {
        const float t = 2.0f * (float)M_PI * (float)bin * (float)i / (float)N;
        lr[2 * i] = lr[2 * i + 1] = (int16_t)(8000.0f + 6000.0f * sinf(t));
    }
    v.feed(lr);
    const SoundFrame& f = bus.read();
    // La moyenne retiree, la trace oscille des DEUX cotes de l axe.
    bool neg = false, pos = false;
    for (int c = 0; c < SoundFrame::WAVE_N; c++) {
        if (f.waveL[c] < -3) neg = true;
        if (f.waveL[c] >  3) pos = true;
    }
    TEST_ASSERT_TRUE(neg);
    TEST_ASSERT_TRUE(pos);
}

// ---- l'enveloppe et sa memoire ----------------------------------------------
// Les habillages `columns` et `matrix` dessinent l'ENVELOPPE de la trace, et le
// temoin de crete de `matrix` est un MAINTIEN — donc du lissage, donc ici et
// pas dans le renderer (A2.15). Ces cas fixent le contrat que le peintre lit.

static int peakOfEnv(const uint8_t* e) {
    int m = 0;
    for (int i = 0; i < SoundFrame::ENV_N; i++) if (e[i] > m) m = e[i];
    return m;
}

// L'enveloppe suit la trace : du son la leve, le silence la couche.
static void test_envelope_follows_the_trace(void) {
    TripleBuffer<SoundFrame> bus;
    SoundViz v(bus);
    fill(12000.0f, 20, 12000.0f, 20);
    v.feed(lr);
    TEST_ASSERT_TRUE(peakOfEnv(bus.read().env) > 100);
    for (int i = 0; i < N * 2; i++) lr[i] = 0;
    v.feed(lr);
    TEST_ASSERT_EQUAL_INT(0, peakOfEnv(bus.read().env));
}

// Elle prend le PLUS FORT des deux micros, jamais la moyenne : une silhouette
// n'a qu'une hauteur, et moyenner laisserait un canal se cacher dans l'autre.
static void test_envelope_takes_the_louder_mic(void) {
    TripleBuffer<SoundFrame> bus;
    SoundViz v(bus);
    fill(12000.0f, 20, 0.0f, 0);                // gauche seul
    v.feed(lr);
    const int lOnly = peakOfEnv(bus.read().env);
    fill(12000.0f, 20, 12000.0f, 20);           // les deux
    v.feed(lr);
    TEST_ASSERT_INT_WITHIN(12, peakOfEnv(bus.read().env), lOnly);
    TEST_ASSERT_TRUE(lOnly > 100);              // pas moyenne a moitie
}

// Le maintien : la crete reste AU-DESSUS de l'enveloppe apres une transitoire,
// puis redescend. Un temoin qui tombe avec le son ne temoigne de rien, un
// temoin qui ne tombe jamais fossilise le premier bruit de la journee.
static void test_env_peak_holds_then_falls(void) {
    TripleBuffer<SoundFrame> bus;
    SoundViz v(bus);
    fill(12000.0f, 20, 12000.0f, 20);
    v.feed(lr);
    const int held = peakOfEnv(bus.read().envPeak);
    TEST_ASSERT_TRUE(held > 100);
    for (int i = 0; i < N * 2; i++) lr[i] = 0;
    v.feed(lr);                                 // silence : l'enveloppe tombe
    TEST_ASSERT_EQUAL_INT(0, peakOfEnv(bus.read().env));
    const int after = peakOfEnv(bus.read().envPeak);
    TEST_ASSERT_TRUE(after > 0 && after < held);         // il descend, pas d'un coup
    for (int k = 0; k < 200; k++) v.feed(lr);            // ...et il finit a zero
    TEST_ASSERT_EQUAL_INT(0, peakOfEnv(bus.read().envPeak));
}

// La crete n'est JAMAIS sous l'enveloppe : le bloc blanc se poserait dans la
// pile au lieu de s'en detacher, et matrix perdrait son seul repere.
static void test_env_peak_never_below_env(void) {
    TripleBuffer<SoundFrame> bus;
    SoundViz v(bus);
    for (int k = 0; k < 30; k++) {
        fill(2000.0f * (float)(k % 6 + 1), 20, 900.0f * (float)(k % 4 + 1), 44);
        v.feed(lr);
        const SoundFrame& f = bus.read();
        for (int i = 0; i < SoundFrame::ENV_N; i++)
            TEST_ASSERT_TRUE(f.envPeak[i] >= f.env[i]);
    }
}

// Un retour au repos EFFACE la memoire. Sans cela, apres une minute de silence
// — le haut-parleur tient le bus I2S, ou l'option est coupee — la premiere
// chose que dirait le bandeau serait une transitoire d'une autre epoque.
static void test_idle_forgets_the_peak(void) {
    TripleBuffer<SoundFrame> bus;
    SoundViz v(bus);
    fill(12000.0f, 20, 12000.0f, 20);
    v.feed(lr);
    TEST_ASSERT_TRUE(peakOfEnv(bus.read().envPeak) > 100);
    v.idle();
    for (int i = 0; i < N * 2; i++) lr[i] = 0;
    v.feed(lr);                                 // premiere trame du retour
    TEST_ASSERT_EQUAL_INT(0, peakOfEnv(bus.read().envPeak));
}

// ---- un bloc absent ne casse rien -------------------------------------------
static void test_null_block_is_refused(void) {
    TripleBuffer<SoundFrame> bus;
    SoundViz v(bus);
    v.feed(nullptr);
    TEST_ASSERT_FALSE(bus.hasEverPublished());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_publishes_at_boot);
    RUN_TEST(test_idle_is_said_once);
    RUN_TEST(test_live_then_idle_is_said);
    RUN_TEST(test_channels_stay_apart);
    RUN_TEST(test_one_silent_channel);
    RUN_TEST(test_silence_draws_nothing);
    RUN_TEST(test_trace_is_triggered);
    RUN_TEST(test_trace_is_bounded);
    RUN_TEST(test_gain_raises_the_bars);
    RUN_TEST(test_gain_is_bounded);
    RUN_TEST(test_trace_ignore_le_continu);
    RUN_TEST(test_envelope_follows_the_trace);
    RUN_TEST(test_envelope_takes_the_louder_mic);
    RUN_TEST(test_env_peak_holds_then_falls);
    RUN_TEST(test_env_peak_never_below_env);
    RUN_TEST(test_idle_forgets_the_peak);
    RUN_TEST(test_null_block_is_refused);
    return UNITY_END();
}
