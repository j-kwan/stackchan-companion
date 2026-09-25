// =============================================================================
// test_dirtybands — the dirty-row comparison (engine/DirtyBands.h)
// =============================================================================
// The dirty-band push is the biggest remaining win of the project AND the most
// sensitive path in it: the 30 expressions all go through it, and the promise
// is not "it looks the same", it is "the panel receives the same bytes". No
// hardware can prove that here, so this suite pins down the only thing that
// CAN be proved on a PC: the comparison never omits a row that changed.
//
// Every case below is either that invariant, one of its edges (empty diff,
// first/last row, merging, saturation) or a fuzz run that checks the
// coverage guarantee on thousands of random buffers.
//
// Execution: .\scripts\gates\test-native.ps1 test_dirtybands
// =============================================================================

#include <unity.h>
#include <string.h>
#include "engine/DirtyBands.h"

using namespace sce::dirty;

void setUp(void) {}
void tearDown(void) {}

// The real geometry, so the numbers below mean something: 320×160, 8 bpp.
static constexpr int W = 320;
static constexpr int H = 160;
static uint8_t g_cur[W * H];
static uint8_t g_ghost[W * H];

static void reset(void) {
    memset(g_cur, 0, sizeof(g_cur));
    memset(g_ghost, 0, sizeof(g_ghost));
}

// Marks row `y` as different (one byte is enough — memcmp is per row).
static void dirtyRow(int y, uint8_t v = 0xFF, int x = 0) {
    g_cur[(size_t)y * W + x] = v;
}

// ---------------------------------------------------------------------------
// THE guarantee: every differing row is covered by exactly one band, bands are
// ordered, disjoint, inside the canvas.
// ---------------------------------------------------------------------------
static void assertCovers(const Band* b, int n) {
    int prevEnd = 0;
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_GREATER_THAN_INT(0, (int)b[i].h);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(prevEnd, (int)b[i].y);   // ordered+disjoint
        TEST_ASSERT_LESS_OR_EQUAL_INT(H, (int)b[i].y + (int)b[i].h);
        prevEnd = b[i].y + b[i].h;
    }
    for (int y = 0; y < H; y++) {
        const size_t off = (size_t)y * W;
        const bool differs = memcmp(g_cur + off, g_ghost + off, W) != 0;
        if (!differs) continue;
        bool covered = false;
        for (int i = 0; i < n; i++)
            if (y >= b[i].y && y < b[i].y + b[i].h) covered = true;
        TEST_ASSERT_TRUE_MESSAGE(covered, "une ligne modifiee n'est dans aucune bande");
    }
}

// ------------------------------------------------------- identical → nothing
// The settled face. This is the case the whole change exists for: zero band,
// zero byte on the wire, 20,5 ms given back.
static void test_buffers_identiques_zero_bande(void) {
    reset();
    Band b[16];
    TEST_ASSERT_EQUAL_INT(0, diffBands(g_cur, g_ghost, W, H, b, 16, 1));
}

// --------------------------------------------------------- one row, one band
static void test_une_ligne_une_bande(void) {
    reset();
    dirtyRow(42);
    Band b[16];
    const int n = diffBands(g_cur, g_ghost, W, H, b, 16, 1);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT16(42, b[0].y);
    TEST_ASSERT_EQUAL_INT16(1, b[0].h);
    TEST_ASSERT_EQUAL_INT32(1, bandRows(b, n));
    assertCovers(b, n);
}

// The difference can be on the LAST byte of the row: comparing a prefix would
// pass this test and lose a column on hardware.
static void test_difference_sur_le_dernier_pixel_de_la_ligne(void) {
    reset();
    dirtyRow(7, 0x01, W - 1);
    Band b[16];
    const int n = diffBands(g_cur, g_ghost, W, H, b, 16, 0);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT16(7, b[0].y);
}

// First and last rows — the classic off-by-one of a row loop.
static void test_premiere_et_derniere_ligne(void) {
    reset();
    dirtyRow(0);
    dirtyRow(H - 1);
    Band b[16];
    const int n = diffBands(g_cur, g_ghost, W, H, b, 16, 1);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_INT16(0, b[0].y);
    TEST_ASSERT_EQUAL_INT16(1, b[0].h);
    TEST_ASSERT_EQUAL_INT16(H - 1, b[1].y);
    TEST_ASSERT_EQUAL_INT16(1, b[1].h);
    assertCovers(b, n);
}

