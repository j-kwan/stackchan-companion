// =============================================================================
// test_cfgbool — ce qui compte pour VRAI dans une valeur de configuration
// =============================================================================
// Trois bins avaient chacun leur reponse, et l'une etait fausse : un test sur
// le PREMIER CARACTERE rendait `off` vrai, parce que « off » commence comme
// « on ». Un mot qui signifie faux dans toutes les langues allumait l'option,
// et rien ne le disait — l'option se comportait comme si elle n'avait jamais
// ete reglee.
//
// Ces cas fixent la reponse UNE fois, et le premier d'entre eux est celui qui
// prenait le code en defaut.
// Execution : .\scripts\gates\test-native.ps1 test_cfgbool
// =============================================================================

#include <unity.h>
#include "../../firmware/common/CfgBool.h"

using sce::webBool;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// ---- LE cas : "off" est FAUX -----------------------------------------------
static void test_off_est_faux(void) {
    TEST_ASSERT_FALSE(webBool("off"));
    TEST_ASSERT_FALSE(webBool("OFF"));
    TEST_ASSERT_FALSE(webBool("Off"));
}

// ---- ce qui est vrai, et rien d'autre --------------------------------------
static void test_les_vrais(void) {
    const char* vrais[] = { "1", "on", "true", "yes", "y", "t",
                            "ON", "True", "YES", "T" };
    for (unsigned i = 0; i < sizeof(vrais) / sizeof(vrais[0]); i++)
        TEST_ASSERT_TRUE(webBool(vrais[i]));
}

static void test_les_faux(void) {
    const char* faux[] = { "0", "no", "n", "false", "f", "", "off",
                           "onn", "tru", "ye", "2", "-1", "oui", "vrai" };
    for (unsigned i = 0; i < sizeof(faux) / sizeof(faux[0]); i++)
        TEST_ASSERT_FALSE(webBool(faux[i]));
}

// ---- un pointeur nul ne casse rien -----------------------------------------
static void test_pointeur_nul(void) {
    TEST_ASSERT_FALSE(webBool(nullptr));
}

// ---- une valeur de formulaire peut arriver avec du remplissage --------------
static void test_blancs_en_tete(void) {
    TEST_ASSERT_TRUE(webBool("  1"));
    TEST_ASSERT_TRUE(webBool("\ton"));
    TEST_ASSERT_FALSE(webBool("   off"));
}

// ---- EXACT, et pas un prefixe ----------------------------------------------
// C'est la propriete qui empeche le retour du bug : "onn" et "yesterday"
// commencent par un mot vrai sans en etre un.
static void test_exact_et_non_prefixe(void) {
    TEST_ASSERT_FALSE(webBool("yesterday"));
    TEST_ASSERT_FALSE(webBool("tomorrow"));
    TEST_ASSERT_FALSE(webBool("online"));
    TEST_ASSERT_FALSE(webBool("1000"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_off_est_faux);
    RUN_TEST(test_les_vrais);
    RUN_TEST(test_les_faux);
    RUN_TEST(test_pointeur_nul);
    RUN_TEST(test_blancs_en_tete);
    RUN_TEST(test_exact_et_non_prefixe);
    return UNITY_END();
}
