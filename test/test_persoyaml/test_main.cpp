// =============================================================================
// test_persoyaml — le fichier de personnalité : lecture, refus, aller-retour
// =============================================================================
// Ce fichier est écrit PAR la console et relu par le robot, donc la propriété
// qui compte n'est pas « une clé est lue » mais **l'aller-retour** : ce qu'on
// sauve doit se recharger identique. Sans cela, enregistrer un caractère depuis
// la console le modifie un peu à chaque passage, et personne ne le voit avant
// que la dérive soit visible à l'œil.
//
// Les deux autres assertions portantes :
//   - un OVERLAY, pas une déclaration : une clé absente garde le défaut
//     compilé, donc un fichier à moitié écrit dégrade vers « presque le
//     défaut » et non vers un caractère troué ;
//   - une clé inconnue est REFUSÉE ET NOMMÉE. Une faute de frappe qui ne fait
//     rien en silence est le mode d'échec que ce dépôt paie en boucle.
// =============================================================================

#include <unity.h>
#include <cstring>
#include <cstdio>
#include "behavior/PersonalityYaml.h"

using namespace sce;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// Fait passer un document par le VRAI analyseur ligne à ligne, comme le
// lecteur SD le fera : un test qui appellerait `feed()` avec des Line fabriquées
// à la main ne testerait pas le décodage des guillemets ni des commentaires.
static void load(const char* doc, Personality& p, PersonalityYaml& y) {
    y.begin(p);
    char buf[128];
    const char* s = doc;
    while (*s) {
        const char* nl = strchr(s, '\n');
        size_t len = nl ? (size_t)(nl - s) : strlen(s);
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, s, len); buf[len] = '\0';
        y.feed(sce::yaml::decodeLine(buf));
        if (!nl) break;
        s = nl + 1;
    }
}

// Un caractère de départ crédible, distinct des défauts, pour que « la clé a été
// lue » ne puisse pas être confondu avec « la valeur était déjà là ».
static void seed(Personality& p) {
    personalities::fill(p, "base", "/stackchan-companion/rules.txt",
                        0x112233u, ConsoleTheme::Default, 5000, 9000);
}

// ---- 1. L'ALLER-RETOUR ------------------------------------------------------
static void test_ce_qu_on_sauve_se_recharge_identique(void) {
    Personality a; seed(a);
    strncpy(a.name, "haro", PERSO_NAME_MAX - 1);
    strncpy(a.rulesFile, "/stackchan-companion/rules.haro.txt", PERSO_PATH_MAX - 1);
    a.eyeRgb = 0x22FF66u; a.theme = ConsoleTheme::Gundam;
    a.roulette = false; a.minMs = 8000; a.maxMs = 20000;
    a.transitionScale = 1.6f; a.pitchBiasScale = 0.4f;   // distinct from seed()'s 1.0/1.0
    a.setWeight(Normal, 1.0f);
    a.setWeight(Happy,  0.6f);
    a.setWeight(Glee,   0.35f);

    static char out[1024]; out[0] = '\0';
    writePersonality(a, [](const char* s) { strncat(out, s, sizeof(out) - strlen(out) - 1); });

    Personality b; seed(b);                       // volontairement DIFFERENT
    PersonalityYaml y; load(out, b, y);

    TEST_ASSERT_EQUAL_INT(0, y.problems());       // ce qu'on ecrit, on sait le lire
    TEST_ASSERT_EQUAL_STRING(a.name, b.name);
    TEST_ASSERT_EQUAL_STRING(a.rulesFile, b.rulesFile);
    TEST_ASSERT_EQUAL_UINT32(a.eyeRgb, b.eyeRgb);
    TEST_ASSERT_TRUE(a.theme == b.theme);
    TEST_ASSERT_EQUAL_INT(a.roulette, b.roulette);
    TEST_ASSERT_EQUAL_UINT32(a.minMs, b.minMs);
    TEST_ASSERT_EQUAL_UINT32(a.maxMs, b.maxMs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, a.transitionScale, b.transitionScale);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, a.pitchBiasScale, b.pitchBiasScale);
    TEST_ASSERT_TRUE(b.hasWeights);
    for (int i = 0; i < EMOTIONS_COUNT; i++)
        TEST_ASSERT_FLOAT_WITHIN(0.01f, a.weights[i], b.weights[i]);
}

// ---- 2. UN OVERLAY, PAS UNE DECLARATION ------------------------------------
static void test_une_cle_absente_garde_le_defaut(void) {
    Personality p; seed(p);
    PersonalityYaml y;
    load("color: 0xFF0000\n", p, y);
    TEST_ASSERT_EQUAL_INT(0, y.problems());
    TEST_ASSERT_EQUAL_UINT32(0xFF0000u, p.eyeRgb);      // ce qui est dit change
    TEST_ASSERT_EQUAL_STRING("base", p.name);           // le reste ne bouge pas
    TEST_ASSERT_EQUAL_UINT32(5000, p.minMs);
    TEST_ASSERT_EQUAL_UINT32(9000, p.maxMs);
    TEST_ASSERT_FALSE(p.hasWeights);
    // memset would have left these at 0.0 — a SCALE at 0 means "erase every
    // transition", so an absent key must land on 1.0 (unscaled), not on
    // whatever fill() happened to zero.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, p.transitionScale);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, p.pitchBiasScale);
}

