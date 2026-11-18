#pragma once
// =============================================================================
// PsJson.h — ArduinoJson allocator backed by PSRAM, SHARED by the guest bins
// =============================================================================
// Both guest bins parse HTTP responses of several hundred kilobytes (Home
// Assistant entity catalogue, ADS-B tile from a hub). A default
// `JsonDocument` takes that volume from the INTERNAL heap — the 320 KB of
// SRAM also shared by WiFi, TLS, AsyncTCP and the DMA buffers. That is how we
// got a dead network stack and a radar showing zero aircraft.
//
// PSRAM (8 MB) is made for this. This allocator sends the document there.
//
// WHY NOT IN SceGuest.h: that header must stay copy-pasteable as-is into a
// third-party project, with M5Unified and SD-Updater as its only
// dependencies. Pulling ArduinoJson in would force that library on every
// guest, even one that speaks no JSON. PsJson.h is therefore a SEPARATE and
// OPTIONAL utility, to be included only when needed.
//
// Usage:
//     #include "../common/PsJson.h"
//     JsonDocument doc(&sce::psAlloc);
// =============================================================================

#include <Arduino.h>
#include <ArduinoJson.h>

namespace sce {

// PSRAM allocator with a BOUNDED internal-heap fallback.
//
// The old version did `ps_malloc(n)` then, on failure, `malloc(n)` — with no
// limit and no trace. So it took from the internal heap exactly when PSRAM
// ran out, i.e. at the worst possible moment, and in order to preserve a heap
// it was precisely meant to spare. The symptom was not a parse error, but a
// network stack dead further down the line, with no visible link to the cause.
//
// The discipline adopted, at ALLOCATION time (`allocate`, the only place
// where a refusal is harmless — see `reallocate` for why the same does not
// hold for resizing):
//   - a SMALL block may still come from the internal heap: that is
//     ArduinoJson's own bookkeeping, a few hundred bytes, harmless;
//   - a BIG block is REFUSED: `deserializeJson` returns NoMemory, the caller
//     logs it and will retry. A failed parse is recoverable; an exhausted
//     internal heap is not;
//   - the total of the fallbacks is capped, otherwise fifty small blocks
//     rebuild the big one we had just refused;
//   - every fallback leaves a TRACE. "Silently" was the real defect.
struct PsAllocator : ArduinoJson::Allocator {
    // A block above this threshold will never be taken from the internal heap.
    static constexpr size_t FALLBACK_MAX   = 4096;
    // TOTAL budget conceded to the internal heap, all blocks taken together.
    static constexpr size_t FALLBACK_BUDGET = 16384;

    size_t fellBack = 0;        // bytes currently taken from the internal heap
    bool   starved  = false;    // at least one refusal since the last reset

    void* allocate(size_t n) override {
        if (void* p = ps_malloc(n)) return p;
        if (n > FALLBACK_MAX || fellBack + n > FALLBACK_BUDGET) {
            note(n);
            return nullptr;
        }
        void* p = malloc(n);
        if (p) fellBack += n;
        else   note(n);
        return p;
    }

    void deallocate(void* p) override { free(p); }

    // Resizing — it NEVER refuses, and that is deliberate.
    //
    // A `reallocate` acts on a block that ALREADY exists, and ArduinoJson
    // takes for granted that a SHRINKING realloc always succeeds:
    //     StringBuffer.hpp  node = resources_->resizeString(node_, size_);
    //                       ARDUINOJSON_ASSERT(node != nullptr);
    //                       // realloc to smaller can't fail
    // But `ARDUINOJSON_ASSERT` vanishes in a release build. And upstream,
    // `StringNode::resize` FREES the original block when the allocator
    // returns nullptr. Refusing here therefore means returning nullptr AFTER
    // having caused the string to be freed: the code that follows
    // dereferences a null pointer. The old version refused as soon as
    // `n > FALLBACK_MAX` — so it crashed in exactly the case it claimed to
    // degrade gracefully: PSRAM full + a long string to validate (found in
    // the 2026-07-29 review).
    //
    // The cap of rule 18 keeps its full meaning: blocks are BORN in
    // `allocate()`, the only place where a refusal is safe (ArduinoJson then
    // returns NoMemory and the caller retries). What is taken here is
    // COUNTED, so the following `allocate()` calls are throttled by the same
    // amount, and every fallback leaves a trace.
    void* reallocate(void* p, size_t n) override {
        // `ps_realloc` finds the heap that owns the pointer: it can therefore
        // move a block born on the internal heap back into PSRAM.
        if (void* q = ps_realloc(p, n)) return q;
        void* q = realloc(p, n);
        if (q) {
            // We count `n` in full: the Allocator API does not give the old
            // size. Overestimating can only TIGHTEN the budget of subsequent
            // allocations, which is the intended direction.
            fellBack += n;
            if (fellBack > FALLBACK_BUDGET) note(n, false);   // trace, no refusal
        } else {
            note(n);
        }
        return q;
    }