// ------------------------------------------------------------ contiguous run
// An eye is a block of rows: it must come out as ONE band, whatever mergeGap
// says (adjacent rows have gap 0).
static void test_bloc_contigu_une_seule_bande(void) {
    reset();
    for (int y = 60; y < 100; y++) dirtyRow(y);
    Band b[16];
    const int n = diffBands(g_cur, g_ghost, W, H, b, 16, 0);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT16(60, b[0].y);
    TEST_ASSERT_EQUAL_INT16(40, b[0].h);
    assertCovers(b, n);
}

// --------------------------------------------------------------- the merging
static void test_ecart_inferieur_ou_egal_a_mergegap_fusionne(void) {
    reset();
    dirtyRow(10);
    dirtyRow(12);                      // one clean row between the two
    Band b[16];
    const int n = diffBands(g_cur, g_ghost, W, H, b, 16, 1);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT16(10, b[0].y);
    TEST_ASSERT_EQUAL_INT16(3, b[0].h);   // 10,11,12 — 11 is clean, pushed anyway
    assertCovers(b, n);
}

static void test_ecart_superieur_a_mergegap_separe(void) {
    reset();
    dirtyRow(10);
    dirtyRow(13);                      // two clean rows
    Band b[16];
    const int n = diffBands(g_cur, g_ghost, W, H, b, 16, 1);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_INT32(2, bandRows(b, n));
    assertCovers(b, n);
}

// mergeGap = 0 must NOT glue two rows separated by a clean one — that would
// silently push rows nobody asked for, and the whole point is the count.
static void test_mergegap_zero_ne_colle_pas(void) {
    reset();
    dirtyRow(10);
    dirtyRow(12);
    Band b[16];
    const int n = diffBands(g_cur, g_ghost, W, H, b, 16, 0);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_INT32(2, bandRows(b, n));
}

// A negative mergeGap is treated as 0, never as a wild merge.
static void test_mergegap_negatif_traite_comme_zero(void) {
    reset();
    dirtyRow(10);
    dirtyRow(12);
    Band b[16];
    TEST_ASSERT_EQUAL_INT(2, diffBands(g_cur, g_ghost, W, H, b, 16, -5));
}

// ------------------------------------------------- the SHIPPED calibration
// `Renderer::MERGE_GAP` went from 2 to 0 on 08-03 because the arithmetic was
// upside down: a clean row pushed costs 128 µs of wire, the band it saves
// costs ≤ 20 µs (DirtyBands.h header). These two cases pin the CONSEQUENCE of
// that value, not the value itself — with the shipped parameters, not one
// clean row reaches the panel, and a run of rows is still ONE band.
static void test_calibrage_livre_ne_pousse_aucune_ligne_propre(void) {
    reset();
    static constexpr int SHIPPED_GAP = 0, SHIPPED_MAX = 12;
    dirtyRow(10);
    dirtyRow(12);                       // 1 clean row  (gap 1)
    dirtyRow(15);                       // 2 clean rows (gap 2 — the OLD value)
    for (int y = 60; y < 100; y++) dirtyRow(y);          // an eye
    Band b[SHIPPED_MAX];
    const int n = diffBands(g_cur, g_ghost, W, H, b, SHIPPED_MAX, SHIPPED_GAP);
    TEST_ASSERT_EQUAL_INT(4, n);        // 10 | 12 | 15 | 60..99 — nothing glued
    TEST_ASSERT_EQUAL_INT32(43, bandRows(b, n));   // 3 + 40, ZERO clean row
    TEST_ASSERT_EQUAL_INT16(60, b[3].y);
    TEST_ASSERT_EQUAL_INT16(40, b[3].h);           // the eye stays one band
    assertCovers(b, n);
}

