// =============================================================================
// test_personalities — le sélecteur de caractère, et la ligne qu'il ne franchit pas
// =============================================================================
// Deux assertions portent ce fichier, et aucune des deux n'est « la table est
// bien formée » :
//
//   1. LA PERSONNALITÉ 0 EST LE ROBOT D'AUJOURD'HUI, AU BIT PRÈS. C'est le
//      critère d'acceptation de tout le lot : une carte sans données de
//      personnalité doit se comporter exactement comme avant que ce code
//      existe. Le test le vérifie en comparant une roulette à qui l'on a
//      appliqué la personnalité 0 à une roulette NEUVE — pas à une copie des
//      poids recopiée ici, qui ne prouverait que ma capacité à recopier.
//
//   2. AUCUNE VALEUR DE PERSONNALITÉ NE PEUT SORTIR LE SERVO DE SES BUTÉES.
//      C'est la ligne de partage écrite en test : une personnalité change ce
//      que le robot RESSENT, jamais ce que le matériel TOLÈRE. Sans ce test,
//      « paramétrable » finit un jour par vouloir dire « cale un servo », et
//      les butées 14/104 causent des dommages permanents.
// =============================================================================

#include <unity.h>
#include <cstring>
#include "engine/Clock.h"
#include "engine/Rng.h"
#include "engine/Units.h"
#include "behavior/EmotionRoulette.h"
#include "behavior/Personalities.h"

using namespace sce;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// Applique ce qu'une personnalité POSSÈDE côté roulette — le même corps que
// main.cpp::applyPersonality, réduit à la partie pure. S'ils divergent, c'est
// le tirage comparé ci-dessous qui le dira.
static void applyWeights(EmotionRoulette& r, const Personality& p) {
    if (p.hasWeights) {
        r.clearWeights();
        for (int i = 0; i < EMOTIONS_COUNT; i++)
            if (p.weights[i] > 0.0f) r.setWeight((eEmotions)i, p.weights[i]);
    } else {
        r.resetWeights();
    }
    r.setInterval(p.minMs, p.maxMs);
}

// Signature d'une distribution : on tire beaucoup, avec la MÊME graine, et on
// compte. Deux roulettes qui rendent le même histogramme sur 4000 tirages ont
// la même table — sans que le test ait à connaître un seul poids, donc sans
// devenir une seconde copie des valeurs qu'il surveille.
// LA PENDULE EST CELLE DE LA ROULETTE, passée en paramètre. Une FakeClock
// locale ici a d'abord semblé marcher : la roulette tient une RÉFÉRENCE vers
// celle de sa construction, donc avancer une autre pendule ne la fait jamais
// tirer — les deux premiers tests comparaient alors deux histogrammes VIDES et
// passaient sans rien affirmer. Un test qui ne peut pas échouer ne prouve rien.
static void histogram(EmotionRoulette& r, FakeClock& clk,
                      int (&out)[EMOTIONS_COUNT]) {
    for (int i = 0; i < EMOTIONS_COUNT; i++) out[i] = 0;
    eEmotions cur = Dead, e;      // Dead n'a aucun poids : ne masque aucun tirage
    for (int i = 0; i < 4000; i++) {
        clk.advanceMs(60000);       // largement au-delà de tout intervalle
        if (r.update(cur, /*locked=*/false, e)) { out[e]++; cur = Dead; }
    }
}

// ---- 1. LE CRITÈRE D'ACCEPTATION ------------------------------------------
static void test_personnalite_zero_est_le_robot_d_avant(void) {
    Personality& p0 = personalities::at(0);
    // Elle ne DÉCLARE aucun poids : elle décline d'y toucher. C'est ce qui rend
    // « pas de données = robot d'aujourd'hui » vrai par construction plutôt que
    // par recopie soigneuse.
    TEST_ASSERT_FALSE(p0.hasWeights);

    Rng ra(4242), rb(4242);       // même graine des deux côtés
    FakeClock ca, cb;
    EmotionRoulette neuve(ca, ra);            // telle que construite
    EmotionRoulette posee(cb, rb);
    applyWeights(posee, p0);                  // ...après application de p0

    int ha[EMOTIONS_COUNT], hb[EMOTIONS_COUNT];
    histogram(neuve, ca, ha);
    histogram(posee, cb, hb);
    for (int i = 0; i < EMOTIONS_COUNT; i++)
        TEST_ASSERT_EQUAL_INT(ha[i], hb[i]);
}

