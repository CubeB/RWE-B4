# ADR-0005: The original's computer player is the default, and RWE's own is an option

A skirmish's computer players used to be RWE's own design (`AiPlayerController`,
`docs/ai-architecture-proposal.md`); they are now the original's
(`ClassicAiController`, written from `TOTALA-EXE-COMPUTER-PLAYER.md`) unless a
seat, a saved game or `--ai-engine rwe` says otherwise. **The engine is chosen
per seat and carried by the personality**: "Original TA" is the classic engine,
and every other built-in and every `ai/*.txt` personality is RWE's. Both sit
behind one `AiController` interface, so the simulation neither knows nor cares
which one a seat has.

**Status**: accepted

## Considered Options

**Replace RWE's computer player outright.** Smallest code, and it throws away
a year of arena-tuned work and the only computer player that uses transports,
guards, fights with aircraft and recovers from losing its base. The original
cannot do any of those, and a player who wants a harder or a smarter opponent
than the original's has nowhere to go.

**Keep RWE's as the default and add the original's as a mode.** The reverse of
what was asked for, and it makes "plays like Total Annihilation" something a
player has to find. A project whose stated aim is compatibility with the
original should present the original's computer player first.

**A global switch.** One setting for the whole game. Rejected because the
per-seat choice is nearly free (the personality is already per seat and already
saved) and because the interesting game is the mixed one: an original against
RWE's, which is also how the two are measured against each other in the arena.

## Consequences

- **The default seat is `Original TA`**, in the skirmish menu, in `ai_arena`
  and in a game started from a command line with no personality. A seat whose
  personality is unknown (a saved game outliving a personality file) falls back
  to `GameParameters::aiEngine`, which is `Classic`.
- **A saved game records the engine per seat.** A save with no engine recorded
  was written before the choice existed and so by RWE's own computer player;
  it loads as `Rwe`, not as the new default, or a game in progress would change
  opponent on load.
- **The knobs are RWE's.** `--ai-tune`, the tuning profile and the autotune
  scripts set `AiTuningProfile` knobs that only the RWE engine reads. Setting
  one on a classic seat is an error rather than a silent no-op, for the reason
  `--ai-tune` already errors on a misspelt knob; the arena tools pass
  `--ai-engine rwe`.
- **The original's computer player does not use RWE's findings.** It has no
  transport policy, never sends an aircraft to attack, never guards or
  captures, rallies at six and attacks the unit nearest its centroid. These are
  the original's and are not defects to be fixed here; a better opponent is
  RWE's engine's job. `TOTALA-EXE-COMPUTER-PLAYER.md` is the specification and
  a divergence from it is a bug in the classic engine.
- **Same-seed determinism holds for both.** Each engine draws exactly one value
  from the simulation's generator at construction, so start positions are dealt
  the same way whichever engine a seat plays, and a replay (which idles
  computer players) does not depend on the engine at all.
- **The handicap is the engine's to supply.** The economy reads the income
  multiplier from the controller's profile (`AiController::getProfile`); the
  classic engine reports the original's 0.5 / 0.7 / 1.0.
- **A network game plays the default.** The launcher sends no personality, so
  both peers run the classic engine; a peer that has set `rwe.cfg` differently
  would not, which is the existing difficulty caveat and no worse.
