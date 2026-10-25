# Existing TA mods: profiles, archive selection and COB extension boundaries

Spec-room findings, 2026-10-25, under ADR-0004. Documentation only. No RWE
implementation or tests were inspected or changed. The current target package
is TA Escalation GOLD10.2.0. The later package-specific check below examined
hashes and narrowly scoped raw path strings; no third-party binary was decompiled.

## MOD-AI-01: original AI profiles are loadable game data, not new AI engines

Original mission parsing reads `aiprofile` and constructs the selected
`ai/<profile>.txt` path; absent selection uses `ai/default.txt`. At player
initialization, the engine first attempts the mission-selected file and falls
back to `ai/default.txt` if it cannot be opened. If neither file opens, it
continues without executing that text. The files are opened through the same
virtual-data interface as other game assets, so an installed AI mod can replace
the selected path without replacing the executable.

The selected file's bytes are passed to the ordinary command-text interpreter.
They are **not passed through the separate AI tokenizer** whose punctuation
handling was left uncertain in `BT-AI-AI-PLAYER-PARITY.md` rule 15. Therefore
that tokenizer's treatment of leading periods does not, by itself, describe
how ordinary profile multipliers such as `.1` are consumed.

The relevant commands are `plan`, `weight` and `limit`. The plan enables the
other two for the selected difficulty; selectors can identify a concrete unit
or category set. Weights and limits are independent adjustments with their
own concrete-unit locks. Preserving category/group selectors, application
order and those locks matters: reducing a profile to a new arbitrary priority
list would not preserve its behavior. `weight`'s missing numeric argument and
`limit`'s missing count both default to zero. `plan any` is recognized when
`any` occupies the first argument; difficulty words can be checked across the
argument list. A lone `plan` disables the following gated directives.

After the main profile, the original runs the command fragments stored for
downloadable unit definitions. For each active computer seat, it makes one
pass conditional on that seat's unit-weight lock and a separate pass
conditional on its unit-limit lock. Each nonempty eligible fragment is again
executed by the command interpreter, with the AI command gate enabled.
These are commands, not just a single numeric per-unit weight. Replacing the
main profile and supporting these unit-definition fragments are separate
compatibility requirements.

Existing prose records additional semantics in `TOTALA-EXE` section 123,
`TA-COMMUNITY-AI.md` section 1 and `BT-AI-AI-PLAYER-PARITY.md` rules 14-15:
concrete-unit locks versus category scaling, finite completed-unit caps,
the special unlimited limit -1, and zero/other negative limits that prevent
selection. This pass directly confirms the load/dispatch paths and command
gates; it does not claim a new executed verification of every selection
arithmetic rule or the resulting build sequence.

An ordinary AI profile changes what the original computer player chooses
within its existing decision machinery. It is not an executable AI plugin.
Loading that profile into a different RWE planner can be useful as a deliberate
translation, but cannot establish original decision or timing parity merely
because the same text was accepted.

## MOD-ARCHIVE-01: the effective file, not archive presence alone, determines compatibility

The directly inspected original archive-registration path uses these stages:

| Stage | Original searched files |
| --- | --- |
| 1 | `rev31.GP3` |
| 2 | Visible `*.CCX` |
| 3 | Visible `*.UFO` |
| 4 | Local `*.HPI`, until ten successful additions or enumeration end |
| 5 | `*.hpi` at each enumerated CD-ROM root |

It then resolves shadowed archive entries. The existing
`BT-GAME-CD-CHECK-PARITY.md` rule 2 records the dependency behavior: loose files
win direct opens; among archives earlier mounts win. The archive-registration
integer distinguishes stream retention, not a second priority class.
This pass checked the executable's registration order directly; it did not
reinspect the separate archive-library implementation behind that prose.
Within a wildcard stage, do not invent a guaranteed alphabetical ranking:
the original uses its enumeration service's discovery order.

An `ai/default.txt` merely present inside a UFO is insufficient evidence that
it wins over another installed copy. A mod-compatibility report should name
the actual resolved file, archive and content hash, plus earlier competing
copies. The same applies to FBIs, weapon TDFs, COBs and build menus.

