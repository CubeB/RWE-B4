# Command permission masks, the debug phrase, the repeat key and the default handler

**Issue:** #906. **Date:** 2026-10-27. **Evidence class:** static reference
reading of the byte-tactics decompilation. No original executable, demo oracle
or RWE build was run. The RWE half is a read-only source audit.

This finding settles the two open questions recorded in
`BT-GAME-CONSOLE-COMMANDS-PARITY.md` section 2 and section 10 rows 4 and 8, and
checks the claims in `TOTALA-EXE-COMMANDS.md` section 124 against the
decompilation. It changes no existing register.

## Verdict in brief

1. The cheat bit is **not** granted for every non-campaign game. Campaign
   selects mask 1. Skirmish selects mask 1 plus the cheat bit (mask 3).
   Multiplayer selects the **host's** "Cheat Codes" setting: mask 1 when the
   host's bit is off, mask 3 when it is on. The lobby option therefore does
   suppress cheats in multiplayer, but only until the debug bit is set.
2. The debug bit does not add mask 4. It replaces the whole mask with 7, which
   includes both the cheat and the debug bits, in every mode. A campaign player
   or a multiplayer guest can therefore unlock every table by entering the
   exact phrase, or by starting with the registry pre-set described below.
3. The phrase is exactly six tokens: `Now Film Chris Include Reload Assert`.
   The command word is case-insensitive; the five following words are
   case-sensitive. Any other invocation of `Now` clears the debug bit; no other
   command does.
4. The repeat key is virtual-key code `0x5C`, active only while the debug bit is
   set, and it runs the saved line with mask `-1` (all bits). The saved line is
   re-tokenised, is capped at 79 characters, and an empty saved buffer does
   nothing.
5. A mask-1 caller never reaches the mask-4 default handler, and the original
   prints no error text for an unknown word. With a mask containing bit 4, an
   unknown word is offered to the default handler, which may spawn units or run
   a debug script, but still prints nothing.

## Reference and evidence

- **Reference revision:** `e89721c46c3fa06e59558419b54fe26061fafb73` in
  `D:\bytetactics` (read-only, clean worktree).
- **Reference binary:** `D:\bytetactics\orig\TotalA.exe`, 1,178,624 bytes,
  MD5 `8E74A1DFFA1F5988624C52048F5B20CD`, SHA-256
  `3B9C0FADABF3DC67ED5F05A70F1E1505A0C65DEADD1A3C930ADFE30E2A84995E`.
- **Read:** the decompiled routines behind the addresses cited below; the
  symbol/global tables for addresses, sizes and initial values; and the
  executable's data section for the battle-room gadget-name list. No other
  byte-tactics tree was opened.
- **Not run:** the original executable, any shipped-data scenario, the demo
  oracles, the RWE build and the RWE tests. Every statement below is a static
  reading. Where a fact could only be established by execution it is listed in
  "Remaining uncertainty".

## 1. The cheat bit, the campaign test and the lobby option

### 1.1 What the talk dialog actually computes

The talk-dialog handler is at `0x493BF0`. It selects the mask at the moment a
`+` line is submitted, not when the dialog is opened. The sequence is:

1. start with mask 1;
2. if bit 1 (value `0x0002`) of the 16-bit option word at game block
   `+0x37F2F` is set, **replace** the mask with 7;
3. if the 32-bit word at `0x5091CC` is nonzero, OR in bit 2.

So the mask presented to the line runner is always one of 1, 3 or 7. The debug
step is not `mask |= 4`; it is `mask = 7`, so it grants the cheat bit as well.

### 1.2 What sets the word at `0x5091CC`

The game-start routine at `0x497180` switches on the mission type and writes
the word:

| Mission type | Meaning | Value written |
| --- | --- | --- |
| 1 | campaign | 0 |
| 2 | skirmish | 1 |
| 3 | multiplayer | 1, then overwritten after the host slot is resolved |

In the multiplayer arm the routine resolves the host slot through the host-bit
search at `0x456850` (the first active player whose flag word at `+0x97` has
bit 0 set), loads the mission, re-resolves the host, and then copies **bit 13
of the host's per-player flags word at `+0x9B`** into `0x5091CC`. Bit 13 of
that little-endian 16-bit word is the byte at `+0x9C`, mask `0x20`. If the host
slot cannot be resolved the routine breaks out before the copy.