// The recalibration must never make the wire BUSIER than the value it
// replaces. Under the cap, gap 0 produces more raw bands and the fusion has to
// pick up the slack — so the claim is checked on random shapes rather than
// argued: at the shipped cap, gap 0 pushes at most as many rows as gap 2.
static void test_gap_zero_jamais_plus_de_lignes_que_gap_deux(void) {
    uint32_t seed = 0x5EEDBEEFu;
    auto next = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 16;
    };
    Band b0[12], b2[12];
    for (int it = 0; it < 1500; it++) {
        reset();
        const int k = (int)(next() % 45);
        for (int i = 0; i < k; i++) {
            const int y = (int)(next() % H);
            const int len = 1 + (int)(next() % 6);      // runs, like real shapes
            for (int j = 0; j < len && y + j < H; j++) dirtyRow(y + j);
        }
        const int n0 = diffBands(g_cur, g_ghost, W, H, b0, 12, 0);
        const int n2 = diffBands(g_cur, g_ghost, W, H, b2, 12, 2);
        assertCovers(b0, n0);
        TEST_ASSERT_LESS_OR_EQUAL_INT32(bandRows(b2, n2), bandRows(b0, n0));
    }
}

// ------------------------------------------------------------- saturation
// More scattered runs than the array can hold: the count MUST stay bounded and
// the coverage MUST hold. Losing a row here is a stale pixel on screen.
static void test_saturation_borne_le_nombre_de_bandes(void) {
    reset();
    for (int y = 0; y < H; y += 4) dirtyRow(y);      // 40 runs
    Band b[8];
    const int n = diffBands(g_cur, g_ghost, W, H, b, 8, 1);
    TEST_ASSERT_LESS_OR_EQUAL_INT(8, n);
    TEST_ASSERT_GREATER_THAN_INT(0, n);
    assertCovers(b, n);
}

// Saturation fuses the SMALLEST gap: two tight runs at the top and one far
// away at the bottom, with room for two bands → the tight pair merges and the
// distant run keeps its own band (fusing it in would push the whole canvas).
static void test_saturation_fusionne_le_plus_petit_ecart(void) {
    reset();
    dirtyRow(10);
    dirtyRow(14);
    dirtyRow(150);
    Band b[2];
    const int n = diffBands(g_cur, g_ghost, W, H, b, 2, 1);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_INT16(10, b[0].y);
    TEST_ASSERT_EQUAL_INT16(5, b[0].h);      // 10..14
    TEST_ASSERT_EQUAL_INT16(150, b[1].y);
    TEST_ASSERT_EQUAL_INT16(1, b[1].h);
    assertCovers(b, n);
}

// One single band available: everything collapses into one span, and that span
// still covers every dirty row (worst case = today's full push, never worse).
static void test_une_seule_bande_disponible(void) {
    reset();
    dirtyRow(3);
    dirtyRow(80);
    dirtyRow(159);
    Band b[1];
    const int n = diffBands(g_cur, g_ghost, W, H, b, 1, 0);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT16(3, b[0].y);
    TEST_ASSERT_EQUAL_INT16(157, b[0].h);
    assertCovers(b, n);
}

// --------------------------------------------------------- degenerate inputs
// The renderer falls back to a full pushSprite when this returns 0 on a null
// buffer, so "0" here has to mean 0 and not a crash.
static void test_entrees_degenerees(void) {
    reset();
    Band b[16];
    TEST_ASSERT_EQUAL_INT(0, diffBands(nullptr, g_ghost, W, H, b, 16, 1));
    TEST_ASSERT_EQUAL_INT(0, diffBands(g_cur, nullptr, W, H, b, 16, 1));
    TEST_ASSERT_EQUAL_INT(0, diffBands(g_cur, g_ghost, W, H, nullptr, 16, 1));
    TEST_ASSERT_EQUAL_INT(0, diffBands(g_cur, g_ghost, 0, H, b, 16, 1));
    TEST_ASSERT_EQUAL_INT(0, diffBands(g_cur, g_ghost, W, 0, b, 16, 1));
    TEST_ASSERT_EQUAL_INT(0, diffBands(g_cur, g_ghost, W, H, b, 0, 1));
}