Mounting arbitrary `*.gp3` and `*.gpf` packages, mounting more than ten local
HPI files, or using explicitly ordered separate mod directories is useful
RWE behavior beyond this original discovery path. Such support must be stated
as an extension rather than inferred from the original's special `rev31.GP3`
registration. It may be essential to making a current mod's data reachable,
but does not prove that all its runtime requirements are supported.

## MOD-COB-01: extended script values and instructions need explicit specifications

The original COB virtual machine implements piece movement/rotation/visibility,
cache and shade controls, stack/arithmetic/comparisons, sleep/wait, script
calls/threads/signals, effects, unit-value reads/writes and transport-related
operations. `GET` and argument-bearing `GET` dispatch to the same unit-value
service; the latter supplies the additional operands needed by position and
vector queries. The original also has distinct transporter/carried-unit
queries; those should not be mistaken for evidence that arbitrary extended
unit-value IDs are supported.

The original unit-value reader handles IDs 1 through20. An unrecognized ID
returns 0. The writable service recognizes only IDs 1,5,6,18,19,20; unknown IDs
have no specific property write, although the common command-change
notification still occurs. The recognized original operations must retain
their original units and encodings: fixed-point heights, packed coordinates,
angles and percentage values are not interchangeable ordinary integers.

An unrecognized VM opcode ends that running script context's execution; the
original does not interpret an unknown instruction as a new extensibility
hook or safely guess its operand count. A third-party engine adding an ID or
opcode consequently establishes a new interface. Returning0 for that extended
request can reproduce the vanilla original while breaking the mod's intended
behavior. Accepting an archive and running Create once are insufficient
compatibility checks for such scripts.

No semantic specification for Escalation-only or third-party-patch-only COB
values/opcodes was obtained here. In particular the vanilla reference cannot
establish extended weapon-control, targeting, unit creation, effect or state
queries merely from a number seen in a mod script. Their names, inputs,
outputs, side effects and timing require third-party documentation, a suitably
licensed independent specification or an original patched-engine observation.
Support added from those sources must be labeled as extension compatibility,
not vanilla-v3.1 conformance.

## MOD-ESC-01: GOLD10.2.0 compatibility is a separate evidence claim

The existing `TA-PATCHES.md` section on Escalation 10.2 records an earlier
investigation of a bundled patched executable and identifies differences
including D-gun completion, aircraft pad seeking, AI handicap constants,
weapon update logic and other simulation behavior. That existing report is
evidence that treating all Escalation behavior as unmodified v3.1 is unsafe.
It is **not** a fresh validation of the user's GOLD10.2.0 package or of every
runtime DLL it includes. No statement about present third-party DLL semantics
is established by this pass.

Separate the following compatibility claims when reporting progress:

- The intended mod and its original AI profile files resolve through the VFS.
- Its ordinary original-data definitions and original COB operations work.
- Any documented third-party extension it requires is explicitly implemented.
- Real games with its actual unit set, scripts, weapons and selected AI run
  correctly, including midgame actions that invoke seldom-used script paths.

Only the first two can derive a vanilla behavior standard from this reference.
The third needs an extension source and the fourth needs execution evidence.
No full Escalation-support conclusion follows from the archive extension or
package version alone.

## Evidence and remaining uncertainty

- Read-only byte-tactics revision:
  `e89721c46c3fa06e59558419b54fe26061fafb73`; checkout clean.
- Reference build GOG/v3.1 `TotalA.exe`, installed executable independently
  hashed in this pass: MD5 `8e74a1dffa1f5988624c52048f5b20cd`.
- Static evidence: mission/profile path construction `0x435DA0`;
  selected/default profile open and command dispatch `0x4648E0`, called by
  player initialization `0x464990`; plan/weight/limit handlers
  `0x406C90`, `0x406DB0`, `0x406E40`; downloadable definition fragment
  passes `0x409F80` and `0x40A040`; archive registration `0x41D4C0`;
  original unit-value read/write `0x480770` and `0x480B20`; VM instruction
  dispatch and unknown-instruction handling `0x4B0DA0`.
- Prose consulted: `TA-COMMUNITY-AI.md`, `TA-PATCHES.md`,
  `BT-AI-AI-PLAYER-PARITY.md`, `BT-GAME-CD-CHECK-PARITY.md`, and the existing
  main data/AI findings. Older prose audit RWE comparisons are historical
  context; this pass did not inspect the current RWE implementation.
