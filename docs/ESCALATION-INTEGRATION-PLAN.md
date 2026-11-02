# TA: Escalation integration plan

Status: proposal, 2026-11-02. Planning only; no Escalation-specific code exists
yet. This is the companion to `PROTA-INTEGRATION-PLAN.md` and uses the same mod
SDK.

## What Escalation is

TA: Escalation is a large Total Annihilation total-conversion-style mod: new
and rebalanced units, weapons, build menus, GUI pages and campaigns, plus
engine behaviour changes that upstream ships as a patched `TotalA.exe` and
`TAESC.dll`. RWE must load its **content** and reimplement its **engine
changes** natively. It must never execute the bundled executable, DLL or
installer.

Existing evidence and recognition:

- `docs/TA-MOD-COMPATIBILITY-FINDINGS.md` and `docs/TA-PATCHES.md` record the
  upstream changes.
- ADR-0002 lists the nine behavioural changes Escalation's patch makes and maps
  them to its eventual hook surface.
- The package library already recognises the desktop Escalation layout
  (`AE/`, `UNITSE/`, `WEAPONE/`, `GUIE/`, `GAMEDATE/`, `DOWNLOADSE/`,
  `UNITPICE/`) and its `GOLD` release marker — see
  `docs/PACKAGE-LIBRARY-FINDINGS.md` PACKAGE-003.
- `rwe_mod.json` is already read from shipped packages; Escalation is the
  motivating example there.

## Content layout

| Resource | Escalation directory |
| --- | --- |
| units | `UNITSE` |
| weapons | `WEAPONE` |
| ai | `AE` |
| gamedata | `GAMEDATE` |
| guis (front end) | `GUIE` |
| builderGuis | `GUIE` |
| downloads | `DOWNLOADSE` |
| unitpics | `UNITPICE` |

These are exactly what the manifest's `resourceDirectories` map is for. The
base archives (rev31.gp3, totala1/2.hpi, ccdata, etc.) stay in the base data
root; Escalation overlays them. The mapping is part of the resolved identity.

A first manifest:

```json
{
  "manifestVersion": 1,
  "id": "taescalation",
  "name": "Total Annihilation: Escalation",
  "version": "9.9.0",
  "kind": "content",
  "api": 1,
  "resourceDirectories": {
    "units": "UNITSE",
    "weapons": "WEAPONE",
    "ai": "AE",
    "gamedata": "GAMEDATE",
    "guis": "GUIE",
    "builderGuis": "GUIE",
    "downloads": "DOWNLOADSE",
    "unitpics": "UNITPICE"
  }
}
```

The version must come from the package's own release marker, never be guessed
from a folder name. No `entry`, `features` or runtime hook is required for the
first content release.

## Engine deltas

ADR-0002's table is the reference. Classification:

1. **AI handicap constant** — a data key read by the AI; no engine hook.
2. **Repair-pad seeking behind a new FBI flag** — the flag is readable through
   the existing TDF preservation work; the decision becomes
   `hook_seek_repair_pad` (MOD-08 runtime surface).
3. **Expired ballistic round: `burnblow` to `noautorange`, inverted** — a
   decision hook (`hook_round_expired_detonates`), plus the existing weapon
   keys.
4. **Weapon motor-function swap** — `hook_weapon_motor`; what the swap selects
   is located but not decoded, so this stays blocked on a prose finding.
5. **D-gun and `commandfire` completion** — `hook_mission_complete`.
6. **Reclaim power-switch gate** — `hook_reclaim_allowed`.
7. **Attacked / kamikaze reaction cluster** — `hook_attacked_reaction`.
8. **New order type** — needs MOD-08 custom orders (`ModOrder` plus a
   `PlayerCommand` alternative). This is the largest dependency and should not
   block the content release.
9. **Clamp at `0x41BD90`** — routine not identified. A hook cannot be named for
   an undecoded computation; obtain a spec-room finding first. Do not guess.

Nothing here authorises copying an engine patch. Each item needs its own
finding under ADR-0004 before implementation; the Escalation upstream patch is
a hypothesis, not the standard.

## What RWE already has

- Escalation layout recognition and `rwe_mod.json` metadata.
- Builder-GUI page loading and the `builderGuis` resource mapping.
- Per-unit veterancy keys under the Community Patch (issue #934) — closely
  related to Escalation-era unit data.
- Community Patch as an opt-in ruleset (ADR-0006), which is the natural place
  for Escalation-profile switches.
- The mod SDK: manifest, resource-directory mapping, identity, pre-match
  transforms, runtime damage/tick, and save/replay identity.

## Not supported yet

- Applying `resourceDirectories` to the engine's `PathMapping` at launch (the
  manifest and identity side exists; the loader wiring is issue #975's
  companion work and must be shared with ProTA, not duplicated).
- Runtime decision hooks (repair pad, rounds, D-gun, reclaim, reaction) —
  MOD-08 remainder.
- Custom orders — MOD-08.
- The undecoded clamp.
- Escalation's DLL-only presentation/engine extensions, if any.
- Multiplayer against original/patched `TotalA.exe` clients.

## Delivery phases

### E1 — recognise, install and load content

- Package the content root, detect `UNITSE`/`WEAPONE`/`GAMEDATE`/`GUIE`, and
  write a manifest with the mapping above.
- Apply the mapping to `PathMapping` and load through the normal VFS.
- Acceptance on real Escalation data: unit/weapon census, build menus, GUI
  pages, unit pics, sidebar art and a skirmish start. No bundled executable or
  DLL is run.
- This is the same engine work as ProTA P1/P2; land it once for both.

### E2 — Escalation rules profile

- Decide with ADR-0006 review whether the profile is a third value of the
  ruleset choice (`off | community | escalation`) or a named profile layered on
  Community.
- Implement the data-key and already-decoded decisions first; every switch is
  named and defaulted explicitly.
- Acceptance: a skirmish behaves as the package intends, saves/replays record
  the profile, and unsupported switches are refused rather than ignored.

### E3 — runtime decisions and orders

- Add the MOD-08 hooks in the order above, each with a finding and a test.
- Add custom orders only when the new missile/order data actually needs them;
  keep the first release content-plus-profile if possible.
- Acceptance: the affected units/builders behave correctly, hashes agree
  across peers, save/resume and replay agree, and the unmodded game is
  unchanged.

### E4 — presentation and verification

- Escalation-specific GUI/sidebar art, if it differs from the base layouts.
- Windows/Linux load and network run; multiplayer compares the full resolved
  identity.
- Record every deliberate divergence and every unsupported upstream feature.

## Risks and non-goals

- **No patch emulation.** Do not load `TAESC.dll`, a patched executable or an
  installer. No CRC/challenge-response emulation.
- **Undecoded behaviour stays unimplemented.** The `0x41BD90` clamp and the
  weapon-motor selection need spec-room findings first.
- **Scale.** Escalation adds many units, weapons and large GUI pages; measure
  load time, memory, AI and pathfinding on real data.
- **Content-only is incomplete.** Without the rules profile, balance and AI
  will not match the package's intent; say so in the library entry.
- **No original-client interoperability claim.** A patched client and RWE are
  different simulations.

## Open questions

1. Which Escalation release is the target, and what is its exact version
   marker?
2. Is content + profile enough for the first release, or are the new orders
   required?
3. Should the Escalation rules be a third ruleset value or a profile layered
   on Community?
4. Are Escalation's GUI/presentation extensions data-only, or do any depend on
   its DLL?
5. Which upstream changes may be implemented before their register findings
   exist? (Answer: none.)
