// =============================================================================
// test_yaml — the ONE YAML line decoder (firmware/common/Yaml.h)
// =============================================================================
// This suite exists because the project has already PAID for its absence:
// rule 17 records four divergent parsers, two outages (a quoted SSID on
// 2026-07-25, a quoted ADS-B source on 07-29) and a pair of "assumed twins"
// that diverged once more (07-29). Every case below is either one of those
// failures or the shape that caused it.
// =============================================================================
#include <unity.h>
#include <string.h>
#include <stdio.h>   // snprintf, for the write/read round trip below
#include "../../firmware/common/Yaml.h"

void setUp(void) {}
void tearDown(void) {}

// The decoder mutates its input, so every case gets its own copy.
static sce::yaml::Line decode(const char* src, char* scratch, size_t n) {
    strncpy(scratch, src, n - 1);
    scratch[n - 1] = '\0';
    return sce::yaml::decodeLine(scratch);
}
#define DEC(s) char _b[512]; sce::yaml::Line L = decode((s), _b, sizeof(_b))

// ----------------------------------------------------------------- basics
static void test_paire_simple(void) {
    DEC("poll_s: 30");
    TEST_ASSERT_TRUE(L.ok);
    TEST_ASSERT_FALSE(L.indented);
    TEST_ASSERT_EQUAL_STRING("poll_s", L.key);
    TEST_ASSERT_EQUAL_STRING("30", L.val);
}

static void test_espaces_autour_de_la_cle_et_de_la_valeur(void) {
    DEC("   radius_nm   :    500   ");
    TEST_ASSERT_TRUE(L.ok);
    TEST_ASSERT_TRUE(L.indented);
    TEST_ASSERT_EQUAL_STRING("radius_nm", L.key);
    TEST_ASSERT_EQUAL_STRING("500", L.val);
}

// The indentation decides section header vs key, and it is read BEFORE any
// trim — a trim first and the distinction is gone.
static void test_indentation_lue_avant_le_trim(void) {
    { DEC("wifi:");        TEST_ASSERT_TRUE(L.ok); TEST_ASSERT_FALSE(L.indented); }
    { DEC("  client_ssid: x"); TEST_ASSERT_TRUE(L.ok); TEST_ASSERT_TRUE(L.indented); }
    { DEC("\tclient_ssid: x"); TEST_ASSERT_TRUE(L.ok); TEST_ASSERT_TRUE(L.indented); }
}

static void test_section_valeur_vide(void) {
    DEC("tuning:");
    TEST_ASSERT_TRUE(L.ok);
    TEST_ASSERT_EQUAL_STRING("tuning", L.key);
    TEST_ASSERT_EQUAL_STRING("", L.val);
}

// ------------------------------------------------- the outages, verbatim
// 2026-07-25: a quoted SSID arrived WITH its quotes, matched no network.
static void test_ssid_entre_guillemets_perd_ses_guillemets(void) {
    DEC("  client_ssid: \"Livebox-1234\"");
    TEST_ASSERT_EQUAL_STRING("client_ssid", L.key);
    TEST_ASSERT_EQUAL_STRING("Livebox-1234", L.val);
}

// 2026-07-29: `api: "adsb.fi"` arrived quoted, matched no known source, and the
// radar silently fell back to another API.
static void test_source_adsb_entre_guillemets(void) {
    DEC("api: \"adsb.fi\"");
    TEST_ASSERT_EQUAL_STRING("adsb.fi", L.val);
}

// A quoted value KEEPS its '#' and its spaces — this is the whole reason the
// quote test must come before the comment cut. A password is the case that
// makes it matter.
static void test_valeur_quotee_garde_diese_et_espaces(void) {
    DEC("  client_password: \"a#b c \"");
    TEST_ASSERT_EQUAL_STRING("a#b c ", L.val);
}

// `password: ""` is the EMPTY string, not two literal quote characters.
static void test_valeur_quotee_vide(void) {
    DEC("  ap_password: \"\"");
    TEST_ASSERT_TRUE(L.ok);
    TEST_ASSERT_EQUAL_STRING("", L.val);
}