The decompilation shows only one reader of `0x5091CC`: the mask selection at
`0x493BF0`. The word is therefore not a general "campaign or not" flag with
side effects; its only use is this permission decision, and in multiplayer it
is really "the host's Cheat Codes bit".

### 1.3 Which field is consulted

The field is the **host's** per-player record, not the local player's. On a
guest, the guest's own record is not read for this decision. This corrects the
wording in `TOTALA-EXE-COMMANDS.md` section 124, which calls it the local
player's colour record.

The field is the battle room's "Cheat Codes" button (the caption recorded in
`BT-GUI-GUI-PARITY.md` rule 6), whose gadget name is `CHEATING`. The
executable's data section at `0x505518` holds the seven-name host-only gadget
list; its entry at `0x505570` is `CHEATING`. The battle-room
click handler at `0x447B10` toggles the local player's bit, and the display
routine at `0x445ED0` shows the host's value. Because the gadget is in the
host-only list, a guest cannot change it in a normal lobby; only the host's bit
is copied into `0x5091CC` at game start.

A separate online configuration file can also seed a player's own bit when the
battle room is entered, but again that writes each player's own record. In
multiplayer the value that matters is still the host's. In skirmish the mission
type forces the value to 1 and no cheating field is consulted; the hidden
"cheat" text handler in the skirmish screen is a player-count shortcut, not a
permission setting.

### 1.4 The debug bit defeats the lobby gate

Bit 1 of `+0x37F2F` is the debug bit. It has two static writers:

- the `Now` handler at `0x416E90` (section 2);
- the settings-load routine at `0x42F9A0`, which sets it from the registry
  DWORD `Games` when the display-depth value is `0x100`, and clears it
  otherwise.

When the bit is set, the talk dialog replaces the mask with 7 before the
campaign/cheat test. The cheat bit is therefore available in a campaign, and in
a multiplayer game whose host left "Cheat Codes" off, as soon as the bit is set.
The lobby option gates the default state, not a determined player who knows the
phrase. This is a property of the original, not a recommendation for RWE.

The same bit also gates the movie-capture key `0xD7`, the debug end-game key
`0xEC` and the repeat key `0x5C` in the key handler at `0x495E90`, which is
consistent with its role as the general debug unlock.

### 1.5 Resulting masks

| Situation | Source of the cheat decision | Debug bit | Mask |
| --- | --- | --- | --- |
| Campaign | mission type 1 writes 0 | clear | **1** |
| Campaign | mission type 1 writes 0 | set | **7** |
| Skirmish | mission type 2 writes 1 | clear | **3** |
| Skirmish | mission type 2 writes 1 | set | **7** |
| Multiplayer, host | host's `+0x9B` bit 13 = 0 | clear | **1** |
| Multiplayer, host | host's `+0x9B` bit 13 = 1 | clear | **3** |
| Multiplayer, guest | the same host record | clear | **1** or **3** |
| Any mode | any | set | **7** |
| Repeated line | any | must be set | **-1** (all bits) |

The debug bit is per client; the cheat bit is host-wide in multiplayer. A guest
who sets the debug bit locally runs with 7 while the host and the other guests
may still be on 1. That asymmetry matters for RWE's lockstep (section 6).

## 2. The debug phrase

The `Now` handler at `0x416E90` is in the ordinary table (mask 1), so it is
reachable in every mode with the base mask, including campaign.

- It requires **exactly six tokens**. Token 0 is the command word; tokens 1 to
  5 must be `Film`, `Chris`, `Include`, `Reload`, `Assert`, in that order.
- The command word is matched case-insensitively by the table lookup, so
  `Now`, `now` and `NOW` are the same command.
- Tokens 1 to 5 are compared with a case-sensitive string compare. `film`,
  `Filmx`, or any other spelling fails.
- On success it sets bit 1 of the 16-bit word at game block `+0x37F2F`. It
  prints nothing and does nothing else.
- On any other invocation — wrong count, wrong case, missing word, or merely
  `+Now` — it clears bit 1 (`word &= ~2`). It does not clear any other bit.
