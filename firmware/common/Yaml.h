#pragma once
// =============================================================================
// Yaml.h — the project's ONE YAML line decoder
// =============================================================================
// Rule 17 / A2.23 says there is a single YAML parser and that four divergent
// copies already cost two outages (a quoted SSID on 07-25, a quoted ADS-B
// source on 07-29). It also had to concede a pair of "assumed twins":
// `SceGuest::yamlForEach` and `SdConfig::load`, because the companion must not
// depend on `src/guest/` — and those twins had ALREADY diverged once (07-29).
//
// This header ends the concession. `firmware/common/` is a place BOTH sides
// already include (I18n.h, SunClock.h, PsJson.h), so the decision "what does
// this line mean" lives here, once, and is NATIVELY TESTED. What stays
// duplicated is only the bounded READ loop — `File`, `readBytesUntil`, the
// over-long-line discard — which is I/O, not interpretation, and which is
// where the twins never disagreed.
//
// PURE by construction: `char*` in, views out. No Arduino String, no File, no
// allocation. That is what makes it testable off-target, and the two outages
// above were both interpretation bugs, i.e. exactly what a test catches.
//
// The decoder MUTATES its buffer (it writes NUL terminators) and returns
// pointers into it. Nothing is copied, so no size limit is invented here: the
// caller's buffer already bounds the line.
// =============================================================================

// No include: the block below uses only `char`, `bool` and pointers.
// It carried `#include <stddef.h>` until 08-02, for a `size_t` it does
// not use — and the vendored copy did not carry it, which is how the
// asymmetry was found.
// >>> VENDORABLE BEGIN Yaml <<<
// Everything between these markers is copied verbatim into
// `src/guest/SceGuest.h` (which must stay copyable ALONE into a third-party
// project) and compared by `scripts/gates/check-vendored.py`. The markers are on
// BOTH sides on purpose: comparing "from the first namespace to EOF" left
// everything above it outside the comparison, so an include added here was
// never carried across and the gate still printed ok (review 08-02).
// Anything this block needs from outside must be listed as NEEDS here;
// check-vendored.py then requires the guest to satisfy it. This block
// needs nothing.
namespace sce {
namespace yaml {

struct Line {
    bool        ok;        // false = blank line, full-line comment, or no ':'
    bool        indented;  // leading space/tab BEFORE trimming — this is what
                           // tells a `section:` header from a key, so it is
                           // read first and never after a trim
    const char* key;       // trimmed, NUL-terminated, points into the buffer
    const char* val;       // scalar-decoded, NUL-terminated, same
};

namespace detail {

inline bool isSpace(char c) { return c == ' ' || c == '\t'; }

// Right-trims IN PLACE from `end` (one past the last char) back towards `s`.
inline void rtrim(char* s, char* end) {
    while (end > s && isSpace(end[-1])) end--;
    *end = '\0';
}

}  // namespace detail

// Decodes the VALUE side of `key: value`, in place.
//   - a QUOTED value keeps everything up to the closing quote: `pass: "a#b "`
//     is the 4 characters `a#b ` and `ssid: ""` is the EMPTY string, not two
//     literal quote characters. That is the round trip `save()` relies on, and
//     the two outages were both a parser that cut at '#' before looking at the
//     quotes;
//   - inside DOUBLE quotes, `\"` and `\\` are the characters `"` and `\`.
//     Single quotes take no escapes, which is YAML's own rule. This exists for
//     a concrete reason: a WPA passphrase is any 8..63 PRINTABLE ASCII
//     characters, `"` and `\` included, so without escapes those two are
//     unrepresentable — and dropping them silently gives a robot that cannot
//     join the network and says nothing about why;
//   - an UNQUOTED value is cut at the first '#' and then trimmed, which is what
//     makes `poll_s: 30   # seconds` read as 30.
// An unterminated quote yields everything after the opening one — degrading to
// "as much as we can read" rather than dropping the line.
inline char* scalar(char* v) {
    while (detail::isSpace(*v)) v++;
    char* end = v;
    while (*end) end++;
    detail::rtrim(v, end);
    if (*v == '"' || *v == '\'') {
        const char q = *v;
        // Unescaped IN PLACE: the writer only ever trails the reader, so the
        // result fits in the space the escapes occupied.
        char* r = v + 1;
        char* w = v + 1;
        while (*r && *r != q) {
            if (q == '"' && *r == '\\' && r[1]) r++;
            *w++ = *r++;
        }
        *w = '\0';
        return v + 1;
    }
    for (char* p = v; *p; p++) {
        if (*p == '#') { detail::rtrim(v, p); break; }
    }
    return v;
}

// Decodes ONE line. `buf` is modified.
inline Line decodeLine(char* buf) {
    Line out{ false, false, "", "" };
    if (!buf) return out;
    // Strip EVERY '\r', not just a trailing one: a file written on Windows and
    // edited elsewhere can carry them mid-line, and a stray '\r' inside a value
    // travels all the way to a WiFi password.
    {
        char* w = buf;
        for (char* r = buf; *r; r++) if (*r != '\r') *w++ = *r;
        *w = '\0';
    }
    out.indented = detail::isSpace(buf[0]);
    char* t = buf;
    while (detail::isSpace(*t)) t++;
    if (*t == '\0' || *t == '#') return out;      // blank or comment
    char* colon = t;
    while (*colon && *colon != ':') colon++;
    if (*colon != ':') return out;                // not a key/value line
    char* v = colon + 1;
    detail::rtrim(t, colon);                      // NUL-terminates the key
    out.key = t;
    out.val = scalar(v);
    out.ok  = true;
    return out;
}

}  // namespace yaml
}  // namespace sce
// >>> VENDORABLE END Yaml <<<