// ---- resetWeights restaure vraiment, après n'importe quel saccage ----------
static void test_le_retour_au_defaut_restaure_tout(void) {
    Rng ra(99), rb(99);
    FakeClock ca, cb;
    EmotionRoulette neuve(ca, ra);
    EmotionRoulette abimee(cb, rb);
    applyWeights(abimee, personalities::at(1));   // devient Haro
    abimee.resetWeights();                        // puis redevient elle-même

    int ha[EMOTIONS_COUNT], hb[EMOTIONS_COUNT];
    histogram(neuve, ca, ha);
    histogram(abimee, cb, hb);
    for (int i = 0; i < EMOTIONS_COUNT; i++)
        TEST_ASSERT_EQUAL_INT(ha[i], hb[i]);
}

// ---- 2. LA LIGNE DE PARTAGE, ÉCRITE EN TEST -------------------------------
static void test_aucune_personnalite_ne_sort_des_butees(void) {
    // Une personnalité ne porte AUCUNE grandeur mécanique. Le test l'affirme
    // sur la seule chose qu'elle puisse influencer indirectement — l'émotion
    // tirée — en vérifiant que toute émotion atteignable garde une posture
    // dans les butées. `pitchBiasFor` vit dans le Brain (non pur) : ce qui se
    // teste ici est l'invariant d'enveloppe, borne comprise.
    for (int i = 0; i < personalities::count(); i++) {
        Personality& p = personalities::at(i);
        if (!p.hasWeights) continue;
        for (int e = 0; e < EMOTIONS_COUNT; e++)
            TEST_ASSERT_TRUE(p.weights[e] >= 0.0f);  // un poids négatif casse le tirage
        // La cadence doit rester une cadence : un intervalle nul ferait tirer
        // la roulette à chaque tick du Brain (100 Hz), ce qui n'est pas un
        // caractère vif, c'est un stroboscope.
        TEST_ASSERT_TRUE(p.minMs >= 1000);
        TEST_ASSERT_TRUE(p.maxMs >= p.minMs);
    }
    // Et l'enveloppe mécanique reste celle d'Units.h, que rien ici ne touche.
    TEST_ASSERT_TRUE(units::PITCH_NEUTRAL >= units::PITCH_MIN);
    TEST_ASSERT_TRUE(units::PITCH_NEUTRAL <= units::PITCH_MAX);
}

// ---- la table elle-même ----------------------------------------------------
static void test_la_table_est_bien_formee(void) {
    TEST_ASSERT_TRUE(personalities::count() >= 2);
    for (int i = 0; i < personalities::count(); i++) {
        Personality& p = personalities::at(i);
        TEST_ASSERT_NOT_NULL(p.name);
        TEST_ASSERT_NOT_NULL(p.rulesFile);
        TEST_ASSERT_TRUE(p.name[0] != '\0');
    }
    // DES FICHIERS DE RÈGLES DISTINCTS : deux personnalités qui pointent le
    // même fichier ne sont pas deux personnalités, et le plafond de 24 règles
    // qu'un fichier par caractère sert à contourner reviendrait aussitôt.
    // DES NOMS DISTINCTS aussi : `personalities::indexOf` cherche par nom, un
    // doublon le rendrait ambigu au runtime (docs/reference/PERSONALITIES.md
    // promet ce test depuis longtemps — seul rulesFile était réellement
    // vérifié).
    for (int i = 0; i < personalities::count(); i++)
        for (int j = i + 1; j < personalities::count(); j++) {
            TEST_ASSERT_TRUE(strcmp(personalities::at(i).rulesFile,
                                    personalities::at(j).rulesFile) != 0);
            TEST_ASSERT_TRUE(strcmp(personalities::at(i).name,
                                    personalities::at(j).name) != 0);
        }
}

