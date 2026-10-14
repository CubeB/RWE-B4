# Typed commands and resource sharing, as the executable runs them

What a line typed into the chat bar does when it starts with `+`, and how the
original shares resources between allied players: the toggles, the reserves,
the automatic pass and the manual gift.

**Where it comes from.** Added 2026-10-14 from a written account of the
executable's command layer, re-stated in the register's own words (ADR-0004):
behaviour, arithmetic, constants, a packet layout and routine addresses. The
account's claims were read from function bodies by inspection and have not been
replayed against the running original. [`TA-PATCHES.md`](TA-PATCHES.md) already
lists the commands as not implemented; the strings are present in the binary
(`ShareEnergy` three times).

## 124. Chat commands and sharing

**Entry.** The talk dialog skips leading spaces; if the first remaining
character is `+`, the rest is handed to the command interpreter locally, with a
permission mask. Ordinary commands carry mask 1; the mask gains bit 2 (cheats)
in non-campaign play and further bits through a UI option. The interpreter
tokenises on whitespace and stops at `#`; it has no quoting; it keeps at most 20
tokens in a buffer of about 128 bytes. The command word is looked up
case-insensitively in a sorted table and runs only if its registration mask
intersects the caller's. An integer argument that is missing takes the
handler's default; one that is present goes through `atoi`/`atof` with no
validation (`0x417B50`, `0x4B73E0`..`0x4B7A30`).

**The sharing commands.** Every one requires the net-game flag (bit 0 of the
network flags) and does nothing outside a network game. They act on the
*local* player, never on a debug camera's. The four preference toggles live
in one byte of the replicated player record (`+0x97`):

| Command | Effect |
|---|---|
| `+ShareMetal` | flips bit `0x02`, prints the new state, broadcasts the player record |
| `+ShareEnergy` | flips bit `0x04`, prints, broadcasts |
| `+ShareMapping` | flips bit `0x20`, prints, broadcasts |
| `+ShareRadar` | flips bit `0x40`, prints, broadcasts |
| `+ShareAll` | the four flips above, in that order, as four independent toggles |
| `+SetShareMetal n` | sets the metal **reserve** to `min(metalStorage, float(atoi(n)))` |
| `+SetShareEnergy n` | the same for energy |

A toggle takes no argument: `+ShareEnergy off` flips it like `+ShareEnergy`.
`+ShareAll` inverts each bit separately, so with mixed starting states it does
not mean "all on", and it does not touch line-of-sight sharing. The reserve
setters do not enable sharing, do not broadcast, accept a negative number
(stored as is), take a missing argument as 0, and **print the number that was
typed, not the capped number that was stored**. The reserves are absolute
amounts, never percentages. A handler that flips line-of-sight sharing (bit
`0x08`, `0x418F10`) exists but is not in the registered table, so `+ShareLOS`
does nothing. **[open]** the byte's bit assignments collide with two other
readings in `TOTALA-EXE-DATA.md` for bits `0x04` and `0x08`; check the binary
before naming a bit anywhere RWE persists it.

**The automatic pass** runs from the game step for the local player, only in a
network game, and only when `tick % 60 == 0` (every two seconds). For metal and
for energy independently:

1. it requires that resource's sharing bit and a stored amount above the
   reserve;
2. it scans slots 0 to 9 for a recipient: an active player that is a remote
   human (player type 3, player-info kind 1), still in the game
   (`unitCount != 0` or `unitsCreated == 0`), allied to the donor according to
   the donor's alliance table, and holding **less of the resource than the
   donor holds**;
3. it keeps the **last** slot that qualifies; there is no comparison with the
   earlier candidates, so the comment's "poorest ally" is not what happens;
4. it offers one third of the surplus over the reserve for metal and one half
   for energy, capped at the recipient's remaining free storage, and moves it
   with the sending-side processing on.

So a donor with 1800 energy and a reserve of 1000 offers 400; if the last
eligible ally has 100 free, it gets 100 even if an earlier ally has far more
room. A computer player is never a recipient of this pass (`0x457D30`).

**Mapping.** When the mapping bit is set, the same pass also runs at
`tick % 450 == 0` (fifteen seconds): each eligible remote ally is sent a
type-`0x16` packet, subtype 3, amount 0, and the receiver merges the sender's
explored map. It moves no resource. Line-of-sight, vision and radar sharing is
a *separate* mechanism, in eleven-entry per-player arrays used by the network
view-state handshake, and the radar bit changes which contacts the radar
presentation accepts; none of it is a flag that makes allied cells permanently
visible. **[open]** what a default alliance shares without any toggle is not
settled by this account, and RWE's teams share sight and radar unconditionally.

**Manual gift.** `+Give player amount metal|energy` requires an active player
in slot 0..9 of type 1, 2 or 3 and **does not require that the recipient be an
ally**. The amount is an integer converted to float; negative amounts are not
clamped. The transfer is capped at what the donor stores, recorded as a spend
for the donor, and credited to the recipient's income account (which settles
into its stock at the next economy second). When the recipient is a local
computer player the credit is scaled by the handicap, 0.5 easy and 0.7 medium
(§123), while the donor's debit and the packet carry the unscaled amount. The
share dialog uses the same transfer.

**The packet** (`0x16`, 17 bytes): offset 0, one byte, the type; 1, four
bytes, the subtype (1 energy, 2 metal, 3 mapping request); 5, four bytes, the
sender's network id; 9, four bytes, the recipient's; 13, four bytes, the amount
as a float's bit pattern (0 for a mapping request). The receiver resolves the
ids to players and applies the same transfer with the sending side switched
off, so there is no second debit and no echo. **The two resource subtypes are
the reverse of their names in the source**: the routine called "metal" moves
energy accounts and the one called "energy" moves metal; the field offsets and
callers settle it, and a new implementation should name them by what they
move.

**What a save keeps.** The player block saves resources, statistics, storage
bonuses and alliances. It does **not** save the sharing byte or the reserves,
so neither survives a load in the original.

**Other registered commands** (mask 1, effects are options and display and
none touches the simulation): `NoShake`, `Contour`, `ScrollSpeed`, `IFace`,
`CDPlay`, `CDStop`, `Sound3D`, `Shading`, `AntiAlias`, `Shadow`, `Dither`,
`SwitchAlt`, `TShadow`, `FShadow`, `LOSType`, `Light`, `RCache`, `Selectable`
(sets selectable on **every** live unit), `MusicMode`, `Logo`, `ScreenChat`,
`Gamma`, `Clock`, `NetStats`, `Sing`, `NoMetal`, `NoEnergy`, `BigBrother`,
`Now`, `Drop`, `ShootAll`, `ShowRanges`, `Compression`, `BPS`, `SFX`. The
cheat table (mask 2: `Radar`, `ATM`, `View`, `LOS`, `Mapping`, `DoubleShot`,
`HalfShot`, `NowISee`, `Meteor`, `MakePoster`) and the debug table (mask 4)
are not specified here.
