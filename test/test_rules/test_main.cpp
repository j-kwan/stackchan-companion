// =============================================================================
// test_rules — moteur de règles behavior/RuleEngine.h (contrat réactif)
// =============================================================================
// Vérifie la sémantique « front tenu » : sustain, anti-spam (armé), retour à
// faux, cooldown, gate enableKey, action set-field, et le cas dark_sleepy.
// =============================================================================

#include <unity.h>
#include <cstring>
#include "behavior/RuleEngine.h"

using namespace sce;

// Capture des Commands postées
static Command g_posted[16];
static int     g_nposted = 0;
static void capture(void*, const Command& c) { g_posted[g_nposted++] = c; }

extern "C" void setUp(void)    { g_nposted = 0; }
extern "C" void tearDown(void) {}

// Règle simple : field > value → SetEmotion (i=42)
static RuleEngine::Rule ruleGT(const char* f, float v, uint32_t sustain = 0,
                               uint32_t cd = 0, const char* en = nullptr) {
    RuleEngine::Rule r;
    r.enableKey = en; r.field = f; r.op = RuleEngine::GT; r.value = v;
    r.sustainMs = sustain; r.cooldownMs = cd;
    r.cmd.type = CmdType::SetEmotion; r.cmd.i = 42;
    return r;
}

static void test_instant_edge_fires_once(void) {
    FieldStore fs; RuleEngine re(fs, capture, nullptr);
    re.add(ruleGT("x", 90.0f));
    fs.set("x", 50.0f); re.update(1000);            // faux
    TEST_ASSERT_EQUAL_INT(0, g_nposted);
    fs.set("x", 95.0f); re.update(1010);            // front vrai → 1 fois
    TEST_ASSERT_EQUAL_INT(1, g_nposted);
    re.update(1020); re.update(1030);               // reste vrai → pas de spam
    TEST_ASSERT_EQUAL_INT(1, g_nposted);
    TEST_ASSERT_EQUAL_INT(42, g_posted[0].i);
}

static void test_reset_on_false_refires(void) {
    FieldStore fs; RuleEngine re(fs, capture, nullptr);
    re.add(ruleGT("x", 90.0f));
    fs.set("x", 95.0f); re.update(100);             // front 1
    fs.set("x", 10.0f); re.update(110);             // retour faux
    fs.set("x", 95.0f); re.update(120);             // front 2
    TEST_ASSERT_EQUAL_INT(2, g_nposted);
}

static void test_sustain(void) {
    FieldStore fs; RuleEngine re(fs, capture, nullptr);
    re.add(ruleGT("x", 90.0f, /*sustain*/6000));
    fs.set("x", 95.0f);
    re.update(1000);                                 // début front
    re.update(4000); TEST_ASSERT_EQUAL_INT(0, g_nposted);   // pas assez tenu
    re.update(7001); TEST_ASSERT_EQUAL_INT(1, g_nposted);   // 6001 ms → feu
}

static void test_sustain_broken_before_fire(void) {
    FieldStore fs; RuleEngine re(fs, capture, nullptr);
    re.add(ruleGT("x", 90.0f, 6000));
    fs.set("x", 95.0f); re.update(1000);
    fs.set("x", 10.0f); re.update(3000);            // condition rompue avant 6 s
    fs.set("x", 95.0f); re.update(4000);            // redémarre le compteur
    re.update(9000); TEST_ASSERT_EQUAL_INT(0, g_nposted);   // 5 s seulement
    re.update(10001); TEST_ASSERT_EQUAL_INT(1, g_nposted);  // 6001 ms
}

static void test_cooldown(void) {
    FieldStore fs; RuleEngine re(fs, capture, nullptr);
    re.add(ruleGT("x", 90.0f, 0, /*cooldown*/5000));
    fs.set("x", 95.0f); re.update(1000);            // feu 1
    fs.set("x", 10.0f); re.update(1100);
    fs.set("x", 95.0f); re.update(2000);            // front dans le cooldown → non
    TEST_ASSERT_EQUAL_INT(1, g_nposted);
    fs.set("x", 10.0f); re.update(6500);
    fs.set("x", 95.0f); re.update(7000);            // hors cooldown → feu 2
    TEST_ASSERT_EQUAL_INT(2, g_nposted);
}

static void test_enable_gate(void) {
    FieldStore fs; RuleEngine re(fs, capture, nullptr);
    re.add(ruleGT("x", 90.0f, 0, 0, /*enable*/"on"));
    fs.set("x", 95.0f);
    fs.set("on", 0.0f); re.update(1000);            // désactivée → rien
    TEST_ASSERT_EQUAL_INT(0, g_nposted);
    fs.set("on", 1.0f); re.update(1010);            // activée + condition vraie → feu
    TEST_ASSERT_EQUAL_INT(1, g_nposted);
}

static void test_set_action(void) {
    FieldStore fs; RuleEngine re(fs, capture, nullptr);
    RuleEngine::Rule r; r.field = "trigger"; r.op = RuleEngine::GE; r.value = 1.0f;
    r.setAction = true; r.setKey = "decision"; r.setVal = 7.0f;
    re.add(r);
    fs.set("trigger", 1.0f); re.update(100);
    TEST_ASSERT_EQUAL_FLOAT(7.0f, fs.getF("decision", -1.0f));
    TEST_ASSERT_EQUAL_INT(0, g_nposted);            // pas de Command
}

// dark_sleepy exprimé en règles : LE 1 (sustain 6 s) → AmbientDark 1 ;
// GT 10 → AmbientDark 0. Gate "dark_sleepy".
static void test_dark_sleepy_as_rules(void) {
    FieldStore fs; RuleEngine re(fs, capture, nullptr);
    RuleEngine::Rule sleep;
    sleep.enableKey = "dark_sleepy"; sleep.field = "light";
    sleep.op = RuleEngine::LE; sleep.value = 1.0f; sleep.sustainMs = 6000;
    sleep.cmd.type = CmdType::AmbientDark; sleep.cmd.i = 1;
    RuleEngine::Rule wake;
    wake.enableKey = "dark_sleepy"; wake.field = "light";
    wake.op = RuleEngine::GT; wake.value = 10.0f;
    wake.cmd.type = CmdType::AmbientDark; wake.cmd.i = 0;
    re.add(sleep); re.add(wake);

    fs.set("dark_sleepy", 1.0f);
    fs.set("light", 0.0f);
    re.update(1000); re.update(3000);
    TEST_ASSERT_EQUAL_INT(0, g_nposted);            // pas encore 6 s
    re.update(7001);
    TEST_ASSERT_EQUAL_INT(1, g_nposted);            // AmbientDark 1
    TEST_ASSERT_EQUAL_INT(1, g_posted[0].i);
    TEST_ASSERT_TRUE(g_posted[0].type == CmdType::AmbientDark);

    fs.set("light", 55.0f);                         // lumière revenue
    re.update(8000);
    TEST_ASSERT_EQUAL_INT(2, g_nposted);            // AmbientDark 0
    TEST_ASSERT_EQUAL_INT(0, g_posted[1].i);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_instant_edge_fires_once);
    RUN_TEST(test_reset_on_false_refires);
    RUN_TEST(test_sustain);
    RUN_TEST(test_sustain_broken_before_fire);
    RUN_TEST(test_cooldown);
    RUN_TEST(test_enable_gate);
    RUN_TEST(test_set_action);
    RUN_TEST(test_dark_sleepy_as_rules);
    return UNITY_END();
}
