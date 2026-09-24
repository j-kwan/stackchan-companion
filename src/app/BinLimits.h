#pragma once
// =============================================================================
// BinLimits.h — the ONE answer to "how many /bins/ can this robot see, and
// how long may a name be".
// =============================================================================
// Two catalogues read the same /bins/ directory: the touch launcher
// (app/Launcher.h) and the API name cache (app/WebApi.h). Until 08-04 each
// carried its own pair of limits — 24 entries / 39 characters against
// 48 / 49 — so a card with 30 bins, or a 42-character name, behaved
// differently per entry point: listed and launchable from the browser,
// silently absent from the on-screen menu. A user debugging that divergence
// chases SD faults; whoever raised one limit did not know the other existed.
// One header, one pair, both catalogues (review 08-04).
//
// BIN_NAME_MAX is bounded by WebApi's `_launchPath` ("/bins/" + name + NUL),
// static_assert'd at its point of use.

#include <cstddef>

namespace sce {

inline constexpr int         BINS_MAX      = 48;   // catalogue capacity
inline constexpr std::size_t BIN_NAME_MAX  = 50;   // 49 characters + NUL

}  // namespace sce