    // To be called before every parse: the budget is per document, not for
    // the lifetime of the program.
    void reset() { fellBack = 0; starved = false; }

private:
    // `refused` tells the two outcomes apart: a REFUSED block makes the parse
    // fail (the caller will retry), a block CONCEDED beyond the budget is
    // only a warning — refusing it would crash (see reallocate).
    void note(size_t n, bool refused = true) {
        if (!starved)                     // a single line per parse
            Serial.printf("[psjson] PSRAM saturee, bloc de %u o %s "
                          "(psram=%u heap=%u repli=%u o)\n",
                          (unsigned)n,
                          refused ? "refuse - analyse abandonnee"
                                  : "CONCEDE au-dela du budget de repli",
                          (unsigned)ESP.getFreePsram(),
                          (unsigned)ESP.getFreeHeap(), (unsigned)fellBack);
        starved = true;
    }
};

// A single instance: parses are sequential (all of them on netTask, the only
// task that opens sockets in either bin). Two documents alive at the same
// time would share the fallback budget, which is the intended behaviour.
static PsAllocator psAlloc;

// A Stream that swallows a response body into PSRAM. It exists for ONE
// reason, and it is not a nicety: HTTPClient only DECHUNKS through
// writeToStream(). getStream() hands back the raw socket — chunk-size lines
// and all — so ArduinoJson reading it sees "1a2b\r\n{" and gives up with
// IncompleteInput. Every service that sends Content-Length parsed fine, which
// is exactly why this stayed invisible until autorouter (Apache + mod_wsgi,
// no Content-Length) became the first chunked source in the bin: the NOTAM
// view failed with a bare "-1001" while the same request from a PC worked.
// BOUNDED (rule 18): PSRAM, doubling growth, and a hard ceiling above which
// the body is refused rather than swallowed — a truncated parse is
// recoverable, an exhausted allocator is not.
class PsSink : public Stream {
public:
    ~PsSink() { if (_buf) free(_buf); }
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* d, size_t n) override {
        if (_over) return n;                     // keep draining, keep nothing
        if (_len + n + 1 > _cap) {
            size_t want = (_len + n + 1) * 2;
            if (want > MAXB) want = MAXB;
            if (_len + n + 1 > want) { _over = true; return n; }
            char* q = (char*)ps_realloc(_buf, want);
            if (!q) { _over = true; return n; }
            _buf = q; _cap = want;
        }
        memcpy(_buf + _len, d, n);
        _len += n; _buf[_len] = '\0';
        return n;
    }
    int  available() override { return 0; }
    int  read()      override { return -1; }
    int  peek()      override { return -1; }
    void flush()     override {}
    const char* data() const { return _buf ? _buf : ""; }
    bool overflowed() const  { return _over; }
private:
    static constexpr size_t MAXB = 96 * 1024;
    char*  _buf = nullptr;
    size_t _cap = 0, _len = 0;
    bool   _over = false;
};

// MOVED HERE ON 08-04, from firmware/flight-radar/main.cpp, when the space bin
// hit the identical failure against Launch Library: chunked response, raw
// stream read, ArduinoJson parsing the chunk-size line "13ea" as the number 13
// and returning Ok on an EMPTY document — a launch list that stayed blank with
// no error anywhere. Two bins meeting the same trap is exactly the point at
// which a solution stops belonging to one of them (A2.23). `firmware/common/`
// is already included by both.


} // namespace sce