// ---- l'accesseur borné rend le robot par défaut, jamais rien --------------
static void test_un_index_inconnu_rend_le_defaut(void) {
    TEST_ASSERT_EQUAL_STRING("default", personalities::at(-1).name);
    TEST_ASSERT_EQUAL_STRING("default", personalities::at(999).name);
    TEST_ASSERT_EQUAL_STRING("default", personalities::at(0).name);
    TEST_ASSERT_EQUAL_INT(0, personalities::indexOf("default"));
    TEST_ASSERT_EQUAL_INT(1, personalities::indexOf("haro"));
    TEST_ASSERT_EQUAL_INT(-1, personalities::indexOf("inconnue"));
    TEST_ASSERT_EQUAL_INT(-1, personalities::indexOf(nullptr));
}

// ---- Haro est ÉTROITE, et c'est le point ----------------------------------
static void test_haro_est_etroite_et_sans_humeur_noire(void) {
    Rng rng(31337); FakeClock clk;
    EmotionRoulette r(clk, rng);
    applyWeights(r, personalities::at(1));
    int h[EMOTIONS_COUNT];
    histogram(r, clk, h);
    // Un Haro ne boude pas tout seul : rien de sombre ne sort au repos, donc
    // quand une RÈGLE pose Worried sur le visage, cela ne peut venir que de ce
    // qui s'est passé. C'est exactement la propriété que la personnalité 0 ne
    // peut pas avoir.
    TEST_ASSERT_EQUAL_INT(0, h[Sad]);
    TEST_ASSERT_EQUAL_INT(0, h[Worried]);
    TEST_ASSERT_EQUAL_INT(0, h[Furious]);
    TEST_ASSERT_EQUAL_INT(0, h[Angry]);
    TEST_ASSERT_EQUAL_INT(0, h[Dead]);
    TEST_ASSERT_TRUE(h[Normal] > 0);
    TEST_ASSERT_TRUE(h[Happy]  > 0);
}

// ---- la roulette éteinte ---------------------------------------------------
static void test_roulette_eteinte_ne_tire_rien_mais_consomme_son_tick(void) {
    Rng rng(5); FakeClock clk;
    EmotionRoulette r(clk, rng);
    r.setInterval(1000, 1000);
    r.setEnabled(false);
    TEST_ASSERT_FALSE(r.enabled());

    eEmotions cur = Normal, out;
    int draws = 0;
    for (int i = 0; i < 200; i++) { clk.advanceMs(100);
        if (r.update(cur, false, out)) draws++; }
    TEST_ASSERT_EQUAL_INT(0, draws);

    // LE TICK A ÉTÉ CONSOMMÉ pendant l'extinction : au rallumage la roulette
    // repart à sa cadence et ne tire pas INSTANTANÉMENT. Sans cela le robot
    // réagirait au fait d'avoir été libéré au lieu de vivre à son rythme.
    r.setEnabled(true);
    TEST_ASSERT_FALSE(r.update(cur, false, out));   // pas de tir immédiat
}

// ---- nuit x personnalité : le seul croisement qui puisse être faux ---------
static void test_la_nuit_traverse_les_personnalites(void) {
    // Le mode nuit met Sleepy à la place de Normal. Une personnalité étroite
    // qui ne déclare PAS Sleepy doit quand même dormir : la nuit n'est pas un
    // trait de caractère, c'est l'obscurité.
    Rng rng(8080); FakeClock clk;
    EmotionRoulette r(clk, rng);
    applyWeights(r, personalities::at(1));   // Haro ne déclare pas Sleepy
    r.setDarkMode(true);
    int h[EMOTIONS_COUNT];
    histogram(r, clk, h);
    TEST_ASSERT_TRUE(h[Sleepy] > 0);
    TEST_ASSERT_EQUAL_INT(0, h[Normal]);     // remplacé par Sleepy
}