- No other command clears or sets bit 1. The settings-load routine at
  `0x42F9A0` can also set or clear it at startup, as described above.

Because the mask is recomputed per line, the phrase takes effect on the
**next** `+` line, not on the phrase's own line. In campaign the phrase is
typed under mask 1, and the following line is typed under mask 7.

## 3. The repeat key

The key handler at `0x495E90` handles virtual-key code `0x5C` (the backslash /
pipe key on a US layout) and does one thing:

- if bit 1 of the word at `+0x37F2F` is set, call the line runner at `0x417B50`
  with a null line pointer and mask `-1`;
- otherwise the key does nothing on this route.

Mask `-1` is all 32 bits set. It is not a masked-down value, and it is not the
caller's current talk mask. A repeated line therefore reaches the ordinary, cheat and
debug tables and the mask-4 default handler regardless of the campaign or lobby
gate. The key is a second bypass, independent of the phrase; it only needs the
debug bit.

The line runner behaves as follows:

- A non-null line is copied into the 80-byte buffer at `0x511BD0` with a
  **79-byte** copy (`0x4F`), so the saved copy is at most 79 characters. The
  copy itself does not write a terminator; the final byte of the buffer lies
  outside the copied range and is zero in the shipped image, so the repeat sees
  a 79-character line at most. The immediate execution is not limited to 79
  characters: it tokenises the caller's original pointer.
- A null line uses the saved buffer instead of copying anything.
- In both cases the text is freshly tokenised. The tokeniser at `0x4B7440` has
  a 126-byte (`0x7E`) token buffer and keeps at most 20 tokens. A repeat is a
  new parse of the saved text, not a replay of the first parse's arguments.
- An empty saved buffer tokenises to zero tokens; the dispatcher then does
  nothing and returns 0. A null line therefore repeats the last saved line when
  there is one, and is silent otherwise.

The saved line is the last line submitted through `0x417B50`, which in the
shipped executable is the talk dialog's `+` path. The repeat key calls the same
runner with a null line and does not change the buffer. The script runners used
by `Include` and by the default handler use a different entry point (`0x4B7A30`)
and do not update this buffer.

## 4. The default handler and the unknown word

Command registration at `0x419560` registers the three tables and installs the
default handler through the setter at `0x4B78E0` with **mask 4**:

| Table | Address | Entries | Mask |
| --- | --- | --- | --- |
| ordinary | `0x501D38` | 43 plus terminator | 1 |
| cheat | `0x501F48` | 10 plus terminator | 2 |
| debug | `0x501FD0` | 30 plus terminator | 4 |

The dispatcher at `0x4B7900` lowercases token 0, looks it up case-insensitively
in the sorted table, and:

1. runs the entry only when its mask intersects the caller's mask, returning
   that entry's mask;
2. otherwise runs the default handler only when the default mask (4) intersects
   the caller's mask, returning 4;
3. otherwise returns 0.

A mask-1 caller (campaign, no debug bit) therefore never reaches the default
handler: `4 & 1 == 0`. A mask-3 caller (skirmish, or multiplayer with the host's
cheat bit on) also never reaches it: `4 & 3 == 0`. Only masks 7 and `-1`
contain bit 4. Because every registered table mask is a subset of 7, in this
executable the default handler is reached **only for a name that is not in any
table**, never for a known name whose mask was denied.

The default handler at `0x417890` does not print an error. It matches token 0 as
a wildcard against internal unit names and creates units for the owner in
argument 1 (default 0), or, if nothing matched, tries `debugdat/<token>.txt` and
executes it with all bits. Both paths are silent.

For an unknown word, therefore:

- with mask 1 or 3: nothing runs, nothing is printed, the dispatcher returns 0;
- with mask 7 or `-1`: the default handler may spawn units or run a script, but
  still prints nothing.

For an unknown word the only visible text is the chat echo: after the command
attempt the talk dialog still sends the original line, including the `+`,
through the chat sender at `0x463E50`. A player therefore sees a chat line
containing `+foo` (and, if a cheat handler ran, it is sent to everyone because a
result with bit 2 sets the chat mode to 0). That is chat, not an error message.

**Cosmetic conclusion:** the original has no "unknown command" text to match.
RWE's `Unknown command: <word>` is an RWE-only message and should be recorded as
a deliberate divergence if it is kept.

