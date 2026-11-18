// =============================================================================
// test_rulestore — parser de règles SD app/RuleStore.h (plugins réactifs)
// =============================================================================
// Vérifie le parsing d'une ligne : champs, ops, actions (AmbientDark,
// SetEmotion par nom, PlayDance par nom, set), commentaires/vides ignorés,
// lignes invalides rejetées, gate enable optionnel.
// =============================================================================

#include <unity.h>
#include <cstring>
#include "app/RuleStore.h"

using namespace sce;

static Command g_last;
static int     g_n = 0;
static void cap(void*, const Command& c) { g_last = c; g_n++; }

static char   arena[512];
static int    used;

extern "C" void setUp(void)    { used = 0; g_n = 0; }
extern "C" void tearDown(void) {}

// parseLine modifie la ligne → on copie dans un buffer mutable
static bool parse(const char* src, RuleEngine& eng) {
    char buf[128]; strncpy(buf, src, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    return RuleStore::parseLine(buf, arena, sizeof(arena), used, eng);
}

static void test_valid_ambientdark(void) {
    RuleEngine eng(*(new FieldStore()), cap, nullptr);
    TEST_ASSERT_TRUE(parse("dark_sleepy | light | le | 1 | 6000 | 2000 | AmbientDark | 1", eng));
    TEST_ASSERT_EQUAL_INT(1, eng.count());
}

static void test_comment_and_blank(void) {
    RuleEngine eng(*(new FieldStore()), cap, nullptr);
    TEST_ASSERT_FALSE(parse("# ceci est un commentaire", eng));
    TEST_ASSERT_FALSE(parse("   ", eng));
    TEST_ASSERT_FALSE(parse("", eng));
    TEST_ASSERT_EQUAL_INT(0, eng.count());
}

static void test_setemotion_by_name(void) {
    FieldStore* fs = new FieldStore();
    RuleEngine eng(*fs, cap, nullptr);
    TEST_ASSERT_TRUE(parse(" | approval | ge | 1 | 0 | 3000 | SetEmotion | Questioning | 1500", eng));
    // déclenche : approval >= 1
    fs->set("approval", 1.0f);
    eng.update(1000);
    TEST_ASSERT_EQUAL_INT(1, g_n);
    TEST_ASSERT_TRUE(g_last.type == CmdType::SetEmotion);
    TEST_ASSERT_EQUAL_INT((int)emotionFromName("Questioning"), g_last.i);
    TEST_ASSERT_EQUAL_UINT32(1500, g_last.u);
}

static void test_unknown_emotion_rejected(void) {
    RuleEngine eng(*(new FieldStore()), cap, nullptr);
    TEST_ASSERT_FALSE(parse(" | x | gt | 1 | 0 | 0 | SetEmotion | PasUneEmotion", eng));
    TEST_ASSERT_EQUAL_INT(0, eng.count());
}

static void test_invalid_op_rejected(void) {
    RuleEngine eng(*(new FieldStore()), cap, nullptr);
    TEST_ASSERT_FALSE(parse(" | x | ~~ | 1 | 0 | 0 | Blink", eng));
    TEST_ASSERT_EQUAL_INT(0, eng.count());
}

static void test_too_few_fields(void) {
    RuleEngine eng(*(new FieldStore()), cap, nullptr);
    TEST_ASSERT_FALSE(parse("x | gt | 1", eng));    // pas d'action
    TEST_ASSERT_EQUAL_INT(0, eng.count());
}

static void test_set_action(void) {
    FieldStore* fs = new FieldStore();
    RuleEngine eng(*fs, cap, nullptr);
    TEST_ASSERT_TRUE(parse(" | trigger | ge | 1 | 0 | 0 | set | decision | 7", eng));
    fs->set("trigger", 1.0f);
    eng.update(100);
    TEST_ASSERT_EQUAL_FLOAT(7.0f, fs->getF("decision", -1.0f));
    TEST_ASSERT_EQUAL_INT(0, g_n);                  // set n'est pas une Command
}

static void test_playdance_by_name(void) {
    FieldStore* fs = new FieldStore();
    RuleEngine eng(*fs, cap, nullptr);
    TEST_ASSERT_TRUE(parse(" | party | ge | 1 | 0 | 0 | PlayDance | greet", eng));
    fs->set("party", 1.0f);
    eng.update(100);
    TEST_ASSERT_TRUE(g_last.type == CmdType::PlayDance);
    TEST_ASSERT_EQUAL_INT(dances::indexOf("greet"), g_last.i);
    // ...et le NOM voyage avec, parce que c'est lui que le puits applicatif
    // resout au declenchement.
    TEST_ASSERT_NOT_NULL(g_last.ptr);
    TEST_ASSERT_EQUAL_STRING("greet", (const char*)g_last.ptr);
}

// ---- LE NOM VOYAGE, PAS L'INDEX -------------------------------------------
// C'est la regression que ce changement existe pour empecher. Une danse de la
// CARTE n'est pas dans la table compilee : `indexOf` rend -1, et l'ancien
// parseur REFUSAIT la ligne — silencieusement. Le fichier avait une regle, le
// moteur n'en avait aucune, et rien ne le disait.
//
// Deux faits rendent la resolution tardive obligatoire plutot que preferable :
// au demarrage les regles sont lues AVANT `danceStore.reload()` (la banque est
// donc vide quand cette ligne est analysee), et DanceStore est DOUBLE-BANQUE —
// un televersement renumerote ce qui suit, donc un index memorise designerait
// une autre danse apres coup.
static void test_une_danse_de_la_carte_est_acceptee_et_nommee(void) {
    FieldStore* fs = new FieldStore();
    RuleEngine eng(*fs, cap, nullptr);
    // Nom absent de la table compilee : exactement le cas des 4 CSV Haro.
    TEST_ASSERT_EQUAL_INT(-1, dances::indexOf("haro_float"));
    TEST_ASSERT_TRUE(parse(" | party | ge | 1 | 0 | 0 | PlayDance | haro_float", eng));
    fs->set("party", 1.0f);
    eng.update(100);
    TEST_ASSERT_TRUE(g_last.type == CmdType::PlayDance);
    TEST_ASSERT_EQUAL_STRING("haro_float", (const char*)g_last.ptr);
    // L'index reste -1 : c'est au puits de resoudre, pas au parseur de deviner.
    TEST_ASSERT_EQUAL_INT(-1, g_last.i);
}

// Le pointeur doit rester valide aussi longtemps que la regle : il pointe dans
// l'arene de RuleStore (membre d'un objet de portee fichier), jamais dans la
// ligne analysee, qui vit sur la pile de l'appelant et sera reecrite a la
// ligne suivante. On le verifie en analysant une SECONDE regle par-dessus le
// meme tampon et en relisant la premiere.
static void test_le_nom_survit_a_la_ligne_suivante(void) {
    FieldStore* fs = new FieldStore();
    RuleEngine eng(*fs, cap, nullptr);
    TEST_ASSERT_TRUE(parse(" | a | ge | 1 | 0 | 0 | PlayDance | haro_call", eng));
    const void* first = eng.at(0).cmd.ptr;
    TEST_ASSERT_TRUE(parse(" | b | ge | 1 | 0 | 0 | PlayDance | haro_scan", eng));
    TEST_ASSERT_EQUAL_STRING("haro_call", (const char*)first);
    TEST_ASSERT_EQUAL_STRING("haro_scan", (const char*)eng.at(1).cmd.ptr);
}

// Un nom VIDE reste refuse : `PlayDance` sans argument n'est pas une action.
static void test_playdance_sans_nom_refuse(void) {
    FieldStore* fs = new FieldStore();
    RuleEngine eng(*fs, cap, nullptr);
    TEST_ASSERT_FALSE(parse(" | party | ge | 1 | 0 | 0 | PlayDance | ", eng));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_ambientdark);
    RUN_TEST(test_comment_and_blank);
    RUN_TEST(test_setemotion_by_name);
    RUN_TEST(test_unknown_emotion_rejected);
    RUN_TEST(test_invalid_op_rejected);
    RUN_TEST(test_too_few_fields);
    RUN_TEST(test_set_action);
    RUN_TEST(test_playdance_by_name);
    RUN_TEST(test_une_danse_de_la_carte_est_acceptee_et_nommee);
    RUN_TEST(test_le_nom_survit_a_la_ligne_suivante);
    RUN_TEST(test_playdance_sans_nom_refuse);
    return UNITY_END();
}