static void test_apostrophes_aussi(void) {
    DEC("safesky_key: 'abc#def'");
    TEST_ASSERT_EQUAL_STRING("abc#def", L.val);
}

// ------------------------------------------------------------- comments
static void test_commentaire_de_fin_de_ligne_coupe_et_trime(void) {
    DEC("poll_s: 30   # secondes");
    TEST_ASSERT_EQUAL_STRING("30", L.val);
}

static void test_ligne_de_commentaire_entiere_ignoree(void) {
    { DEC("# rien ici"); TEST_ASSERT_FALSE(L.ok); }
    { DEC("    # meme indentee: avec un deux-points"); TEST_ASSERT_FALSE(L.ok); }
}

static void test_ligne_vide_ou_sans_deux_points(void) {
    { DEC("");            TEST_ASSERT_FALSE(L.ok); }
    { DEC("      ");      TEST_ASSERT_FALSE(L.ok); }
    { DEC("pas de paire"); TEST_ASSERT_FALSE(L.ok); }
}

// ------------------------------------------------------------- oddities
// A value may legitimately contain ':' (a time, a URL). Only the FIRST one
// splits.
static void test_seul_le_premier_deux_points_separe(void) {
    DEC("url: https://example.org:8080/a");
    TEST_ASSERT_EQUAL_STRING("url", L.key);
    TEST_ASSERT_EQUAL_STRING("https://example.org:8080/a", L.val);
}

// An unterminated quote degrades to "everything after the opening one" rather
// than dropping the line: a truncated password is visible, a missing one is
// a mystery.
static void test_guillemet_non_ferme_degrade(void) {
    DEC("notam_pass: \"abcdef");
    TEST_ASSERT_EQUAL_STRING("abcdef", L.val);
}

// A stray '\r' anywhere must not reach a WiFi password.
static void test_retours_chariot_retires_partout(void) {
    DEC("  client_ssid: Liv\rebox\r");
    TEST_ASSERT_EQUAL_STRING("Livebox", L.val);
}

static void test_valeur_absente(void) {
    DEC("lang:");
    TEST_ASSERT_TRUE(L.ok);
    TEST_ASSERT_EQUAL_STRING("lang", L.key);
    TEST_ASSERT_EQUAL_STRING("", L.val);
}

// The token persisted by flight-radar is long, quoted, and round-trips.
static void test_jeton_long_quote(void) {
    DEC("notam_tok: \"3308fdc3aa11bb22cc33dd44ee55ff6677889900aabbccddeeff0011\"");
    TEST_ASSERT_EQUAL_STRING(
        "3308fdc3aa11bb22cc33dd44ee55ff6677889900aabbccddeeff0011", L.val);
}

static void test_buffer_nul(void) {
    sce::yaml::Line L = sce::yaml::decodeLine(nullptr);
    TEST_ASSERT_FALSE(L.ok);
}

// ---------------------------------------------------------------- harness

// ------------------------------------------- passphrases a caracteres speciaux
// Une phrase secrete WPA-PSK, c'est 8 a 63 caracteres ASCII IMPRIMABLES : le
// guillemet et l'antislash en font partie. Ils etaient SUPPRIMES en silence des
// deux cotes (le filtre de /api/wifi, puis `yamlQuote`), donc le robot
// rejoignait le reseau une fois — avec la valeur saisie, encore en RAM — et
// echouait pour toujours apres le premier redemarrage, avec la valeur amputee
// relue de la carte. Rien a l'ecran ne pouvait le dire.
static void test_guillemet_echappe_dans_une_valeur(void) {
    DEC("  client_password: \"a\\\"b\"");
    TEST_ASSERT_TRUE(L.ok);
    TEST_ASSERT_EQUAL_STRING("client_password", L.key);
    TEST_ASSERT_EQUAL_STRING("a\"b", L.val);
}

static void test_antislash_echappe(void) {
    DEC("  client_password: \"a\\\\b\"");
    TEST_ASSERT_EQUAL_STRING("a\\b", L.val);
}