## 5. RWE audit

Read-only audit of `src/rwe/game/chat_command.*` and
`src/rwe/game/GameScene_chat.cpp` at the 2026-10-27 working tree. Nothing was
built or run; line numbers are approximate.

The parser is a hand-written chain rather than a registration table, but it is
still a single list of **eight names, with no masks, no `Now`, no default
handler and no repeat key**:

| Word | Kind | What `runChatCommand` does | Lockstep path |
| --- | --- | --- | --- |
| `ShareMetal` | `ShareMetal` | prints the new state, submits a metal toggle | `PlayerShareCommand` |
| `ShareEnergy` | `ShareEnergy` | prints the new state, submits an energy toggle | `PlayerShareCommand` |
| `ShareAll` | `ShareAll` | prints both states, submits both toggles | `PlayerShareCommand` |
| `ShareMapping` | `ShareMapping` | prints the "no toggle" notice; submits nothing | none |
| `ShareRadar` | `ShareRadar` | prints the "not available" notice; submits nothing | none |
| `SetShareMetal` | `SetShareMetal` | prints the typed number, submits the reserve | `PlayerShareCommand` |
| `SetShareEnergy` | `SetShareEnergy` | prints the typed number, submits the reserve | `PlayerShareCommand` |
| `Give` | `Give` / `GiveUsage` | resolves a name or `Player n` from one, refuses self, submits the gift | `PlayerShareCommand` |

Every known command is refused unless the game is networked, has remote peers
and the local player is human, with `That command only works in a network
game.` The unknown branch is checked first and prints `Unknown command: <word>`
even in single player. There is no mask-dependent routing, no command-line
buffer, no history and no key handling for a repeat. Command lines are never
sent as chat, which is the opposite of the original's echo and is already an
RWE design choice (`BT-INGAME-DIALOGS-PARITY.md` rule 3, BT-177).

## 6. Implementation contract

A build that claims console parity needs the following. A build that keeps only
the sharing subset still needs the same rules for any command it adds; it does
not need to register tables it does not implement.

### 6.1 Selection and dispatch

- Mask constants: ordinary 1, cheat 2, debug 4. The default handler's mask is 4.
- Per submitted `+` line: start at 1; if the local debug bit is set, use 7;
  otherwise, and only if the mode grants it, OR in 2. Do not derive the cheat
  grant from "not a campaign" alone. The correct sources are: campaign 0,
  skirmish 1, multiplayer the host's replicated Cheat Codes bit.
- Look the command word up case-insensitively and exactly. Run it only when
  its registration mask intersects the caller's. Fall back to the default
  handler only when the default mask intersects the caller's. Return the mask
  that ran, or 0.
- The default handler is not an error reporter. If RWE keeps
  `Unknown command`, that is a separate, documented message.
- The debug bit is local and per client. It must not be replicated, hashed,
  saved or replayed. The same is true of the selected chat mode and the repeat
  buffer.

### 6.2 What must remain lockstep-safe

The mask, the phrase, the repeat key and the chat echo are presentation. None
may enter the command stream, the hash, a save or a replay.

Every command whose effect touches hashed state must be a replicated command,
submitted from the local player's command buffer and applied on every peer in
the same tick. The existing `PlayerShareCommand` path is the model. At minimum
this covers:

- player resource writes: `Give`, `NoMetal`, `NoEnergy`, `ATM`, and the
  `SetShare*` reserves (already replicated);
- player fields and per-unit flags: `Logo`, `View`, `Selectable`, `AI`,
  `Control`;
- unit existence, types, orders and completion: `Kill`, `IWin`, `ILose`,
  `Assign`, `Reload`;
- map, feature and pathing state: `Feature`, `BurnAll`, `BurnOne`, `SeaLevel`,
  `Edge`, `Search`;
- per-player visibility: `LOS`, `Mapping`, `NowISee`;
- weapon behaviour: `DoubleShot`, `HalfShot`;
- packet-loss simulation: `Senderror`;
- simulation events: `Meteor`.

