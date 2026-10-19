# Local playable feature batch

Ready: D:\RWE\build-release\rwe.exe, built 2026-10-19 at 14:00 local time.
Branch: 706-701-conformance. Changes are committed locally; no push or merge.

Release artifact: 48,746,603 bytes. SHA-256:
`FD63E2201B8D05B88A71859A9AC21C75EC0D8A8930BBD5F9EDE6EC78D020B155`.
The binary was built from the working tree before the finishing commit;
its generated source identity is 9215a8cd-d (with the full lifecycle changes).

Included changes:

- #706: remove dropped players' units at the agreed tick.
- #701: grey mission-excluded build buttons.
- #655: hide the example campaign by owner policy.
- #687: read Floater solely from its FBI key.
- #681: animation-counted burning, one spread with wind samples, burn damage,
  saved/hashed order and countdowns, play-once visuals and ignition sound.
- #682: shifted feature-centre blast reach and owner-approved deterministic
  accounting for active sprites, retaining the 64-entry cap.
- #683: shared 2,048-model/animation capacity, refusal and replacement rules,
  death/reclaim lifetime and blocking, explicit lifecycle shadows.

Validation passed:

- Debug game, all Debug targets and Release game builds.
- All 1,831 permitted tests, 193,660 assertions. Neither TaChecksums case ran.
- Focused feature selection: 74 cases, 12,911 assertions (included above).
- Full section scan: 1,053 objects; peak 24,276, limit 24,575.
- Two seed-7 games on shipped Coast To Coast data: identical 5,399-tick hashes.
- Real-data Palm02 ignition via ARMCOM's D-gun: 200 GAF updates, one 139-update
  spread timer, replacement at the documented expiry. See the #683 note.

Build script and logs: D:\tmp\rwe-feature-final-build.sh and the
rwe-feature-final logs in D:\tmp. Runtime evidence is under
D:\tmp\rwe-feature-final-determinism and D:\tmp\rwe-feature-final-fire.

Reference authority is documented in each issue's sibling implementation note.
All original-game comparisons are static byte-tactics findings at 97d7ac59,
retail GOG v3.1. No original-game execution, demo oracle or owner visual check
was performed. Existing blast/unit phase ordering and positional/fog audio gaps
remain outside this batch (BT-260 and #583).

Save/replay compatibility: this batch uses save and command-replay version 3.
Earlier nonburning saves remain accepted. Older command replays and legacy
active-fire saves are refused because they cannot reproduce the new fire rules.

For local play, start a fresh skirmish with foliage. Common Tree1 burns for
192 updates (6.4 simulation seconds), spreads once, and applies burn damage
once if its spark timer expires before burnout. Death and reclaim effects keep
blocking until their primary animation completes. Trees have no lifecycle
shadow; MarsPlant01's burn shadow ends after 10 updates while fire continues to
92. TREEBURN uses the existing effects path; positional/fog parity is not claimed.
