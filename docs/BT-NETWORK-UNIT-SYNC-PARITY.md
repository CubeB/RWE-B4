# Pregame unit catalogue, compatibility, restrictions and the completion tests parity audit

Spec-room finding, 2026-10-17. Like `BT-GAME-DATA-FILES-PARITY.md`, `BT-GAME-PLAYERS-PARITY.md`
and `TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one
chapter of the reference, `network-unit-sync` (74 services from `0x46c620` to `0x470a40`): the
pregame handshake in which a multiplayer setup reconciles **which unit definitions each machine
has** (key, script checksum, permission, per-type limit), the tests that say the handshake is
complete, the host's restriction editing, how the result is applied to the unit definitions, a
reusable resend helper, and (sharing the address range) the online report dispatch. It does not
replicate live units; position, health and orders are other chapters. This is the finding that
implementation work cites for any of them, per `AGENTS.md` and ADR-0004.

**Scope.** RWE's own lockstep multiplayer is RWE-original and is **not** audited against this
chapter (the lobby's archive-hash check in `launcher/src/common/archives.ts` is its own,
stricter, mechanism). Audited here: RWE's TA-compatible battleroom (`net/ta/TaBattleroom.*`,
`TaChecksums.*`) and `docs/TA-NETWORK.md` "Unit sync", an **executed-against-real-TA record that
outranks a static reading**, with the `0x1a` entries of `docs/TA-DEMOS.md`. Where this chapter
re-explains a capture, it is recorded as an explanation, not a correction.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\network-unit-sync.md`).
  Nothing else under that tree was opened beyond the sibling network chapters: no source, no
  decompilation, and none of its source links were followed. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only; no row
  concerns this chapter. `BT-GAME-DATA-FILES-PARITY.md` rule 8 (the unit script checksum) is
  continued, not repeated.
- **Evidence class.** A **static reading** by that project: nothing was executed. Weaker than
  `TOTALA-EXE*.md` and than `TA-NETWORK.md`'s captures. Disagreements are marked **CONFLICT**
  with no winner picked.
- **The RWE half** is a read-only audit of `net/ta/TaBattleroom.*` (the unit-sync builders and
  `echoUnitSync`, about 215-250 and 510-545), `net/ta/TaChecksums.*`, `puppet/TadPuppetDriver.cpp`
  (how a unit type index is resolved), `docs/TA-NETWORK.md` and `docs/TA-DEMOS.md` "The `0x1a`
  unit table", at the working tree of 2026-10-17. Nothing was built, run or changed. Another
  agent had uncommitted edits in the tree, so line numbers are "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. The catalogue entry and the 14-byte packet
3. The handshake the capture shows, explained
4. Compatibility: the key, the script checksum and the version rule
5. Completion tests and status text
6. Restrictions, limits and applying the result
7. The resend helper, and the report dispatch (not applicable)
8. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| packet layout (type `0x1a`, 14 bytes, subtypes 0 to 4) | `TA-DEMOS.md` "The `0x1a` unit table" (`1a <sub> <u32> <u32 id> <u32 value>`, the value assembled from three places) | rule 2 agrees and names the three fields |
| handshake, the 1 + 2n counter | `TA-NETWORK.md` "Unit sync" (steps 1 to 4, the counter reaching 557) | rule 3 explains it |
| the script checksum and who compares it | `BT-GAME-DATA-FILES-PARITY.md` rule 8 ("consumer unknown"); `TA-NETWORK.md` ("a TA host checks a joiner's CRCs") | rule 4 identifies the consumer |
| completion tests | none | rule 5 |
| restrictions, apply | `TA-DEMOS.md` ("a unit restrictions file") | rule 6 |
| sequenced resend, report dispatch | none | rule 7 |

## 2. The catalogue entry and the 14-byte packet

