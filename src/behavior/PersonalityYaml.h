#pragma once
// =============================================================================
// PersonalityYaml.h — StackChan-Companion (behavior) — PURE, tested natively
// =============================================================================
// The INTERPRETATION of a personality file, with no SD and no Arduino in it:
// hand it decoded lines, it fills a `Personality`. The file I/O (open, bounded
// read, discard the tail of an over-long line) lives in `app/PersonalityStore.h`
// with the other three readers — that split is A2.23's, and it is what lets the
// part that can be WRONG be tested on a PC.
//
// The format, one nesting level, read by the one YAML parser:
//
//     name: haro                       # id — the file's own name wins if absent
//     rules: /stackchan-companion/rules.haro.txt
//     color: 0x22FF66                  # 0 / absent = keep the emotion palette
//     theme: gundam                    # COMPILED skin, picked by name
//     roulette:
//       enabled: 1
//       min_ms: 8000
//       max_ms: 20000
//     weights:                         # any emotion left out is never drawn
//       Normal: 1.0
//       Happy: 0.6
//
// AN OVERLAY, NOT A DECLARATION. Every key is optional and an absent one keeps
// the compiled default, so a half-written file degrades to "mostly the default"
// instead of to a character with holes in it. The one exception is `weights:`
// — declaring the section at all REPLACES the table rather than merging into
// it, because a merge could never express "this character does not do Sad": you
// would be able to add an emotion but never remove one.
//
// A KEY IT DOES NOT UNDERSTAND IS REFUSED AND NAMED, never skipped. A typo in a
// file that silently does nothing is the failure this project keeps paying for;
// `problems()` carries what was wrong so the caller can put it on the serial
// line and in the console.
// =============================================================================

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "Personalities.h"
#include "../../firmware/common/Yaml.h"

namespace sce {

class PersonalityYaml {
public:
    // Reset before feeding the lines of a file.
    void begin(Personality& target) {
        _p        = &target;
        _section  = Section::None;
        _nProblem = 0;
        _sawWeights = false;
        _problem[0] = '\0';
    }

    // Feed ONE already-decoded line. Returns false if the line was refused —
    // the caller keeps going (one bad line is not a bad file) but the reason is
    // recorded.
    bool feed(const sce::yaml::Line& ln) {
        if (!_p || !ln.ok) return true;          // blank/comment: nothing to do

        // A SECTION HEADER is a non-indented key with no value. Read from
        // `indented` rather than guessed from the name, which is why the shared
        // parser exposes that flag at all.
        if (!ln.indented && (!ln.val || !ln.val[0])) {
            if      (!strcmp(ln.key, "roulette")) _section = Section::Roulette;
            else if (!strcmp(ln.key, "weights"))  { _section = Section::Weights;
                                                    beginWeights(); }
            else { _section = Section::None; return refuse(ln.key); }
            return true;
        }
        if (!ln.indented) _section = Section::None;   // back to the top level

        switch (_section) {
            case Section::Roulette: return feedRoulette(ln);
            case Section::Weights:  return feedWeight(ln);
            case Section::None:     return feedTop(ln);
        }
        return true;
    }

    int         problems() const { return _nProblem; }
    const char* firstProblem() const { return _problem; }

    // Names of the compiled console skins. ONE table, used to parse the file
    // and to render the console's picker, so a theme cannot exist in one and
    // not the other.
    static const char* themeName(ConsoleTheme t) {
        switch (t) {
            case ConsoleTheme::Gundam: return "gundam";
            default:                   return "default";
        }
    }
    static bool themeFromName(const char* s, ConsoleTheme& out) {
        if (!strcmp(s, "default")) { out = ConsoleTheme::Default; return true; }
        if (!strcmp(s, "gundam"))  { out = ConsoleTheme::Gundam;  return true; }
        return false;
    }

private:
    enum class Section : uint8_t { None, Roulette, Weights };