// ------------------------------------------------------------------- shapes
// The frame the diagnosis singles out: both eyes closed = a 1 px line. Today
// it pushes 51 200 px to show 320. Here it must come out as ONE row.
static void test_forme_ligne_de_blink(void) {
    reset();
    for (int x = 0; x < W; x++) g_cur[(size_t)88 * W + x] = 0x1C;
    Band b[16];
    const int n = diffBands(g_cur, g_ghost, W, H, b, 16, 1);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT32(1, bandRows(b, n));
}

// A saccade moves the eyes horizontally: the ROWS of the eyes change, the rows
// above and below do not. One band, ~the height of an eye.
static void test_forme_saccade_horizontale(void) {
    reset();
    for (int y = 55; y < 105; y++)
        for (int x = 40; x < 280; x++) g_cur[(size_t)y * W + x] = (uint8_t)(x & 0xFF);
    Band b[16];
    const int n = diffBands(g_cur, g_ghost, W, H, b, 16, 1);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT16(55, b[0].y);
    TEST_ASSERT_EQUAL_INT16(50, b[0].h);
}

// ---------------------------------------------------------------------- fuzz
// The coverage guarantee, on thousands of random shapes and every merge/cap
// setting. A band list that misses one row leaves a stale pixel on the panel,
// and a stale pixel is exactly what no hardware test would reliably catch.
static void test_fuzz_couverture(void) {
    uint32_t seed = 0xC0FFEEu;
    auto next = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 16;
    };
    Band b[16];
    for (int it = 0; it < 2000; it++) {
        reset();
        const int k = (int)(next() % 40);
        for (int i = 0; i < k; i++) {
            const int y = (int)(next() % H);
            const int x = (int)(next() % W);
            g_cur[(size_t)y * W + x] = (uint8_t)(next() | 1u);
        }
        const int maxB = 1 + (int)(next() % 16);
        const int gap  = (int)(next() % 6);
        const int n = diffBands(g_cur, g_ghost, W, H, b, maxB, gap);
        TEST_ASSERT_LESS_OR_EQUAL_INT(maxB, n);
        assertCovers(b, n);
    }
}

// The symmetric fuzz: the ghost is the noisy one and `cur` is the clean image
// (what happens the frame AFTER a busy screen or a launcher exit). Same
// guarantee, and it catches an implementation that only ever reads one buffer.
static void test_fuzz_couverture_sens_inverse(void) {
    uint32_t seed = 0x1234567u;
    auto next = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 16;
    };
    Band b[16];
    for (int it = 0; it < 500; it++) {
        reset();
        const int k = (int)(next() % 30);
        for (int i = 0; i < k; i++)
            g_ghost[(size_t)(next() % H) * W + (next() % W)] = (uint8_t)(next() | 1u);
        const int n = diffBands(g_cur, g_ghost, W, H, b, 16, 2);
        assertCovers(b, n);
    }
}

// ---------------------------------------------------------------- harness
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_buffers_identiques_zero_bande);
    RUN_TEST(test_une_ligne_une_bande);
    RUN_TEST(test_difference_sur_le_dernier_pixel_de_la_ligne);
    RUN_TEST(test_premiere_et_derniere_ligne);
    RUN_TEST(test_bloc_contigu_une_seule_bande);
    RUN_TEST(test_ecart_inferieur_ou_egal_a_mergegap_fusionne);
    RUN_TEST(test_ecart_superieur_a_mergegap_separe);
    RUN_TEST(test_mergegap_zero_ne_colle_pas);
    RUN_TEST(test_mergegap_negatif_traite_comme_zero);
    RUN_TEST(test_calibrage_livre_ne_pousse_aucune_ligne_propre);
    RUN_TEST(test_gap_zero_jamais_plus_de_lignes_que_gap_deux);
    RUN_TEST(test_saturation_borne_le_nombre_de_bandes);
    RUN_TEST(test_saturation_fusionne_le_plus_petit_ecart);
    RUN_TEST(test_une_seule_bande_disponible);
    RUN_TEST(test_entrees_degenerees);
    RUN_TEST(test_forme_ligne_de_blink);
    RUN_TEST(test_forme_saccade_horizontale);
    RUN_TEST(test_fuzz_couverture);
    RUN_TEST(test_fuzz_couverture_sens_inverse);
    return UNITY_END();
}