- Evidence class is **static original-reference comparison**. No original or
  patched game was launched, no mod installation was changed, no executable
  matching checker or demo oracle ran, and no mod gameplay test was executed.
  No active effective AI/profile/archive stack was measured here.
- A future compatibility fixture should use the actual selected map/profile,
  resolved archive identities, real mod units and the actual extended script
  operations encountered. Original-v3.1 and patched-engine outcomes must be
  recorded separately. Unknown requirements remain unknown rather than being
  silently called compatible.

## MOD-AI-02: exact downloadable-fragment initialization and lock examples

Follow-up static check, same reference revision/build, with real GOLD10.2.0
FBI data inspected read-only. Original table reset at `0x409470` assigns every
type a profile weight multiplier 100, clears its weight lock, assigns limit -1
(unlimited), and clears its limit lock. These multipliers are distinct from
the separately initialized base production ratings. The AI reset operation
at `0x40A100` resets the active computer tables and then reloads the selected
profile and downloadable fragments; reloading must not multiply stale tables
again in place as though that were the reset behavior.

Original FBI loading at `0x42A8D0` reads `ai_weight` into a 64-byte buffer
(at most **63 payload bytes plus NUL**), defaulting to the empty string.
`ai_limit` is read separately; the
traced downloadable execution passes use the `ai_weight` string. The
downloadable definition flag defaults to 0 in FBI parsing. The flag stores the
parsed integer's **low bit**, rather than interpreting every nonzero integer
as true: 0 and 2 are false; 1,3 and -1 are true; -2 is false. This is directly
checked in the FBI loader at `0x42BF40`. A non-downloadable
definition does not qualify for these passes merely because it has a nonempty
`ai_weight`. Other original unit/build-menu loading can promote a definition
to downloadable, so the effective flag after loading is what matters.
In particular `0x42BD40` promotes it when a download build-menu entry's first
named type matches the definition, issuing the original missing-flag warning.

The fragment uses the full ordinary command grammar, including its selectors.
It does not implicitly substitute its owning unit as the command subject.
Shipped package examples include a fragment on ARMLIFT naming ARMMUAT,
and one naming the `ALL_MINE4` category. These examples prohibit treating the
field as an unnamed numeric weight for the owner. Other package fragments
explicitly write their owner's weight 0; several also declare Downloadable 1.

The command's numeric readers use ordinary decimal integer conversion for
limits and floating conversion for weights (`0x4B73E0`, `0x4B7410`). A missing
argument defaults 0; a malformed number follows those conversions rather than
the separate AI token scanner. A missing **field** is an empty fragment and
executes nothing, leaving the initial multiplier 100 unless the main profile
changes it. A present `weight UNIT` command with no number instead supplies0
and can disable and lock that unit's weight.

Each applicable weight adjustment multiplies the current profile multiplier,
truncates to an integer and clamps to0..100. A concrete unit selector then
locks that unit against subsequent weight adjustments. A category selector
does not lock and therefore can compound with later adjustments. Limits use
their independent lock. The two downloadable passes consider, respectively,
the owning definition's weight lock and owning definition's limit lock at
the time that definition is visited. They do not globally skip every fragment
after one concrete weight directive. Commands inside a qualifying fragment
still have their own subject-specific locks.

Each pass enables the AI command gate **once at the start of that pass**,
before visiting its definitions. The final `plan` state from the main profile
does not suppress the first pass: its start re-enables commands. The entire
qualifying fragment is executed, including both `weight` and `limit` when
both occur; there is no command-type extraction just because the pass's
eligibility check used a weight lock or limit lock. A fragment's own `plan`
can change the gate seen by later commands and later fragments within that
pass. The next pass independently re-enables the gate at its start.

For a fresh single computer seat with both locks clear, an owning fragment
containing concrete `weight UNIT .5` and concrete `limit UNIT 3` produces
multiplier50 and limit3 and sets both target locks on its first execution.
The second pass skips that owner's fragment because its limit lock is now
set. If only the owning weight was already locked by the main profile, the
first pass skips the whole fragment; its second pass can still execute the
whole fragment, ignore the already locked weight command and apply limit3.
This is different from merely applying weight lines first and limit lines
later. A fragment naming OTHER or a category need not lock its own owner;
it can therefore qualify for execution again in the second pass. Identical
zero-weight assignments are often idempotent, but arbitrary category
multipliers need not be.