    bool feedTop(const sce::yaml::Line& ln) {
        if (!strcmp(ln.key, "name")) {
            if (!ln.val[0]) return refuse("name");      // a nameless slot is free
            strncpy(_p->name, ln.val, PERSO_NAME_MAX - 1);
            _p->name[PERSO_NAME_MAX - 1] = '\0';
            return true;
        }
        if (!strcmp(ln.key, "rules")) {
            strncpy(_p->rulesFile, ln.val, PERSO_PATH_MAX - 1);
            _p->rulesFile[PERSO_PATH_MAX - 1] = '\0';
            return true;
        }
        if (!strcmp(ln.key, "color")) {
            // `strtoul` with base 0 takes 0x22FF66 AND 2293094 — people write
            // colours in hex and config files are edited by hand.
            _p->eyeRgb = (uint32_t)strtoul(ln.val, nullptr, 0) & 0xFFFFFFu;
            return true;
        }
        if (!strcmp(ln.key, "theme")) {
            ConsoleTheme t;
            if (!themeFromName(ln.val, t)) return refuse("theme");
            _p->theme = t;
            return true;
        }
        // `index` is accepted and IGNORED: the slot a personality occupies is
        // decided by the loader (directory order), not by the file. A file that
        // could claim an index could also claim someone else's, and two files
        // claiming the same one has no good answer.
        if (!strcmp(ln.key, "index")) return true;
        // Parsed PERMISSIVELY here, like `min_ms`/`max_ms` above them — the
        // safety floor (a scale near zero making every transition instant, or
        // one large enough to strobe) is enforced where the value is APPLIED
        // (`Brain::setTransitionScale`/`setPitchBiasScale`), not where it is
        // read. A file that says something extreme is not a parse error; the
        // robot simply will not carry it out literally.
        if (!strcmp(ln.key, "transition_scale")) {
            _p->transitionScale = (float)atof(ln.val); return true;
        }
        if (!strcmp(ln.key, "pitch_bias_scale")) {
            _p->pitchBiasScale = (float)atof(ln.val); return true;
        }
        return refuse(ln.key);
    }

    bool feedRoulette(const sce::yaml::Line& ln) {
        if (!strcmp(ln.key, "enabled")) { _p->roulette = atoi(ln.val) != 0; return true; }
        if (!strcmp(ln.key, "min_ms"))  { _p->minMs = (uint32_t)strtoul(ln.val, nullptr, 10); return true; }
        if (!strcmp(ln.key, "max_ms"))  { _p->maxMs = (uint32_t)strtoul(ln.val, nullptr, 10); return true; }
        return refuse(ln.key);
    }

    void beginWeights() {
        // Declaring the section replaces the table — see the header.
        if (!_sawWeights) { _p->clearWeights(); _sawWeights = true; }
    }

    bool feedWeight(const sce::yaml::Line& ln) {
        const eEmotions e = emotionFromName(ln.key);
        if (e >= EMOTIONS_COUNT) return refuse(ln.key);
        _p->setWeight(e, (float)atof(ln.val));
        return true;
    }

    bool refuse(const char* what) {
        if (!_nProblem) {                       // keep the FIRST, not the last:
            strncpy(_problem, what, sizeof(_problem) - 1);   // it is the one that
            _problem[sizeof(_problem) - 1] = '\0';           // usually explains
        }                                                    // the others
        _nProblem++;
        return false;
    }

    Personality* _p       = nullptr;
    Section      _section = Section::None;
    int          _nProblem = 0;
    bool         _sawWeights = false;
    char         _problem[24] = { 0 };
};

// ---- WRITING ONE BACK ------------------------------------------------------
// Emitted through a caller-supplied sink so this stays pure: `PersonalityStore`
// hands it a File, a test hands it a buffer. It writes EVERY field rather than
// only the differences — a file the console has saved is the character's whole
// state, and "which of these did I change" is not a question a user should have
// to answer to understand their own file.
template <typename Emit>
inline void writePersonality(const Personality& p, Emit emit) {
    char line[96];
    snprintf(line, sizeof(line), "name: %s\n", p.name);                 emit(line);
    snprintf(line, sizeof(line), "rules: %s\n", p.rulesFile);           emit(line);
    snprintf(line, sizeof(line), "color: 0x%06lX\n",
             (unsigned long)(p.eyeRgb & 0xFFFFFFu));                    emit(line);
    snprintf(line, sizeof(line), "theme: %s\n",
             PersonalityYaml::themeName(p.theme));                      emit(line);
    snprintf(line, sizeof(line), "transition_scale: %.2f\n",
             p.transitionScale);                                        emit(line);
    snprintf(line, sizeof(line), "pitch_bias_scale: %.2f\n",
             p.pitchBiasScale);                                         emit(line);
    emit("roulette:\n");
    snprintf(line, sizeof(line), "  enabled: %d\n", p.roulette ? 1 : 0); emit(line);
    snprintf(line, sizeof(line), "  min_ms: %lu\n", (unsigned long)p.minMs); emit(line);
    snprintf(line, sizeof(line), "  max_ms: %lu\n", (unsigned long)p.maxMs); emit(line);
    // The section is written ONLY when the character declares one. Writing an
    // empty `weights:` would mean "draws nothing" on the next read — a file that
    // saves a working character and reloads as a catatonic one.
    if (p.hasWeights) {
        emit("weights:\n");
        for (int i = 0; i < EMOTIONS_COUNT; i++) {
            if (p.weights[i] <= 0.0f) continue;
            snprintf(line, sizeof(line), "  %s: %.2f\n",
                     emotionName((eEmotions)i), p.weights[i]);
            emit(line);
        }
    }
}

} // namespace sce
