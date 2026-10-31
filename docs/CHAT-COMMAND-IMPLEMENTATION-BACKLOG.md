# Requested chat commands and cheat-code implementation backlog

Work order: [importance and implementation effort](PLAYTEST-2026-10-26-PRIORITIES.md).

The owner requested every code in the desktop playtest list and the
[TA Wiki cheat-code page](https://totalannihilation.fandom.com/wiki/Cheat_codes),
including its universal, exploit, developer and miscellaneous sections.
This page is an implementation inventory, not an implementation or a
verified description of the original. Tracking parent: [#905](https://github.com/CubeB/RWE-B4/issues/905).

Fandom returned a bot-verification page to the available readers. The owner
pasted the requested page text directly on 2026-10-27; the inventory below is
from that paste and the desktop file. No command from those inputs is silently
excluded because an older audit described it as not applicable or superseded.

## Current evidence

Static RWE source inspection at baseline
`fdd33b643e1efea4c4de13d185c93d265711ccb1` finds eight recognized names in
`src/rwe/game/chat_command.*`: ShareMetal, ShareEnergy, ShareAll, ShareMapping,
ShareRadar, SetShareMetal, SetShareEnergy and Give. In `GameScene_chat.cpp`,
the metal/energy/reserve/gift commands have handlers, the two visibility-sharing
names print notices, and ShareAll changes resource sharing only. None of these
commands was executed during triage. Existing support must be verified rather
than rewritten blindly; recognized does not mean complete.

Existing reference findings:

- [Console-command audit](BT-GAME-CONSOLE-COMMANDS-PARITY.md), especially
  sections 2-8, and [sharing-command audit](BT-GAME-SHARE-COMMANDS-PARITY.md).
  Both identify static reference revision
  `97d7ac59b460417bcdfa629cf90da99f31818c65` and explicitly state their evidence
  class and unresolved conflicts.
- [Command register](TOTALA-EXE-COMMANDS.md), section 124, for the line parser
  and resource sharing. Its broader classifications must be read with the
  later console audit's conflicts.
- [Game settings](BT-GAME-SETTINGS-PARITY.md),
  [campaign menu](BT-FRONTEND-CAMPAIGN-MENU-PARITY.md),
  [skirmish menu](BT-FRONTEND-SKIRMISH-MENU-PARITY.md), and
  [battle keys](BT-INGAME-KEYS-PARITY.md) for option/menu/repeat details.

The wiki's descriptions and mode/cheat claims are hypotheses where the
findings disagree or leave them open. Examples include player defaults and
numbering, `+NoMetal`/`+NoEnergy` assignment arguments, cheat gating, the
developer phrase, numeric ranges/persistence, and the exact effects of sharing.
Those conflicts must be resolved by the spec room before implementation.

## Complete requested interface

Every row is mandatory requested coverage; the linked issue owns its effect,
argument forms and validation. Names are case-insensitive where the accepted
parser finding says so; the developer phrase has its separately specified case.
Bracketed arguments describe optional forms, not literal brackets to type.

| Command or code | Current static status | Implementation issue |
| --- | --- | --- |
| `+now Film Chris Include Reload Assert` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#906](https://github.com/CubeB/RWE-B4/issues/906) |
| `repeat with \` | Repeat behavior requires implementation/verification with permissions. | [#906](https://github.com/CubeB/RWE-B4/issues/906) |
| `+noshake` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#907](https://github.com/CubeB/RWE-B4/issues/907) |
| `+clock` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#907](https://github.com/CubeB/RWE-B4/issues/907) |
| `+switchalt` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#907](https://github.com/CubeB/RWE-B4/issues/907) |
| `+dither` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#907](https://github.com/CubeB/RWE-B4/issues/907) |
| `+screenchat` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#907](https://github.com/CubeB/RWE-B4/issues/907) |
| `+contour #` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#907](https://github.com/CubeB/RWE-B4/issues/907) |
| `+showranges` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#907](https://github.com/CubeB/RWE-B4/issues/907) |
| `+sfx` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#907](https://github.com/CubeB/RWE-B4/issues/907) |
| `+bigbrother` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#907](https://github.com/CubeB/RWE-B4/issues/907) |
| `+gamma #` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#907](https://github.com/CubeB/RWE-B4/issues/907) |
| `+shadow` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#907](https://github.com/CubeB/RWE-B4/issues/907) |
| `+fshadow` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#907](https://github.com/CubeB/RWE-B4/issues/907) |
| `+tshadow` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#907](https://github.com/CubeB/RWE-B4/issues/907) |
| `+antialias` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#907](https://github.com/CubeB/RWE-B4/issues/907) |
| `+shading` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#907](https://github.com/CubeB/RWE-B4/issues/907) |
| `+light #` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#907](https://github.com/CubeB/RWE-B4/issues/907) |
| `+scrollspeed #` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#907](https://github.com/CubeB/RWE-B4/issues/907) |
| `+lostype` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#907](https://github.com/CubeB/RWE-B4/issues/907) |
| `+iface [#]` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#907](https://github.com/CubeB/RWE-B4/issues/907) |
| `+sound3d` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#908](https://github.com/CubeB/RWE-B4/issues/908) |
| `+cdstop` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#908](https://github.com/CubeB/RWE-B4/issues/908) |
| `+cdplay [#]` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#908](https://github.com/CubeB/RWE-B4/issues/908) |
| `+musicmode [#]` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#908](https://github.com/CubeB/RWE-B4/issues/908) |
| `+sing` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#908](https://github.com/CubeB/RWE-B4/issues/908) |
| `+setsharemetal #` | Recognized and handled; verify behavior, modes and arguments. | [#909](https://github.com/CubeB/RWE-B4/issues/909) |
| `+setshareenergy #` | Recognized and handled; verify behavior, modes and arguments. | [#909](https://github.com/CubeB/RWE-B4/issues/909) |
| `+shareradar` | Recognized notice; requested sharing toggle/effect incomplete. | [#909](https://github.com/CubeB/RWE-B4/issues/909) |
| `+sharemetal` | Recognized and handled; verify behavior, modes and arguments. | [#909](https://github.com/CubeB/RWE-B4/issues/909) |
| `+shareenergy` | Recognized and handled; verify behavior, modes and arguments. | [#909](https://github.com/CubeB/RWE-B4/issues/909) |
| `+sharemapping` | Recognized notice; requested sharing toggle/effect incomplete. | [#909](https://github.com/CubeB/RWE-B4/issues/909) |
| `+shareall` | Recognized; currently combines metal/energy only. | [#909](https://github.com/CubeB/RWE-B4/issues/909) |
| `+give player amount metal\|energy` | Recognized and handled; verify behavior, modes and arguments. | [#909](https://github.com/CubeB/RWE-B4/issues/909) |
| `+shootall` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#910](https://github.com/CubeB/RWE-B4/issues/910) |
| `+logo colour [player]` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#910](https://github.com/CubeB/RWE-B4/issues/910) |
| `+selectable` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#910](https://github.com/CubeB/RWE-B4/issues/910) |
| `+nometal [player [amount]]` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#910](https://github.com/CubeB/RWE-B4/issues/910) |
| `+noenergy [player [amount]]` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#910](https://github.com/CubeB/RWE-B4/issues/910) |
| `+bps` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#911](https://github.com/CubeB/RWE-B4/issues/911) |
| `+compression` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#911](https://github.com/CubeB/RWE-B4/issues/911) |
| `+atm` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#912](https://github.com/CubeB/RWE-B4/issues/912) |
| `+nowisee` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#912](https://github.com/CubeB/RWE-B4/issues/912) |
| `+control [player]` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#913](https://github.com/CubeB/RWE-B4/issues/913) |
| `+ai [player]` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#913](https://github.com/CubeB/RWE-B4/issues/913) |
| `+UNITNAME [player]; examples: CORCOM, ARMPT, CORDEV1, ARMDEV1` | Missing typed interface in the inspected parser; verify underlying subsystem. | [#913](https://github.com/CubeB/RWE-B4/issues/913) |
| `DRDEATH on the single-player selection screen` | Menu sequence; audit existing mission access/seat controls. | [#914](https://github.com/CubeB/RWE-B4/issues/914) |
| `*I through *X on skirmish setup` | Menu sequence; audit existing mission access/seat controls. | [#914](https://github.com/CubeB/RWE-B4/issues/914) |
| `+makeposter` | Recognized and handled: optional `all` or `<width> <height>`, tiled off-screen world capture, `screenshots/BIGSHOT.pcx`. See `docs/MAKEPOSTER.md`. | [#915](https://github.com/CubeB/RWE-B4/issues/915) |

The `+logo` row includes the colour-only and colour-plus-player forms.
`+nometal` and `+noenergy` include both no-argument and explicit-player forms;
the extra amount form is recorded by the reference and must not be confused
with the wiki's shorter summary. `+control`, `+ai` and `+UNITNAME` include
both default-player and explicit-player forms. The developer phrase is exactly
`+now Film Chris Include Reload Assert` as supplied. The frontend rows cover
`DRDEATH` on single-player selection and `*I` through `*X` on skirmish setup;
the repeat key is backslash (`\`).

The desktop syntax `#` denotes an argument placeholder, not a literal comment
character to type. Its stray asterisk before `+shareenergy` is not a new
command. The original's crashing invalid-player spawn is evidence about a
defect, not a requirement to crash RWE.

## Completion rules

- Link each implementation to accepted findings with revision/build, evidence
  and uncertainty under ADR-0004. Implementation agents never inspect the
  decompilation. An unresolved reference conflict remains pending research.
- Provide actual behavior for every requested interface, with visible useful
  feedback on unsupported contexts/errors. Parser acceptance or a silent no-op
  does not close an implementation issue.
- Separate local options from mutations that can affect a game's outcome.
  Replicate those mutations through the ordinary deterministic command stream;
  validate authority, cheat permissions, save/hash/dump and replay behavior.
- Validate with shipped data, original-game observations or demo oracles where
  available, and multiple peers for multiplayer effects. Static checks and RWE
  tests alone are not executed-original evidence.
- Where the original has no matching mechanism, implement a documented
  RWE-original behavior supporting the requested interface. Record deliberate
  departures in the divergence register, and claim parity only where verified.

No command implementation was written or tested in this documentation pass.
