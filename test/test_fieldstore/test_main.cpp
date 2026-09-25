// =============================================================================
// test_fieldstore — blackboard engine/FieldStore.h (contrat statusbar/plugins)
// =============================================================================
// Vérifie le magasin de champs : set/get float+chaîne, upsert, défauts,
// conservation de chaîne quand seul le float change, capacité.
// =============================================================================

#include <unity.h>
#include <cstdio>
#include <cstring>
#include "engine/FieldStore.h"

using namespace sce;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

static void test_set_get_float(void) {
    FieldStore fs;
    TEST_ASSERT_TRUE(fs.set("ctx", 62.0f));
    TEST_ASSERT_EQUAL_FLOAT(62.0f, fs.getF("ctx"));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, fs.getF("absent", -1.0f));   // défaut
    TEST_ASSERT_TRUE(fs.has("ctx"));
    TEST_ASSERT_FALSE(fs.has("absent"));
}

static void test_upsert(void) {
    FieldStore fs;
    fs.set("h5", 10.0f);
    fs.set("h5", 41.0f);                 // même clé → mise à jour, pas d'ajout
    TEST_ASSERT_EQUAL_FLOAT(41.0f, fs.getF("h5"));
    TEST_ASSERT_EQUAL_INT(1, fs.count());
}

static void test_string_value(void) {
    FieldStore fs;
    char buf[24];
    fs.set("label", 0.0f, "stackchan-companion");
    TEST_ASSERT_TRUE(fs.getS("label", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("stackchan-companion", buf);  // tronqué à STR_LEN-1 si long
    // chaîne absente → out vidé, false
    TEST_ASSERT_FALSE(fs.getS("ctx", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

static void test_float_update_keeps_string(void) {
    FieldStore fs;
    char buf[24];
    fs.set("g", 20.0f, "abc");
    fs.set("g", 55.0f);                  // s=nullptr → chaîne conservée
    TEST_ASSERT_EQUAL_FLOAT(55.0f, fs.getF("g"));
    TEST_ASSERT_TRUE(fs.getS("g", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("abc", buf);
}

static void test_string_truncation(void) {
    FieldStore fs;
    char buf[24];
    fs.set("k", 0.0f, "0123456789ABCDEFGHIJKLMNOPQRSTUV");  // > STR_LEN
    fs.getS("k", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(FieldStore::STR_LEN - 1, (int)strlen(buf));
}

static void test_capacity(void) {
    FieldStore fs;
    char key[8];
    for (int i = 0; i < FieldStore::MAX_FIELDS; i++) {
        snprintf(key, sizeof(key), "f%d", i);
        TEST_ASSERT_TRUE(fs.set(key, (float)i));
    }
    TEST_ASSERT_FALSE(fs.set("overflow", 1.0f));   // pleine, clé inconnue
    TEST_ASSERT_TRUE(fs.set("f0", 999.0f));        // clé connue → OK même pleine
    TEST_ASSERT_EQUAL_FLOAT(999.0f, fs.getF("f0"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_set_get_float);
    RUN_TEST(test_upsert);
    RUN_TEST(test_string_value);
    RUN_TEST(test_float_update_keeps_string);
    RUN_TEST(test_string_truncation);
    RUN_TEST(test_capacity);
    return UNITY_END();
}
