#pragma once

#include <string.h>
#include <time.h>
#include <math.h>
#include <stdio.h>
// Bilingual UI, EN/FR — ONE mechanism for the companion, the guest bins and
// SceGuest. English is the default and the fallback: an absent or unreadable
// `lang` key means English, so a card that never heard of this setting behaves
// like an English build.
//
// WHY A PAIR AND NOT A KEY TABLE. The obvious design is `t("settings.title")`
// against two tables. It is also the design this project has already paid for
// once, in another guise: four divergent copies of a YAML parser cost two
// outages (A2.23), and a key table has the same failure mode — a key added to
// one side only, a key renamed on one side only, a key nobody removes when the
// string dies. Nothing DENOUNCES the drift, because a missing key is a runtime
// miss on a screen nobody is looking at.
//
// Here the two forms ARE the call:
//
//     canvas.drawString(sce::T("Settings", "Reglages"), x, y);
//
// You cannot add a string in one language only — that is a compile error, not
// a silent miss. You cannot rename a key wrong, there is no key. You cannot
// leave a dead translation behind, it dies with the line that used it. And the
// English is written first, where a reader of the code sees it.
//
// The cost is that the two forms live at the call site rather than in a
// translator-friendly file. For a firmware with a few hundred strings and no
// translator workflow, that trade is the right way round.
//
// ACCENTS. Font0 (the 6x8 bitmap face used for most of the on-screen UI)
// renders UTF-8 as "??": French strings drawn in it must be written WITHOUT
// accents ("Reglages", not "Réglages"). efontJA_12 and the HTML console are
// Unicode and take proper French. That is a property of the FACE, not of this
// header — see fr::cityClean in the flight-radar bin, which strips accents
// from wire data for the same reason.

#include <stdint.h>