**Reference.** The manager holds a map keyed by the **unit FBI checksum** (unsigned) whose entry
is: the key, a cached **script/resource checksum** (zero until first needed), a 16-bit
**permission**, a 16-bit **availability** (compatibility across the checked participants) and a
32-bit **per-type limit** (initially zero for a type whose definition flag says it is limited,
otherwise minus one). Construction inserts definition indices **one through count minus one**
(index zero is never inserted); definitions with equal checksums share a key and the later one
overwrites the earlier. Every packet is **14 bytes**: byte 0 type `0x1A`, byte 1 subtype, bytes
2-5 a sequence id that the live adapter writes as zero, bytes 6-9 the unit key (or a
subtype-specific integer), and bytes 10-13 the value: **permission byte, availability byte, then
a signed 16-bit limit**. Subtypes: 0 initial notice, 1 expected unit count, 2 key and checksum
report, 3 permission, availability and limit update, 4 acknowledgement count. Limits are narrowed
to signed 16 bits on the wire and widened on receipt, so a limit above 32767 is not preserved.

**RWE: MATCHES the builders.** `taBuildUnitSyncHeader` is 14 bytes of zero with type `0x1A` and
subtype 0; `taBuildUnitSyncEcho` is `1A 03`, four zero bytes, the key, a 16-bit `0x0101` or
`0x0001` (permission byte then availability byte, little-endian) and `0xFFFF`
(`TaBattleroom.cpp` about 215-250). `0xFFFF` is minus one, the "no limit" initial value, and the
first two value bytes are the chapter's permission and availability. This matches the decode in
`TA-DEMOS.md` ("the low byte is always 1, the second byte is a flag, the top word is a `0xffff`
sentinel") and adds the names. `taParseUnitSyncId` reads the key at offset 6 of a subtype-2
record and ignores the checksum at offset 10.

## 3. The handshake the capture shows, explained

**Reference.** On the **authority side** (the host), `ProcessSync` keeps one record per remote
human; discovering one sends subtype 0 and counts it sent; if the participant set changed, every
map entry is **rechecked with checksum zero**, which tests only whether each tracked player has
reported the key, and each recheck sends a subtype-3 record. A subtype-2 report appends the
key and checksum (a duplicate key is ignored but the received counter has already risen) and
checks that one key again, sending another subtype-3 record. On the **client side** (a joining
TA), nothing happens until a packet has been handled; it then sends subtype 1 with the count of
its definitions minus one, then up to **four** reports per call, and once all are reported it
repeats subtype 4 carrying the **cumulative count of handled packets**, a count never reset
after acknowledgment. The client mutates its own map only on subtype 3.

**Register.** `TA-NETWORK.md`: the host sends one subtype-0 record, then each of its unit ids as
a subtype-3 record of "status `0x0001`"; the joiner sends subtype 1, then subtype 2 per type;
the host answers each batch with subtype-3 records of "status `0x0101`: in use"; the joiner sends
subtype 4 until the count reaches **1 + 2n (557)**.

**RWE: MATCHES the capture; the chapter supplies the meaning.**

- **The two records per id are not "unused" and "in use".** `0x0001` is permission one,
  availability **zero**: the recheck at join time, before the joiner has reported (not every
  tracked player has the key yet). `0x0101` is permission one, availability **one**, sent after
  the report. So `TA-NETWORK.md`'s "in use" is a name for **available**, and the first record
  is **not yet verified**. The joiner's own map ends at available for every id the host
  confirmed. *Register wording only* (kind: register).
- **Why 1 + 2n.** One subtype 0, n rechecks and n post-report records: 557 for 278 types. It
  is a count of **handled packets**, not of units, and the joiner sends it per record handled,
  tens of times a second (the capture's 387, 510, 557).
- **Order: DIFFERS in a way that was accepted.** The real host sends the n rechecks at join and
  the n confirmations as reports arrive; RWE sends the pair for each reported id in the
  joiner's order (`echoUnitSync`: unverified, then available, flushed every 35 records). The
  count is the same, the final state is the same, and `TA-NETWORK.md` records that a real
  joiner counted to 557 and launched. The difference is the interleaving only.
- **The 512-id cap: DIFFERS.** `maxUnitSyncIdsPerMessage` (512) answers no more ids than that in
  one message and counts the rest as refused. A joiner with more than 512 definitions would
  count fewer than 1 + 2m; a joiner does not wait on its count (rule 5) but a mod with more than
  512 types would show unconfirmed units. The sets seen are 278 (the GOG install, `TA-NETWORK.md`), 317
  (ProTA 4.8) and 549 (Escalation 10.2) (`TA-DEMOS.md`), so a 549-type joiner that sends its table as one
  message (the code's own comment says a joiner does) already exceeds it. **That is the one concrete
  consequence found**: raise the bound above the largest known set (about 600) and keep a sanity limit.
- **Duplicate keys.** The chapter says definitions with an identical FBI checksum collapse to
  one key on the authority side. That may be why `TA-DEMOS.md` finds the subtype-2 block "one or
  two entries longer than" subtype 3 (an open item there); whether the demo's table is a capture of the
  packets or the recorder's own arrangement of them was not checked, so this is a lead, not a
  finding. **UNCLEAR**.

## 4. Compatibility: the key, the script checksum and the version rule

**Reference.** `CheckUnitAvailable` ignores disabled state and keys the host lacks. For a nonzero
checksum it lazily computes the host's **script checksum** for that definition (cached in the
map) and compares it with the reported one. It then requires **every tracked player to have
reported the key**; and, only when a nonzero checksum is involved and the remote's version is
**at least 2, or 1 with a minor of at least 2**, also requires the stored checksum to equal it.
Older versions need only the key. If a player record is missing mid-scan the loop breaks and the
result stays true. The result is written to availability and a subtype-3 notice is sent whether
or not it changed. The local script checksum comes from data-file checksumming (`BT-GAME-DATA-
FILES-PARITY.md` rule 8: it includes associated resources, not only a script).

**Register.** `TA-NETWORK.md`: "Only the joiner sends CRCs, so only the host checks them", the
"CRC" being the unit's id; `TaChecksums` computes the **key** (the checksum over the raw FBI
bytes) and notes the host "echoes the joiner's ids back" so no checksum is computed. The
**second** value at offset 10 of a subtype-2 record was "not enough to be a substitute" in the
register's own words and its consumer was unknown (`BT-GAME-DATA-FILES-PARITY.md` rule 8).

**RWE: the consumer is now identified; RWE does not use it.**

- **The consumer: this closes `BT-GAME-DATA-FILES-PARITY.md` gap 7's question.** The script
  checksum is read by `CheckUnitAvailable` in the authority's pregame handshake, for a remote
  at or above the version threshold. It is a *pregame data-compatibility check*, not a
  simulation input.
- **RWE as host: DIFFERS, by documented design.** `echoUnitSync` confirms every id as available
  and reads neither the key against RWE's own definitions nor the checksum. `TA-NETWORK.md`
  records this ("hosting needs no unit checksum"). It is correct for getting a game started, and
  it means RWE **never tells a TA that a unit is unavailable**. A joiner whose data set differs
  (a mod unit RWE lacks) is allowed to build it, and RWE then receives a `0x09`/`0x11` for a
  type index it resolves through its **own** load order (`tadUnitNameForTypeIndex`,
  `tad_events.cpp` about 627-638; the numbering is by sorted FBI stem, `TA-DEMOS.md`), so the
  picture is wrong or the unit is "unplaced" (`TadPuppetDriver` `unplacedUnits`) with no warning
  to either player. RWE could instead record the joiner's (index, key) list from its subtype-2
  records, compare each key with `taUnitTypeChecksum` of its own FBI at the same load-order
  index, and answer availability zero for a mismatch; that is the chapter's mechanism, it needs
  only the first checksum, and it would also detect the whole-table mismatch (`unitId` shifted
  by an extra unit) that otherwise renders as the wrong units.
- **RWE as joiner: not implemented.** Joining a TA host (open in `TA-NETWORK.md`) needs the
  reporting side: subtype 1 with the definition count minus one, subtype 2 per definition with
  the key and the **second** checksum, up to four per call, then subtype 4 with the cumulative
  handled count. Two facts reduce the risk: the second checksum is compared only if the joiner's
  advertised version is at least 2 (or 1.2), and the version is carried in the joining player's
  21-byte data block (`BT-NETWORK-HAPINET-PARITY.md` rule 8, **UNCLEAR** which short is the major),
  so what version RWE announces decides whether it must compute the heavy checksum at all.
- **The fixed pseudo-entry in the demo table: UNCLEAR.** `TA-DEMOS.md` finds one id
  (`2455016279`) in every demo of two unrelated data sets, with the availability byte cleared and
  once a limit of 1000. The chapter has no fixed entry; availability zero is what an entry that
  not every tracked player reported would have, and a limit is what an edit sets
  (rule 6). A possible reading is an entry produced by the map's default-constructing
  subscript, but no source here says so.

## 5. Completion tests and status text

**Reference.** Three tests differ on purpose. `AllPlayersSynced`: true at once if the manager is
disabled, in a non-direct (client) setup, or with no tracked players; otherwise, skipping players
missing from the game and active computer players, it requires for each remote human a **nonzero
expected count, a reported-key count equal to it, and sent equal to acknowledged**. An
acknowledgement *greater* than sent still fails the equality, although subtype 4 only raises the
stored maximum. `IsPlayerSynced` is false outside direct mode, true for a disabled manager, an
absent player or a computer player, and applies the same equality tests otherwise; an empty
catalogue can never pass for a remote human. The status text returns the first problem in
player order ("expected count zero", reported-versus-expected mismatch, sent-versus-
acknowledged mismatch) or "OK", formatted into one shared buffer, and ignores the skip rules.
There is **no timeout, retransmission or transport-result test** in the handshake; the battle
room calls `ProcessSync` each frame and the front-end coordinator decides when to finish and
apply (and then delete) the manager.

**RWE: MISSING, with one consequence.** RWE's battleroom never evaluates sync completion. It
launches when the host asks (`TaBattleroom::launch`) provided somebody has joined and the teams
rule passes (`launchRefusal`); the launch does not wait for the joiner's subtype-4 count, nor
show "waiting for unit sync". A real host's front end gates its Start on these tests (the
chapter says the coordinator "decides when to finish", not that Start is disabled, so the
gating is **UNCLEAR**). With RWE as host the practical risk is a launch while a slow joiner is
still reporting: the joiner has no completed catalogue when it loads. The `Waiting`/`Launching`
states are the place for a "joiner has acknowledged `1 + 2m`" condition, with a timeout
because the original has none and a vanished joiner would otherwise block for ever. Kind:
UI/host rule, not hash-visible.