// Declarer `weights:` REMPLACE la table au lieu de s'y fondre — sinon on
// pourrait ajouter une emotion mais jamais en retirer une, et « ce caractere ne
// fait pas Sad » serait inexprimable.
static void test_declarer_des_poids_remplace_la_table(void) {
    Personality p; seed(p);
    p.setWeight(Sad, 0.9f);                 // present AVANT
    PersonalityYaml y;
    load("weights:\n  Happy: 0.5\n", p, y);
    TEST_ASSERT_EQUAL_INT(0, y.problems());
    TEST_ASSERT_TRUE(p.hasWeights);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, p.weights[Happy]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, p.weights[Sad]);   // parti, pas fusionne
}

// ---- 3. CE QU'IL REFUSE, ET CE QU'IL NOMME ---------------------------------
static void test_une_cle_inconnue_est_refusee_et_nommee(void) {
    Personality p; seed(p);
    PersonalityYaml y;
    load("couleur: 0xFF0000\n", p, y);      // faute de frappe credible (FR/EN)
    TEST_ASSERT_EQUAL_INT(1, y.problems());
    TEST_ASSERT_EQUAL_STRING("couleur", y.firstProblem());
    TEST_ASSERT_EQUAL_UINT32(0x112233u, p.eyeRgb);          // rien n'a bouge
}

static void test_une_emotion_inconnue_est_refusee(void) {
    Personality p; seed(p);
    PersonalityYaml y;
    load("weights:\n  Joyeux: 1.0\n  Happy: 0.5\n", p, y);
    TEST_ASSERT_EQUAL_INT(1, y.problems());
    TEST_ASSERT_EQUAL_STRING("Joyeux", y.firstProblem());
    // La ligne SUIVANTE est quand meme lue : une faute n'invalide pas le fichier.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, p.weights[Happy]);
}

static void test_un_theme_inconnu_est_refuse(void) {
    Personality p; seed(p);
    PersonalityYaml y;
    load("theme: zeta\n", p, y);
    TEST_ASSERT_EQUAL_INT(1, y.problems());
    TEST_ASSERT_TRUE(p.theme == ConsoleTheme::Default);     // pas de theme invente
}

// ---- 4. LE FORMAT PARTAGE FAIT SON TRAVAIL ---------------------------------
// Ces deux-la ne testent pas PersonalityYaml mais le CONTRAT avec l'analyseur
// commun : commentaire en fin de ligne, valeur entre guillemets. Les deux
// pannes historiques du depot (SSID quote, source ADS-B quotee) etaient
// exactement cela, et un nouveau lecteur doit en heriter, pas les repayer.
static void test_commentaire_et_guillemets(void) {
    Personality p; seed(p);
    PersonalityYaml y;
    load("name: \"haro 2\"\nroulette:\n  min_ms: 7000   # plus calme\n", p, y);
    TEST_ASSERT_EQUAL_INT(0, y.problems());
    TEST_ASSERT_EQUAL_STRING("haro 2", p.name);
    TEST_ASSERT_EQUAL_UINT32(7000, p.minMs);
}

// Un nom plus long que le champ est TRONQUE, jamais ecrit a cote.
static void test_un_nom_trop_long_est_tronque(void) {
    Personality p; seed(p);
    PersonalityYaml y;
    load("name: unnomvraimenttreslongquidepasse\n", p, y);
    TEST_ASSERT_EQUAL_INT(0, y.problems());
    TEST_ASSERT_EQUAL_INT(PERSO_NAME_MAX - 1, (int)strlen(p.name));
}

// ---- 5. LA LIGNE DE PARTAGE, ENCORE ----------------------------------------
// Aucune grandeur mecanique n'est lisible depuis un fichier : les clés servo,
// budget de frame et cadences de bus ne sont pas seulement absentes de la
// table, elles sont REFUSEES si quelqu'un les ecrit.
static void test_aucune_grandeur_mecanique_n_est_acceptee(void) {
    Personality p; seed(p);
    PersonalityYaml y;
    load("pitch_min: 5\npitch_max: 120\nyaw_max: 300\nframe_ms: 5\n", p, y);
    TEST_ASSERT_EQUAL_INT(4, y.problems());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ce_qu_on_sauve_se_recharge_identique);
    RUN_TEST(test_une_cle_absente_garde_le_defaut);
    RUN_TEST(test_declarer_des_poids_remplace_la_table);
    RUN_TEST(test_une_cle_inconnue_est_refusee_et_nommee);
    RUN_TEST(test_une_emotion_inconnue_est_refusee);
    RUN_TEST(test_un_theme_inconnu_est_refuse);
    RUN_TEST(test_commentaire_et_guillemets);
    RUN_TEST(test_un_nom_trop_long_est_tronque);
    RUN_TEST(test_aucune_grandeur_mecanique_n_est_acceptee);
    return UNITY_END();
}