These are **prose expected outcomes**, not executed original-game tests:

| Scenario, after fresh reset | Expected final result |
| --- | --- |
| No main-profile weight change; missing/empty `ai_weight` | Multiplier100 remains |
| Main profile says `weight UNIT .5`; downloadable owner fragment says `weight UNIT 0` | Concrete profile assignment makes multiplier 50 and locks it; later fragment cannot turn that weight into 0 |
| Main profile says `weight CATEGORY .5`; downloadable UNIT in that category says `weight UNIT .2` | Category gives 50 without locking; concrete fragment gives 10 and locks |
| Fragment says `weight UNIT` with no number and its weight is unlocked | Multiplier0 and weight lock set |
| Fragment says `weight OTHER 0` | OTHER is affected; owning unit is not silently substituted |
| Main profile explicitly locks UNIT's weight but not its limit; its fragment contains `limit UNIT 3` | First downloadable pass can skip it on weight lock; separate limit-unlocked pass can still assign limit 3 |
| Concrete `limit UNIT 0`, then a later concrete `limit UNIT 10` | First limit remains 0 and blocks selection; a weight lock is a different state |
| `limit UNIT -1` versus `limit UNIT -2` | -1 is unlimited; -2 blocks selection rather than being normalized to unlimited |

Because command handlers can affect more than the invoking computer seat and
category fragments remain unlocked, do not collapse the ordered per-seat
passes into one convenient unordered application. This pass has not measured
every multi-seat compounding example against the running executable.

## MOD-ESC-02: GOLD10.2.0's AE profile namespace has direct static package evidence

The read-only package at
`C:/Users/Baile/Desktop/TAESC/Step_2_Install Main Files (copy all contents into your main TA folder)`
contains `TAESC.gp3` with these profile entries: `ae/default.txt`,
`ae/acid.txt`, `ae/airbattle.txt`, `ae/hover.txt`, `ae/krogoth.txt`,
`ae/metal.txt`, `ae/seabattle.txt`, `ae/urban.txt`, `ae/waterwrld.txt`.
No `ai/` entries were found in that archive. It also uses a `unitse/` FBI
namespace; archive availability must not be mistaken for ordinary `units/`
discovery.

No third-party binary was decompiled. A narrowly scoped raw-ASCII comparison
of its bundled executable against the original found:

| Executable file offset | Original ASCII | GOLD10.2.0 package ASCII |
| --- | --- | --- |
| `0x103110` | `ai` | `aE` |
| `0x105908` | `ai\default.txt` | `aE\default.txt` |

These offsets contain the directory and fallback strings whose original uses
were already traced in MOD-AI-01. Together with the archive's AE profiles,
the replacements are strong **static package-specific evidence** for the
patched engine selecting profiles under `ae/` instead of vanilla `ai/`.
This is an inference from package data and changed string operands, not an
executed patched-engine trace and not a proof that every surrounding patched
instruction is unchanged. Supporting that namespace should be explicitly
described as Escalation compatibility, independently of vanilla fallback.

Package executable identity: MD5 `1e677a7f92c79b5ab35440853d822c17`,
SHA-256 `06d9e87f35846c31913f989abae7223f62e8ebc8d3c3d175b561bc9c8f238600`.
This MD5 agrees with the earlier `TA-PATCHES.md` investigation. The bundled
`TAESC.dll` SHA-256 is
`61783acaa51ae5348e91ca57706540cff39cbf9905ba6fb26020603d394965df`;
no matching AE/AI profile-path ASCII was found in that narrowly scoped search,
which does not prove the DLL has no related behavior.

The package's primary shipped `ESC_READ_ME.txt` describes enhanced AI logic,
resource handicaps and optional third-party AI packs; `GOLD_10_2_0.txt` records
updates to Default/SeaBattle profiles. Neither inspected text documents the
AE path itself. The readme's numerical descriptions are author claims and
were not independently executed here. No binary change, install, visible game
launch or patched-engine gameplay validation was performed.

## MOD-DIRS-01: explicit directory remapping is an RWE extension