// Le cas qui pique : un antislash JUSTE avant la quote fermante. Sans decodage
// des echappements, la quote est prise pour la fin et le reste de la ligne est
// perdu.
static void test_antislash_avant_la_quote_fermante(void) {
    DEC("  client_password: \"fin\\\\\"");
    TEST_ASSERT_EQUAL_STRING("fin\\", L.val);
}

// Tout le reste du clavier passait deja, et doit continuer : diese, deux-points,
// espaces, esperluette, pourcent, plus, chevrons, apostrophe.
static void test_le_reste_des_caracteres_speciaux_passe(void) {
    DEC("  client_password: \"a#b:c d&e%f+g<h>i'j\"");
    TEST_ASSERT_EQUAL_STRING("a#b:c d&e%f+g<h>i'j", L.val);
}

// Les guillemets SIMPLES ne prennent pas d'echappement — c'est la regle de YAML,
// et c'est ce qui permet d'ecrire un antislash sans rien doubler.
static void test_les_quotes_simples_ne_prennent_pas_d_echappement(void) {
    DEC("  client_password: 'a\\\\b'");
    TEST_ASSERT_EQUAL_STRING("a\\\\b", L.val);
}

// LE ROND : ce que `SdConfig::yamlQuote` ecrit doit se relire a l'identique.
// La fonction n'est pas incluable ici (Arduino String), donc sa regle est
// reproduite — et c'est justement cette regle qui est verifiee.
static void quote_like_sdconfig(const char* in, char* out) {
    char* w = out;
    *w++ = '"';
    for (const char* r = in; *r; r++) {
        if (*r == '"' || *r == '\\') *w++ = '\\';
        *w++ = *r;
    }
    *w++ = '"';
    *w = '\0';
}

static void test_aller_retour_ecriture_relecture(void) {
    static const char* CASES[] = {
        "simple", "avec des espaces", "a#b", "cle: valeur", "100%",
        "gui\"llemet", "anti\\slash", "les \"deux\" \\ ensemble",
        "\\", "\"", "fin\\", "\"debut", "", "e\xCC\x81t\xC3\xA9 caf\xC3\xA9",
    };
    for (unsigned i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        char quoted[256], line[300], scratch[300];
        quote_like_sdconfig(CASES[i], quoted);
        snprintf(line, sizeof(line), "  client_password: %s", quoted);
        sce::yaml::Line r = decode(line, scratch, sizeof(scratch));
        TEST_ASSERT_TRUE(r.ok);
        TEST_ASSERT_EQUAL_STRING(CASES[i], r.val);
    }
}


int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_paire_simple);
    RUN_TEST(test_espaces_autour_de_la_cle_et_de_la_valeur);
    RUN_TEST(test_indentation_lue_avant_le_trim);
    RUN_TEST(test_section_valeur_vide);
    RUN_TEST(test_ssid_entre_guillemets_perd_ses_guillemets);
    RUN_TEST(test_source_adsb_entre_guillemets);
    RUN_TEST(test_valeur_quotee_garde_diese_et_espaces);
    RUN_TEST(test_valeur_quotee_vide);
    RUN_TEST(test_apostrophes_aussi);
    RUN_TEST(test_commentaire_de_fin_de_ligne_coupe_et_trime);
    RUN_TEST(test_ligne_de_commentaire_entiere_ignoree);
    RUN_TEST(test_ligne_vide_ou_sans_deux_points);
    RUN_TEST(test_seul_le_premier_deux_points_separe);
    RUN_TEST(test_guillemet_non_ferme_degrade);
    RUN_TEST(test_retours_chariot_retires_partout);
    RUN_TEST(test_valeur_absente);
    RUN_TEST(test_jeton_long_quote);
    RUN_TEST(test_guillemet_echappe_dans_une_valeur);
    RUN_TEST(test_antislash_echappe);
    RUN_TEST(test_antislash_avant_la_quote_fermante);
    RUN_TEST(test_le_reste_des_caracteres_speciaux_passe);
    RUN_TEST(test_les_quotes_simples_ne_prennent_pas_d_echappement);
    RUN_TEST(test_aller_retour_ecriture_relecture);
    RUN_TEST(test_buffer_nul);
    return UNITY_END();
}
