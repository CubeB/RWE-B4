# ADR-0006: The Community Patch is an opt-in, RWE-original ruleset

Issue #934 asks for a "Community Patch mode": a subset of the TA Community
Patch (`tdraw.dll`, <https://github.com/tanvanman/TADR>) that is relevant to
RWE. The patch is a community project with no byte-tactics counterpart, so this
cannot be parity work: it is a second, documented ruleset.

**Status**: accepted, first slice implemented.

## Considered Options

**Adopt TADR rules unconditionally.** Smallest surface, and mods that use
TADR's data keys would "just work". But several of the rules change game
outcomes (projectile pass-through, aircraft damage, repair rates, patrol
reclaim), and RWE's default game is meant to be the original's. A player who
had never heard of the patch would get its economy and combat rules without
asking.

**One toggle per feature.** Maximum control, but the point of a community
patch is a shared expectation: a mod says "use the community rules" and
everyone means the same set. Per-feature switches also invite
desynchronised multiplayer games where one player has a rule the others do not.
TADR itself ships per-mod build configurations rather than a menu of rules.

**A local preference only.** Some of the patch is local (snap radii, counters,
chat), but the parts that matter most change the simulation and must be shared.
Treating the whole patch as local would make the simulation depend on a
client-side setting.

**An opt-in ruleset carried by the game parameters (chosen).** The mode is one
flag, off by default, shared by every peer through the lobby's setup options,
saved with the game, and carried by replays. Local preferences remain local and
are not part of the flag.

## Consequences

- **`--community-patch`** sets `GameParameters::communityPatch`, copied into
  `GameSimulation::communityPatchMode`. The first adopted rules are the TADR
  weapon flags `nottoair`, `nottounderwater`, `notoverwater`, `notoverland` and
  `surfacefire`.
- **Network and recorded games.** The lobby carries the flag as the
  `community` setup option; the host toggles it with `/community` and every
  guest applies it before launch. Saves carry it; replays carry it from header
  version 8 (version 7 loads as the mode off); a TA demo is refused because the
  original's format cannot carry it. The mode is not in the simulation hash,
  because adding a field would change every existing hash; the launch parameter
  is the invariant. A direct command-line network launch must pass the flag on
  every peer.
- **No parity claim.** TADR is not Total Annihilation: each adopted feature
  cites TADR's written rule and records RWE's departures. The mode never
  appears in the original-game divergence register as "matching TA"; where it
  differs from TA it is a separate ruleset, described in
  `docs/COMMUNITY-PATCH-MODE.md`.
- **The subset is curated.** UI-only TADR features remain local preferences or
  are not ported at all (megamap, whiteboard, TA-Hook internals); engine
  patches for `TotalA.exe` do not apply to RWE; features already served by
  RWE's own design (music, replays, lobby, drop handling) are not duplicated.