The implementation room reports that the target package also uses namespaces
`unitse`, `weapone`, `guie`, `gamedate`, `downloadse` and `unitpice`, alongside
`ae`. This pass directly observed `unitse` FBI entries and the AE profiles;
the other directory names are an implementation-room package census, not a
newly independent spec-room measurement of every entry count.

Allowing the user to configure the unit, weapon, GUI, game-data, downloadable
build-menu, unit-picture and AI-profile directories explicitly is a useful
**RWE extension**, not a behavior inferred from vanilla's hardcoded directory
names. The configured AI directory must govern both the mission-selected
profile and its default-profile fallback. All game-launch and diagnostic
entry points intended to run that configured mod need the same effective
directory choices. This does not prescribe an automatic package detector or
an Escalation preset: neither was established as part of this change.

Unconfigured vanilla operation retains its existing ordinary namespaces,
including `ai/<profile>.txt` and `ai/default.txt`. Selecting AE is an explicit
mod choice. A failed named profile should fall back within that chosen
namespace; silently switching to another installed mod's stock profile would
mix policy from a different dataset and conceal the failure.

Record the deliberate namespace configuration in the existing RWE extension
or divergence register before implementing it. Suggested wording: "RWE
allows explicit per-mod resource directories, including the AI-profile root,
with ordinary TA directories as the defaults. Configured launch and harness
paths share that selection. This supports data layouts such as Escalation's
AE and UNITSE namespaces without claiming that vanilla v3.1 discovers them
automatically or that all Escalation runtime extensions are implemented."

A run that starts successfully with stock unit paths but modded shared
resources is not evidence of Escalation compatibility. The implementation
room reports that such a mixed-data arena run can succeed. Before interpreting
gameplay outcomes, verify the resolved actual mod unit set, selected profile,
build menus and their source identities. That report was not independently
executed in the spec room.

## MOD-AI-03: payload bounds, definition order and immutable collection

Final targeted static clarification, same original reference revision/build.
The FBI string reader used for `ai_weight` delegates to `0x4C48C0`, which
copies within the supplied size and explicitly writes NUL into its last byte.
A size 64 buffer therefore preserves at most 63 payload bytes, not 64. It is a
byte bound in the original single-byte asset encoding, not a Unicode-character
bound. An empty field remains an empty fragment. This resolves the earlier
wording "up to 64" in MOD-AI-02.

Fragment passes iterate current loaded unit-definition IDs in ascending order,
starting at 1 and excluding the reserved zero entry. That is not a guarantee
of raw directory-enumeration order. Original type setup at `0x42D2E0` filters
the unit table, sorts accepted definitions by **case-insensitive UNITNAME**,
then rewrites the numeric IDs in that order. This occurs before the later
profile/fragment application. File names and UNITNAME can differ; sorting
fragments by FBI path is not the same ordering rule. Equal-name tie behavior
and non-ASCII locale ordering were not established by this pass.

Downloadable promotion uses a valid stored first row, as clarified in
`DOWNLOAD-MENU-BUILD-LINK-FINDINGS.md`. Storing its product name requires
successful UNITMENU resolution. Promotion then compares that product name
against loaded definitions, implicitly resolving the target. Missing or
unresolved builders and products must not produce fabricated promotion in
a safe adaptation. A later valid row can add a build link without qualifying
as that file's first-row promotion source.

An RWE immutable configuration collector is a reasonable implementation-room
design: retain each accepted loaded type's canonical unit name, effective
Downloadable flag after valid promotion, bounded `ai_weight` bytes and
provenance. Preserve the loaded original-compatible unit-name order for those
records. The configuration can then be shared by gameplay and arena entry
points; mutable per-seat weights, limits, gates and locks still belong to the
actual initialization/execution rather than the immutable asset record.
This is a proposed RWE design, not recovered original structure or pseudocode.

If RWE chooses deterministic filename ordering for archive enumeration,
download-file promotion traversal or other asset discovery where original
discovery order cannot be reproduced, document that choice as an explicit
deterministic adaptation. It must not be described as original fragment order.
In particular a case-insensitive UNITNAME sort can implement the established
normal fragment order without introducing a filename-order departure there.

The directly checked string reader, type setup and download loader are marked
matched in the inspected reference progress table. No checker, runtime
original/patched game or gameplay oracle was executed in this clarification.
