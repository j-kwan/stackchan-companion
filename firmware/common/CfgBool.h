#pragma once
// =============================================================================
// CfgBool.h — what counts as TRUE in a configuration value
// =============================================================================
// Three bins each had their own answer and one of them was wrong. `space` used
// a FIRST-CHARACTER test — true if the value began with '1', 't', 'o' or 'y' —
// so `off` read as TRUE, because it starts with the same letter as `on`. A word
// that means false in every language on earth turned an option on, and nothing
// said so: the option simply behaved as if it had never been set.
//
// The other two compared exactly, against `"1" | "on" | "true"`, and agreed
// with each other. That is rule 17's shape exactly: not a copy that drifted,
// but three independent answers to one question, differing where it mattered.
//
// PURE by construction — `char*` in, `bool` out, no Arduino and no `String`.
// That is what lets the same function serve a yaml scalar (a raw pointer into
// the read buffer) and a form field (`String::c_str()`), and what lets it be
// tested off-target: see `test/test_cfgbool`.
// =============================================================================

namespace sce {

// TRUE for "1", "on", "true", "yes", "y", "t" — case-insensitive and EXACT.
// Everything else is FALSE, including an empty value and every misspelling.
//
// EXACT is the whole point. A prefix or first-letter test looks friendlier and
// is how `off` became true; and a "starts with a true-ish letter" rule cannot
// be extended safely, because the false words share initials with the true
// ones (`off`/`on`, `no`/`n`… ). A flag that guesses is a flag that surprises,
// and a configuration flag surprises silently.
//
// Leading blanks are skipped because a form can send padding; nothing else is
// trimmed, since the yaml decoder already trims what it hands over.
inline bool webBool(const char* v) {
    if (!v) return false;
    while (*v == ' ' || *v == '\t') v++;

    // Case-insensitive compare, written here rather than pulled from
    // <strings.h>: `strcasecmp` is not portable to every toolchain this header
    // is compiled by, and the whole function is shorter than the include.
    struct Eq {
        static bool of(const char* a, const char* b) {
            for (;; a++, b++) {
                char ca = *a;
                if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
                if (ca != *b) return false;
                if (!ca)      return true;
            }
        }
    };
    return Eq::of(v, "1")   || Eq::of(v, "on") || Eq::of(v, "true") ||
           Eq::of(v, "yes") || Eq::of(v, "y")  || Eq::of(v, "t");
}

}  // namespace sce