// >>> VENDORABLE BEGIN I18n <<<
// NEEDS: <stdint.h>
namespace sce {

enum class Lang : uint8_t { EN = 0, FR = 1 };

// Not `static`: this header is included by several translation units and they
// must share ONE language, not one each.
inline Lang g_lang = Lang::EN;

// Accepts what a config file actually contains: "fr", "FR", "fr-FR",
// "francais", "français". Anything else — including an empty or missing
// value — is English, deliberately: the fallback must never depend on
// recognising the string.
inline void setLang(const char* s) {
    g_lang = (s && (s[0] == 'f' || s[0] == 'F')) ? Lang::FR : Lang::EN;
}

inline const char* langCode() { return g_lang == Lang::FR ? "fr" : "en"; }

// The whole API.
inline const char* T(const char* en, const char* fr) {
    return g_lang == Lang::FR ? fr : en;
}


// THE ROBOT'S LANGUAGE, read from the COMPANION's own top-level `lang:` key in
// /stackchan-companion/config.yaml. It is deliberately NOT a per-bin setting: a
// copy in each guest's yaml would be a second thing to keep in step, and two
// language settings that can disagree are not a setting — one robot, one
// language. flight-radar has read it this way since the beginning; extracted
// here on 08-04 when the space bin was found keeping its own copy.
//
// `sectioned = false` ON PURPOSE: in sectioned mode a NON-indented line is
// read as a section header, so a top-level key never reaches the callback.
// Absent or unreadable card: English, which is setLang's own fallback.
//
// Declared as a template on the guest type so this header keeps its ONE
// dependency rule (no include of SceGuest.h, which includes it in turn).
template <typename Guest>
inline void loadCompanionLang() {
    Guest::yamlForEach(Guest::CONFIG_PATH, false,
                       [](void*, const char*, const char* k, const char* v) {
        if (!strcmp(k, "lang")) setLang(v);
    }, nullptr);
}

// ---- THE TIME ZONE, AND WHO GETS THE LAST WORD -----------------------------
// Reported by the user, 08-05: set the UTC offset, go to a guest bin, and the
// clock loses it. Each guest read `tz_offset_h` from its OWN yaml and the
// companion keeps its copy in `config.yaml`; nothing joined the two.
//
// THE OBVIOUS FIX IS THE WRONG ONE, and it was measured before shipping: on
// this robot the companion's stored value is 0.00 (nobody ever turned that
// dial) while both guest yamls say 4.0. "The companion always wins" would
// therefore have dragged two correctly configured bins to UTC — turning a
// missing setting into a broken one.
//
// So the rule is: THE GUEST'S OWN KEY WINS WHEN IT HAS ONE, and the companion
// fills the gap when it does not. A bin whose card never mentioned the offset
// inherits the robot's instead of silently sitting on UTC, and nothing that
// already works changes. `lang` can be companion-only because the guests
// deleted their key; the offset cannot, because theirs is populated.
//
// Returns true when the companion stated an offset; the caller decides.
template <typename Guest>
inline bool companionTzOffsetH(float& out) {
    struct Ctx { float v; bool found; } c{ 0.0f, false };
    Guest::yamlForEach(Guest::CONFIG_PATH, false,
                       [](void* ctx, const char*, const char* k, const char* v) {
        if (!strcmp(k, "tz_offset_h")) {
            Ctx* c = (Ctx*)ctx;
            c->v = (float)atof(v);
            c->found = true;
        }
    }, &c);
    // Clamped here rather than at the call sites. Chatham is +12.75, Baker
    // Island -12; the +/-14 bound is the real one (Kiribati).
    if (c.found) {
        if (c.v < -14.0f) c.v = -14.0f;
        if (c.v >  14.0f) c.v =  14.0f;
        out = c.v;
    }
    return c.found;
}

// ---- TRUNCATION THAT SAYS SO ------------------------------------------------
// Copies `src` into `dst` and, when it did not fit, replaces the last kept
// glyph with a '.' so the cut is VISIBLE. That mark is a project discipline,
// not a detail: a silently truncated name reads as a complete one, and the
// screen then states something false with full confidence. It was written five
// times across the bins with three different marks and two with none at all
// (review 08-04) — this is the one place it lives now.
//
// `maxGlyphs` counts CHARACTERS, which for the 6 px Font0 is the usable pixel
// width divided by six. `dst` must hold maxGlyphs + 1 bytes.
inline void fitGlyphs(char* dst, size_t cap, const char* src, size_t maxGlyphs) {
    if (!dst || cap == 0) return;
    if (maxGlyphs > cap - 1) maxGlyphs = cap - 1;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n <= maxGlyphs) { memcpy(dst, src, n + 1); return; }
    if (maxGlyphs == 0) { dst[0] = '\0'; return; }
    memcpy(dst, src, maxGlyphs);
    dst[maxGlyphs - 1] = '.';            // the cut, made visible
    dst[maxGlyphs] = '\0';
}

// ---- LOCAL WALL CLOCK FROM A UTC INSTANT ------------------------------------
// One conversion, because there were four: a time_t shifted and taken modulo
// 86400, modular-minute arithmetic, the same thing again in Julian days, and
// libc's own TZ via configTime. The last is the odd one out and is why the two
// guest bins disagreed about what `tz_offset_h` means.
//
// THE POLICY, stated once: everything internal is UTC, and the offset is
// applied HERE, at display time, exactly once. Decimal hours cover every real
// zone (India 5.5, Nepal 5.75, Chatham 12.75).
inline void localHhmm(time_t utc, float offsetH, char* out, size_t n) {
    if (!out || n < 6) return;
    const time_t lt = utc + (time_t)lround(offsetH * 3600.0f);
    struct tm t;
    gmtime_r(&lt, &t);                    // gmtime on an ALREADY shifted instant
    snprintf(out, n, "%02d:%02d", t.tm_hour, t.tm_min);
}

inline void localDayHhmm(time_t utc, float offsetH, char* out, size_t n) {
    if (!out || n < 12) return;
    const time_t lt = utc + (time_t)lround(offsetH * 3600.0f);
    struct tm t;
    gmtime_r(&lt, &t);
    snprintf(out, n, "%02d/%02d %02d:%02d",
             t.tm_mday, t.tm_mon + 1, t.tm_hour, t.tm_min);
}

}  // namespace sce
// >>> VENDORABLE END I18n <<<
