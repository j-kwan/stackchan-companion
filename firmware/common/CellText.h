#pragma once
// =============================================================================
// CellText.h — StackChan-Companion (shared by the guest bins)
// =============================================================================
// DEFERRED TEXT, DRAWN FROM ONE CALL SITE. This is the A2.22 discipline made
// into a thing instead of retyped per view.
//
// WHY IT EXISTS AT ALL. GCC 8.4 Xtensa is entitled to delete the SECOND of two
// similar draw calls in the same function body, and it does — the lesson the X
// of the Dead expression cost this project on 2026-07-25. The workaround is to
// have exactly ONE `drawString` per body and route every string through it. The
// space bin had written that workaround out by hand SIX times: six `struct
// Cell`, six `nc` counters, six `put` lambdas, six flush loops, each with its
// own array size and its own text width, and one of them silently different.
// Six copies of a compiler workaround is six chances to get the workaround
// wrong, which is precisely the "assumed twins" A2.23 forbids.
//
// So the single call site lives HERE, once, `noinline` so the compiler cannot
// duplicate it back into the callers and reintroduce the very shape the class
// exists to avoid.
//
// STATIC STORAGE, NOT THE STACK. Each hand-written copy put its array on
// loopTask's stack — 8 KB total, and one of the tables was 47 entries. One
// shared instance costs its size once, and only one view draws at a time
// (everything here is called from loop()).
//
// TRUNCATION IS NAMED. A table that quietly stops accepting cells draws a
// screen that is missing a line with no sign that anything was dropped — the
// failure mode this project keeps calling out. `overflowed()` reports it and
// the flush says so once on the serial console.
// =============================================================================

#include <Arduino.h>
#include <M5Unified.h>

namespace sce {
namespace ui {

class CellText {
public:
    // The widest table in service is the passes list: SIX headers (when, rise,
    // az, max, set, profile) and six rows of seven columns = 48 exactly. The
    // first version said five headers and sized itself at 48, i.e. full to the
    // last slot with the arithmetic written down wrong — and the table it
    // replaced was 47, so it had been dropping the sixth row's last cell in
    // silence all along (review 08-05). 56 leaves a row of headroom, and the
    // count above is now the one you can check.
    static constexpr int  MAX_CELLS = 56;
    static constexpr size_t MAX_TXT = 40;

    // NO BACKGROUND. `setTextColor(fg)` and `setTextColor(fg, fg)` are the same
    // thing to LovyanGFX — a glyph whose fore and back agree is drawn without
    // filling its cell — so "transparent" needs no branch in the flush, only a
    // value outside the 16-bit colour range to mean "the caller said nothing".
    static constexpr uint32_t NO_BG = 0x01000000u;

    void reset() { _n = 0; _over = false; }

    // `size` defaults to 1: four of the six call sites never varied it, and
    // making them pass it would have been noise at every one of them.
    //
    // `datum` and `bg` came later, from the flight-radar METAR card, which had
    // its own hand-written copy of this class for exactly one reason: its cells
    // carry a text DATUM (its right column is anchored right, not computed) and
    // one of them paints a background. Carrying both here retires the copy
    // instead of leaving two tables that agree on everything but their extras.
    void put(int x, int y, uint16_t col, const char* s, uint8_t size = 1,
             textdatum_t datum = textdatum_t::top_left, uint32_t bg = NO_BG) {
        if (!s) return;
        if (_n >= MAX_CELLS) { _over = true; return; }
        Cell& c = _c[_n++];
        c.x = (int16_t)x; c.y = (int16_t)y;
        c.size = size; c.col = col;
        c.datum = (uint8_t)datum;
        c.bg = (bg == NO_BG) ? col : (uint16_t)bg;
        // THE OTHER TRUNCATION. The header above promises that a table which
        // stops accepting cells says so; a cell whose TEXT did not fit was
        // being cut by strlcpy without a word, which is the same failure one
        // level down. strlcpy returns the length it wanted, so the test costs
        // nothing that was not already computed.
        if (strlcpy(c.txt, s, sizeof(c.txt)) >= sizeof(c.txt)) _cut = true;
    }

    // Right-aligned BY ARITHMETIC. The built-in font is a fixed 6x8 cell, so
    // the width is computable — and doing it here means the callers stop each
    // computing `right - strlen(s) * 6 * size` slightly differently.
    //
    // Not the same thing as `top_right`, and both are kept: this one gives the
    // caller the left edge it may need for a rule or a chip beside the text,
    // while the datum leaves the placement to the renderer and survives a
    // proportional font. Use the datum unless the geometry is needed.
    void putR(int right, int y, uint16_t col, const char* s, uint8_t size = 1) {
        if (!s) return;
        put(right - (int)strlen(s) * 6 * size, y, col, s, size);
    }

    bool overflowed() const { return _over; }   // cells lost
    bool truncated() const  { return _cut; }    // a cell's text lost its tail
    int  count() const { return _n; }

    // THE ONE CALL SITE. `noinline` for two reasons and both matter: inlined
    // into six callers it would be six drawString sites again (A2.22), and the
    // gate that verifies the rule inside the binary needs a symbol to look at.
    __attribute__((noinline)) void flush(M5Canvas& canvas) {
        for (int i = 0; i < _n; i++) {
            canvas.setTextDatum((textdatum_t)_c[i].datum);
            canvas.setTextSize(_c[i].size);
            // ONE setTextColor, not a branch: `bg` already equals `col` when the
            // caller wanted none (see NO_BG), which is the transparent case.
            canvas.setTextColor(_c[i].col, _c[i].bg);
            canvas.drawString(_c[i].txt, _c[i].x, _c[i].y);
        }
        // LEFT AS IT WAS FOUND, both of them. A cell states its own size and
        // datum, so a view that flushes cannot leak either into whatever draws
        // next — and the alternative is every caller remembering to restore two
        // globals after a call whose whole purpose is that it draws for them.
        canvas.setTextSize(1);
        canvas.setTextDatum(textdatum_t::top_left);
        // TWO different truncations, said apart: one costs a whole cell, the
        // other the tail of a string, and they are fixed in different places
        // (MAX_CELLS against the caller's arithmetic, MAX_TXT against the
        // longest line a view composes). One line each, once per boot — a
        // report repeated every frame is a report nobody reads.
        static bool saidFull = false, saidCut = false;
        if (_over && !saidFull) {
            saidFull = true;
            Serial.printf("[ui] CellText plein (%d cellules) : un ecran a "
                          "perdu des cellules\n", MAX_CELLS);
        }
        if (_cut && !saidCut) {
            saidCut = true;
            Serial.printf("[ui] CellText : un texte depassait %d octets et a "
                          "ete coupe\n", (int)MAX_TXT);
        }
        _n = 0;
    }

private:
    struct Cell {
        int16_t  x = 0, y = 0;
        uint8_t  size = 1;
        uint8_t  datum = (uint8_t)textdatum_t::top_left;
        uint16_t col = 0;
        uint16_t bg = 0;      // == col means transparent, see NO_BG
        char     txt[MAX_TXT] = "";
    };
    Cell _c[MAX_CELLS];
    int  _n = 0;
    bool _over = false;   // the table filled up
    bool _cut = false;    // a string did not fit MAX_TXT
};

} // namespace ui
} // namespace sce