## 6. Restrictions, limits and applying the result

**Reference.** The host edits an entry's permission (toggle, disallow, allow) or limit; each edit
notifies (direct mode sends the entry to every tracked player; both modes append the key to a
change list, coalescing repeats), and the edit helpers mutate even while the manager is
disabled. A client's subtype 3 builds or replaces its entry from wire key, permission,
availability and signed limit and clears the cached checksum. At the end,
`ApplyToUnitTypes` writes, for each local definition from one up, **availability = permission AND
availability** into definition flag bit 23 and the limit into the definition's per-unit maximum;
a definition with no entry is made unavailable with limit zero. It does not touch build menus
(consumers read the flags) or copy any remote asset.

**RWE: MISSING for restrictions, MATCHES in effect for the unrestricted case.**

- RWE's TA host has no restriction editor and sends permission one and limit minus one for
  every id; the joiner therefore receives "everything allowed, no limits", which is the
  default state of the original (`TA-DEMOS.md` finds `0xffff0101` on every entry bar one). A
  host-side unit restriction file (the format the recorder's `unitid.txt` documents) is a TA
  feature RWE lacks; nothing in RWE's own skirmish setup has it either.
- **The limit direction.** Limits are enforced by the *receiver's* definitions after
  `ApplyToUnitTypes`, i.e. by the joiner's own TA; RWE cannot enforce a limit it does not
  apply to units a joiner owns (owner-authoritative, `TA-NETWORK.md`). A limit set on the
  RWE side would only constrain RWE's own units, through a field RWE does not have per game
  (the definition's `UnitLimit`, if any, is data, not a lobby setting).
- **Apply: not applicable on the RWE host**, which owns no TA-style definition flags; the
  availability result would be applied to RWE's own build menu only if RWE honoured the chosen
  restrictions for its own player, which it does not.

## 7. The resend helper, and the report dispatch (not applicable)

**Reference.** A separate sequencer numbers outgoing records, keeps them, and answers a subtype
`0x65` request by resending the stored copy; receiving delivers exactly the next id and then any
contiguous held ids, appends later ids to a held list and requests every missing id from the
next one up to the maximum seen (the requests leave the last eight bytes uninitialised). Held
duplicates are not removed, nothing is pruned and there is no timeout or gap bound. **The live
catalogue path does not use it**: the receive routine zeroes the id and dispatches subtypes below
one hundred directly. The report functions (`ReportGameEvent`, `ReportGameChat`, the interval
timer) copy the session GUID and the trimmed session name and feed an online statistics
interface and an optional `reporter.dll`; with the HAPINET route's flags query returning zero
the first route is switched off and the DLL's result is ignored.

**RWE: not applicable.** RWE ignores a subtype-`0x65` request (it never sends one, and a TA
joiner's live handshake does not either, per the chapter). The report path has no counterpart:
RWE reports a finished game through its launcher and master server, not through `reporter.dll`
(`sendGameEnded` in `GameScene`, `BT-FRONTEND-ENDGAME-PARITY.md`).

---

## 8. Gaps to fix

Ranked by what could hurt a TA game. None touches the simulation's hashed state.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **RWE never checks the joiner's unit table.** A joiner whose definitions differ is told everything is available, and RWE then resolves its type indices through its own load order: wrong or missing units with no warning. | DIVERGENCE (documented as "echo"), with a visible consequence | code: record (index, key) from subtype 2, compare with `taUnitTypeChecksum` of RWE's FBI at that index, answer availability zero and tell the host player |
| 2 | **The 512-id cap is below the largest known set (549).** | DIFFERS | code: raise the bound to about 600 |
| 3 | **The launch does not wait for the joiner's catalogue to finish**, and the original has no timeout either. | MISSING (host rule) | code: a bounded wait for the joiner's `1 + 2m` acknowledgement; UNCLEAR whether TA's own Start is gated |
| 4 | **`TA-NETWORK.md` calls `0x0101` "in use"**; it is permission and availability, and `0x0001` is "not yet verified". | register wording | edit the wording; keep the capture facts |
| 5 | **The script checksum's consumer is identified** (the authority's availability check, for a remote at version 2 or 1.2 and above), closing `BT-GAME-DATA-FILES-PARITY.md` gap 7's question. Joining a TA host will need it unless the announced version is older. | continuation | spec-room read of the version write (see `BT-NETWORK-HAPINET-PARITY.md` rule 8) |
| 6 | **No restriction editing.** | MISSING | decision |
| 7 | **The fixed pseudo-entry and the sub-2/sub-3 length difference** remain unexplained; duplicate keys are one lead. | UNCLEAR | a corpus check for equal-key pairs in a data set |

No source file was changed.