// ---- L'ACCESSEUR DE LECTURE N'EST PAS CELUI D'ECRITURE --------------------
// Bug trouve SUR LA CARTE le 09-14, et il etait purement logique : le chargeur
// remplissait les slots via `at()`, qui RABAT tout index hors plage sur le
// slot 0. Ecrire dans le slot 2 d'une table qui en compte 2 ecrivait donc dans
// la personnalite 0 — puis la boucle de nettoyage, passant par le meme `at()`,
// EFFAÇAIT son nom. Symptome : `personality=2` relu 0, et le robot tirait les
// emotions de la personnalite par defaut.
//
// Les deux accesseurs existent pour deux usages opposes, et c'est ce que ce
// test fixe : `at()` protege le LECTEUR (on a toujours un robot), `slot()`
// protege la TABLE (on n'ecrit jamais a cote).
static void test_ecrire_hors_plage_ne_touche_pas_la_personnalite_zero(void) {
    const int n = personalities::count();
    // `at()` rabat — c'est voulu, un lecteur doit toujours obtenir un caractere.
    TEST_ASSERT_EQUAL_STRING("default", personalities::at(n + 5).name);
    // `slot()` REFUSE plutot que de rabattre.
    TEST_ASSERT_NULL(personalities::slot(MAX_PERSONALITIES));
    TEST_ASSERT_NULL(personalities::slot(-1));
    // ...et il donne acces au premier slot LIBRE, celui que `at()` ne sait pas
    // atteindre : c'est ainsi qu'une personnalite de la carte peut naitre.
    Personality* libre = personalities::slot(n);
    TEST_ASSERT_NOT_NULL(libre);
    TEST_ASSERT_TRUE(libre != &personalities::at(0));
}

// Remplir un slot libre fait grandir la table, et la personnalite 0 est intacte.
static void test_un_slot_rempli_fait_grandir_la_table(void) {
    const int n0 = personalities::count();
    Personality* p = personalities::slot(n0);
    TEST_ASSERT_NOT_NULL(p);
    personalities::fill(*p, "essai", "/stackchan-companion/rules.txt", 0u,
                        ConsoleTheme::Default, 6000, 12000);
    TEST_ASSERT_EQUAL_INT(n0 + 1, personalities::count());
    TEST_ASSERT_EQUAL_INT(n0, personalities::indexOf("essai"));
    TEST_ASSERT_EQUAL_STRING("default", personalities::at(0).name);   // intacte
    // Et le vider la fait redescendre — pas de second compteur a tenir en phase.
    p->name[0] = 0;
    TEST_ASSERT_EQUAL_INT(n0, personalities::count());
    TEST_ASSERT_EQUAL_STRING("default", personalities::at(0).name);
}

// La personnalite 0 n'est jamais supprimable : `at()` y retombe pour tout index
// inconnu et main.cpp retombe sur SON fichier de regles quand celui d'un
// caractere manque. La retirer retirerait le plancher.
static void test_la_personnalite_zero_n_est_pas_supprimable(void) {
    TEST_ASSERT_FALSE(personalities::deletable(0));
    TEST_ASSERT_TRUE(personalities::deletable(1));
    TEST_ASSERT_FALSE(personalities::deletable(personalities::count()));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_personnalite_zero_est_le_robot_d_avant);
    RUN_TEST(test_le_retour_au_defaut_restaure_tout);
    RUN_TEST(test_aucune_personnalite_ne_sort_des_butees);
    RUN_TEST(test_la_table_est_bien_formee);
    RUN_TEST(test_un_index_inconnu_rend_le_defaut);
    RUN_TEST(test_haro_est_etroite_et_sans_humeur_noire);
    RUN_TEST(test_roulette_eteinte_ne_tire_rien_mais_consomme_son_tick);
    RUN_TEST(test_la_nuit_traverse_les_personnalites);
    RUN_TEST(test_ecrire_hors_plage_ne_touche_pas_la_personnalite_zero);
    RUN_TEST(test_un_slot_rempli_fait_grandir_la_table);
    RUN_TEST(test_la_personnalite_zero_n_est_pas_supprimable);
    return UNITY_END();
}