Local-only effects (`Gamma`, `Shading`, `Shadow`, `Now`, `Film`, `MemDump`,
`DPrint`, the empty debug bodies, and so on) may stay local, but they must not
write hashed state. The original's "the local machine only, nothing replicates
it" model is not safe here: RWE has no owner-authority overwrite to repair a
guest's local edit. A local `+ATM` in RWE would be a desync on the first tick.

### 6.3 Decisions this finding leaves to the owner

1. **Expose the cheat table at all?** RWE has no divergent control/view state,
   no meteor shower, no poster capture and no per-player map-visibility
   toggle. A defensible choice is to implement the gate and the repeat key but
   not the ten cheat callbacks, and to record that as a divergence.
2. **Honour the original's debug-bit override, or close it?** The original lets
   the phrase and the repeat key defeat the lobby's Cheat Codes setting. RWE
   may instead make the host's option authoritative. Either way it is a
   deliberate behaviour departure and belongs in the divergence register.
3. **Echo `+` lines as chat, or not?** The original does; RWE currently does
   not. BT-177 records the original's behaviour.
4. **Repeat key and truncation.** Reproduce key `0x5C`, the `-1` mask and the
   79-character saved buffer, or provide a safer full-line history. The
   original's behaviour is a permission bypass and a truncation bug.
5. **`Unknown command`.** Keep RWE's message (documented divergence) or stay
   silent like the original.
6. **The `Now` phrase.** If RWE implements it, it must match the exact six
   tokens and case sensitivity; if RWE does not, the debug table has no runtime
   route and that must be said in the register.
7. **The registry `Games` pre-set.** Reproducing a hidden registry switch that
   enables the debug bit is not recommended; it is recorded here because it is
   a second route in the original.
8. **The mask-one commands that write simulation state** (`NoMetal`,
   `NoEnergy`, `Selectable`, `Logo`, `Give`) remain the open conflict in
   `BT-GAME-CONSOLE-COMMANDS-PARITY.md` row 1. If any is built, it is a
   replicated command, not a local console handler.

## 7. Remaining uncertainty

**Executed checks: none.** All of the following are static inferences from the
read-only decompilation.

- The host's Cheat Codes bit reaching every peer identically at game start was
  read from the lobby broadcast and the game-start copy, not observed with a
  real host and guest.
- The claim that the 80th byte of the saved buffer is zero relies on it being
  BSS padding that no routine writes. The copy itself is deliberately
  unterminated, so a different loader or a future write to that byte could make
  a repeat read past the buffer.
- Virtual-key `0x5C` is the backslash/pipe key on a US layout. The code tests
  the key code, not the character; the physical key on another layout was not
  observed.
- The registry `Games` route at `0x42F9A0` requires a particular display-depth
  value and was not exercised in a retail configuration. Its reachability
  through the shipped menus is unknown.
- The chat echo's visibility in campaign depends on the chat sender's
  single-player path; it was read, not run.
- The host-slot search at `0x456850` returns 10 if no host remains, and the
  multiplayer arm breaks before the copy in that case. Host departure, late join
  and host migration were not exercised.
- The decompilation was not searched for patched or community builds. The
  identity above is the only build this finding covers.
- No original runtime check, demo oracle or RWE test was run. A green RWE test
  would not establish parity for any of this.

## 8. What this settles

- `BT-GAME-CONSOLE-COMMANDS-PARITY.md` section 2, first bullet: the lobby
  "Cheat Codes" option **does** gate the cheat bit in multiplayer; skirmish is
  always cheat-enabled; campaign is not; the debug bit overrides all of it.
- Same section, second bullet, and section 10 row 8: the original prints
  nothing for an unknown word. A mask-1 caller does not reach the default
  handler. RWE's message is cosmetic and RWE-specific.
- Same section, section 10 row 4: closed.
- `TOTALA-EXE-COMMANDS.md` section 124: the effect described there is correct,
  but the field belongs to the **host's** record, not the local player's, and
  the mask is read per line, not when the dialog opens. The "mask minus one" is
  literally `-1`, and the 79-byte copy is the repeat limit, not the execution
  limit.
- `BT-GAME-CONSOLE-COMMANDS-PARITY.md` section 3: the repeat key exists, uses
  all bits, and re-tokenises the saved text.
- The default handler's mask-4 entry is a unit spawner and script loader, not
  an error path.
