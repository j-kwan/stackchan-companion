#pragma once
// =============================================================================
// SdRoot.h — the config root path, and its one-time move to the new name
// =============================================================================
// The project renamed itself stackchan-eyes -> stackchan-companion (2026-09);
// the SD config directory follows, because every doc and every fresh card
// should read ONE name, not "well, it depends when you flashed". A FAT
// rename of a directory only rewrites its OWN entry in the parent folder —
// the tree underneath (files, the `personalities/` subdirectory) never
// moves — so the whole migration is one filesystem call, done before
// anything opens a path under either name.
//
// WHO CALLS THIS, AND WHY EVERY READER CARRIES IT RATHER THAN JUST THE
// COMPANION. The non-fire guests are launched BY the companion, which has
// by then already migrated — but the *-fire guests (flight-radar-fire,
// space-fire, led-fluid-fire) run standalone on a board that never runs a
// companion at all (`docs/guests/FLIGHT-RADAR.md` §Seconde carte). A card
// that started life on a Fire board would never be migrated if this lived
// in the companion alone. So every firmware that mounts the card calls
// `sce::migrateSdRoot()` once, right after `SD.begin()` succeeds and before
// any config path is opened — idempotent, a no-op past the first call on a
// given card.
//
// shouldMigrate() is pulled out pure and testable (test_sdroot) for the
// same reason the personality dials' clamp math lives in Personalities.h
// and not inline in Brain.h: the three-way decision — nothing to do on a
// fresh card, nothing to do once already migrated, move it otherwise — is
// exactly the kind of branch that is easy to get backwards (migrate INTO an
// already-populated new directory, silently losing the old one) and cheap
// to pin down once with a real test.
//
// WHAT THIS DOES NOT REACH. The rename moves the DIRECTORY ENTRY only — text
// sitting inside a file under it is untouched. A personality yaml SAVED by
// the console before this update carries its `rules:` field as a literal
// absolute path (`/stackchan-eyes/rules.<name>.txt`); migrating the
// directory does not rewrite that string, so the file loads fine but the
// path it names does not exist until the personality is re-saved once from
// the Characters tab. `RuleStore::load()` already falls back loudly on any
// missing rules file — this is that same, already-tested path, reached a
// new way, not a new failure mode.
// =============================================================================

namespace sce {

inline constexpr const char* SD_ROOT     = "/stackchan-companion";
inline constexpr const char* SD_ROOT_OLD = "/stackchan-eyes";

// Pure: no filesystem access, so it is what test_sdroot actually calls.
//   newExists=false, oldExists=false -> false (fresh card, nothing to move)
//   newExists=true,  *               -> false (already migrated — the old
//                                       path, if it somehow still exists, is
//                                       left alone rather than guessed about)
//   newExists=false, oldExists=true  -> true  (the one real case)
inline constexpr bool shouldMigrate(bool newExists, bool oldExists) {
    return !newExists && oldExists;
}

} // namespace sce

#if defined(ARDUINO)
#include <SD.h>

namespace sce {

inline void migrateSdRoot() {
    if (shouldMigrate(SD.exists(SD_ROOT), SD.exists(SD_ROOT_OLD)))
        SD.rename(SD_ROOT_OLD, SD_ROOT);
}

} // namespace sce
#endif
