#pragma once
// =============================================================================
// PersonalityStore.h — StackChan-Companion (app)
// =============================================================================
// The SD half of the personality layer: enumerate `/stackchan-companion/personalities/
// *.yaml`, overlay each onto the live table, and write one back when the console
// saves. The INTERPRETATION lives in `behavior/PersonalityYaml.h`, which is pure
// and natively tested; what is here is I/O and nothing else.
//
// That split is A2.23's, and it is the same one `SdConfig`/`SceGuest` make: the
// bounded READ (a stack buffer, `readBytesUntil`, the tail of an over-long line
// discarded) stays duplicated on purpose — it is I/O, not interpretation, and it
// is not where the twins ever disagreed.
//
// ORDER IS THE DIRECTORY'S, NOT THE FILE'S. A file does not choose its index:
// one that could claim a slot could claim someone else's, and two files claiming
// the same one has no good answer. Personality 0 is COMPILED and never loaded
// from a file, so the card can neither replace nor delete the fallback the rest
// of the design stands on.
//
// A CARD WITH NO `personalities/` DIRECTORY CHANGES NOTHING. That is the
// acceptance criterion of the whole feature: the compiled table is already the
// robot, and this class only ever overlays onto it.
// =============================================================================

#include <Arduino.h>
#include <SD.h>
#include "../behavior/PersonalityYaml.h"
#include "../behavior/Personalities.h"

namespace sce {

class PersonalityStore {
public:
    static constexpr const char* DIR = "/stackchan-companion/personalities";

    // Longest line we will read whole. A `weights:` entry is ~20 characters and
    // a rules path ~40; 96 leaves room for a comment without letting one line
    // eat the stack.
    static constexpr int MAX_LINE = 96;

    // Loads every file in the directory over the live table. Returns how many
    // were applied, or -1 if there is no directory (which is NOT an error — see
    // the header). `problems` counts refused keys AND files refused outright
    // (a 9th character with the table already full) across all files, so the
    // caller can say so once.
    int loadAll(int& problems) {
        problems = 0;
        File dir = SD.open(DIR);
        if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return -1; }

        // BACK TO THE FIRMWARE'S OWN CHARACTERS FIRST, so loading is
        // IDEMPOTENT: a reload after a deletion cannot leave the deleted
        // character behind, and a reload after an edit cannot apply it twice.
        personalities::resetToBuiltins();

        int applied = 0;
        File f;
        while ((f = dir.openNextFile())) {
            String fname = f.name();
            if (!f.isDirectory() && fname.endsWith(".yaml")) {
                // THE FILE NAME IS THE ID, and it decides EDIT versus CREATE.
                // `haro.yaml` matches the compiled Haro and overlays ONTO it —
                // that is how the console saves a change to a character the
                // firmware ships with. The first version of this loop got it
                // wrong: it appended from slot 1 blindly, so the first card file
                // silently REPLACED Haro (seen on target through
                // /api/personalities, 09-14). A name nobody holds takes the next
                // free slot instead.
                const String id = idFrom(fname);
                int idx = personalities::indexOf(id.c_str());
                Personality* p = nullptr;
                const bool creating = (idx < 0);
                if (creating) {
                    idx = personalities::count();
                    p = personalities::slot(idx);
                    // TABLE FULL: skip THIS file, not the rest of the
                    // directory — a later file may be an EDIT to a
                    // personality already loaded, not a creation, and it
                    // must still get its turn (`p` stays null, so the
                    // parse below is skipped for this one file only). SAID
                    // OUT LOUD (A KEY IT DOES NOT UNDERSTAND IS REFUSED AND
                    // NAMED, never skipped — same rule, one level up):
                    // a card with a 9th character must not look like a
                    // card that loaded fine.
                    if (p) {
                        seedFrom(*p, id);               // CREATE: from defaults
                    } else {
                        problems++;
                        Serial.printf("[perso] '%s' ignoree : table pleine (%d)\n",
                                      id.c_str(), personalities::count());
                    }
                } else {
                    // EDIT: the existing values are the base, so a file saying
                    // only `color:` changes the colour and nothing else.
                    p = personalities::slot(idx);
                }
                int bad = 0;
                if (p) { if (parse(f, *p, bad)) applied++; problems += bad; }
            }
            f.close();
        }
        dir.close();
        return applied;
    }

    // Writes one personality to its own file. Returns false on any I/O failure
    // — reported, never assumed: a save that silently did nothing would let the
    // console show a character the card does not have.
    static bool save(const Personality& p) {
        if (!p.name[0]) return false;
        SD.mkdir(DIR);
        char path[96];
        pathFor(p.name, path, sizeof(path));
        File f = SD.open(path, FILE_WRITE);
        if (!f) return false;
        bool ok = true;
        writePersonality(p, [&](const char* s) {
            if (ok && f.print(s) == 0 && s[0]) ok = false;
        });
        f.close();
        return ok;
    }

    static bool remove(const char* name) {
        if (!name || !name[0]) return false;
        char path[96];
        pathFor(name, path, sizeof(path));
        return SD.exists(path) ? SD.remove(path) : true;
    }

    static void pathFor(const char* name, char* out, size_t cap) {
        snprintf(out, cap, "%s/%s.yaml", DIR, name);
    }

private:
    // The slot starts from the DEFAULT personality's values, not from whatever
    // the slot held before: a file is an overlay, and it must be an overlay onto
    // something known rather than onto the residue of a previous load.
    // The id a file carries: its base name. `File::name()` returns the bare
    // name on some cores and a full path on others, so the leading directory is
    // stripped either way rather than assumed absent.
    static String idFrom(const String& fname) {
        int dot = fname.lastIndexOf('.');
        String id = dot > 0 ? fname.substring(0, dot) : fname;
        int slash = id.lastIndexOf('/');
        return slash >= 0 ? id.substring(slash + 1) : id;
    }

    static void seedFrom(Personality& p, const String& id) {
        char rules[PERSO_PATH_MAX];
        snprintf(rules, sizeof(rules), "/stackchan-companion/rules.%s.txt", id.c_str());
        personalities::fill(p, id.c_str(), rules, 0u,
                            ConsoleTheme::Default, 6000, 12000);
    }

    static bool parse(File& f, Personality& p, int& problems) {
        PersonalityYaml y;
        y.begin(p);
        char line[MAX_LINE];
        while (f.available()) {
            int len = f.readBytesUntil('\n', line, sizeof(line) - 1);
            line[len] = '\0';
            // OVER-LONG LINE: throw the remainder away up to the next newline.
            // The fourth bounded-read loop of A2.23 had diverged by NOT doing
            // this, and the tail came back as a line of its own and parsed as a
            // second, invented entry. Same guard, same reason.
            if (len == (int)sizeof(line) - 1 && f.available() && f.peek() != '\n') {
                while (f.available() && f.read() != '\n') { }
                continue;
            }
            y.feed(sce::yaml::decodeLine(line));
        }
        problems = y.problems();
        if (problems && y.firstProblem()[0])
            Serial.printf("[perso] %s : cle refusee '%s' (%d au total)\n",
                          p.name, y.firstProblem(), problems);
        return p.name[0] != '\0';
    }
};

} // namespace sce
