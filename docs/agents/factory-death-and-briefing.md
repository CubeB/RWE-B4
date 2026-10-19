# Factory destruction and briefing panorama corrections

Owner reports, 2026-10-19. Prose findings below supplied by the dedicated
spec room in D:/RWE-spec-room, commits b25ccb62 (BT-266) and 74c726f9
(BT-267). Implementation never reads the decompilation.

## BT-266 Factory destruction cancels its retained product through silent cause 9

**Question.** When a factory, including an advanced kbot lab, dies during
construction, what happens to its child? Distinguish unfinished construction,
completion/release, mobile builders and a product killed independently.

**Reference and method.** Static spec-room reading on 2026-10-19 of
byte-tactics `97d7ac59b460417bcdfa629cf90da99f31818c65`, matching retail GOG
v3.1 `TotalA.exe`. Read `TOTALA-EXE-ECONOMY.md` sections 93 and 134 and
BT-45/BT-260 before the reference. Read-only shipped ARMALAB/CORALAB FBI checks
confirm the reported advanced-lab content exists; no special advanced-lab
exception appears in the generic factory mission examined here. No RWE
source/tests inspected or edited; no build, original game or demo oracle run.

**Fate of the in-progress product.** Factory death clears its primary and
secondary order lists while the dying factory is still a live unit. An active
factory construction order is destroyed before generic attached-child cleanup.
Its cancellation notification, armed while it sleeps with a product reference,
requests the ordinary completion operation on that product and then applies
**30000 damage with death cause 9**, credited as coming from the factory.
This is the original's deliberate cancellation/removal path, not an abandoned
nanoframe left behind to decay. It applies to the product referenced by the
active factory order; it is not a search for all unfinished units near the
factory or inside its yard.

The completion operation first sets remaining build fraction to zero, marks
the construction transition and detaches an eligible mobile product from the
factory's attachment list. The subsequent cause-9 damage marks a normal
locally simulated product for pending death. The request follows ordinary
unit-damage gating; it is not an unconditional immediate erase. In particular,
a product already marked for death does not accept a replacement damage cause,
and extreme mod health values were not established as covered by 30000 damage.
For the normal cancellation death, cause 9 produces no Killed script, no corpse,
no death-weapon explosion and no kill/loss or veterancy credit from the ordinary
weapon-kill paths. The artificial completion before removal is bookkeeping,
not delivery of a usable finished unit to the player.

**Damage/activation clarification, 2026-10-19.** The 30000 cause-9 request
enters ordinary direct unit damage, not weapon-table/falloff calculation.
At exactly 30000 it bypasses the activated armored-state damage multiplier,
whose gate applies only below 30000. It still applies the victim's veterancy
reduction: with `v = min(floor(victimKills/5),5)`, the delivered amount is
`(25-v)*30000*4/100`, ranging from30000 to24000. The factory's own veterancy
is not added as a weapon-damage bonus. Thus it is neither raw unconditional
HP subtraction nor guaranteed removal of every possible completed/mod product.
A normal unfinished product with no kills takes30000. The completion operation
before that damage **preserves current HP**; it does not heal to maximum or
apply the missing build work to health.

Artificial completion does more than just clear the progress fraction: it
resets the model's construction-draw field, marks the transition, detaches the
eligible mobile product, refreshes relevant builder/selection presentation and
sends the completion/link notification for a locally simulated owner. It also
honors `activatewhenbuilt` through the ordinary activation setter. On an actual
inactive-to-active change that setter starts Activate and requests the unit's
sound slot3 (BT-66); repeated activation without a state change adds neither.
The artificial path does not request the factory's normal finished-product
speech/sound slot8; that request resides in its separate ordinary finish phase.
No generic player-facing unit-finished event/sound is established merely by
the completion bookkeeping call. Pending cause-9 removal follows afterward,
without retroactively removing the completion's documented state/activation
requests. Evidence: `0x41B8D0`, `0x489BB0`, `0x489CE0`, `0x48B090` and
the distinct completion versus cancellation paths of `0x402640`. Static only.

**Release boundary.** Cancellation tests whether the order still retains its
product reference; it does **not** require a positive remaining-build fraction.
A zero-fraction product still retained by an armed cancellation order is
therefore subject to the same silent cancellation. However, ordinary build
completion already calls the completion/detachment operation when the last
build work reaches zero. The factory's finish phase then calls completion
again, clears its product reference and decreases the queue count. After that
reference is cleared, factory death does not cancel the delivered product,
even if it is physically still standing in or walking out of the factory.
There is no extra original rule to kill every finished unit until it crosses
the yard boundary. The command driver can advance through nonwaiting mission
phases in the same update; these logical boundaries must not be assumed to
last an extra simulation tick each.

Pending lethal damage and teardown are also distinct. The unit pass can run
orders before its pending-death cleanup. Work that actually completes and
clears the product reference before the factory is torn down has crossed the
boundary above; the original does not freeze all progress merely because a
lethal hit was queued. This finding supplies the boundary, not an authorization
to change unrelated unit-pass death timing (BT-260).

**Relationship cleanup.** Completion detaches the factory product before the
factory's generic cargo-death loop. Hence the normal construction-cancellation
product takes cause 9 rather than transport-parent-death cause 6. The order
reference is unlinked by order destruction, the product's own references are
cleared at its eventual death, and the factory's remaining incoming references
are notified and nulled before its unit slot is cleared. No surviving child
may retain a live attachment to a removed factory. Generic cargo that remains
attached follows its separate parent-death rule; this finding does not replace
that rule or classify every nearby unit as cargo.

**Cancellation ledger.** The factory cancellation also writes truncated
`builtFraction * metalCost` to the dying builder's per-second metal-production
accumulator, with the AI-owner difficulty scaling already specified in economy
section 134, and refunds no energy. It is not a direct surviving-player bank
credit. The prior register leaves collection of that dying accumulator
unconfirmed; this follow-up does not upgrade that uncertainty into a paid
refund. Correcting product death does not authorize an extra economic refund.

**Mobile builder and external product death.** Mobile and aircraft-mobile
build-order cancellation does not run the factory product-kill path: its frame
remains and follows unfinished-frame behavior/decay from section 93. The old
section-134 phrase calling `0x41C110` a detach operation is imprecise: that
routine refreshes selection/order presentation; it does not itself kill or
complete the target. Order/reference cleanup is what ends that mobile build
relationship. Separately, if the factory product dies from outside, its
reference is nulled by death cleanup; the factory's construction phase sees no
product and returns the flush result, clearing its entire primary build/rally
list as section 134 records. That is not the same as the factory dying and
silently cancelling its still-live product. An unfinished frame killed by
ordinary external weapon damage retains BT-45's distinct piece-script behavior;
it must not be assigned cause-9 silence merely because it was under construction.

**Evidence.** `0x4866D0` (factory order deletion before attachment/cargo cleanup
and final slot clear), `0x439EB0` / `0x43A1F0` (order deletion and armed
cancellation notification), `0x402640` (factory cancellation and completion
phases), `0x41BA60` / `0x41B8D0` (final progress and completion/detachment),
`0x489BB0` / `0x489CE0` (cause-9 damage and pending-death gate), `0x4864B0`
(cause-9 script/corpse suppression), `0x48AAC0` / `0x48AB70` (attachment
unlink), `0x489740` (incoming reference clear), `0x43B7C0` (mission phase
advancement/flush), `0x403A20` / `0x413D80` (mobile cancellation), `0x41C110`
(selection refresh). Only prose, arithmetic and addresses cross the room.

**Confidence and uncertainty.** High for normal factory child cancellation,
the retained-reference boundary, cause-9 side effects and relationship ordering.
No runtime factory-death or near-completion trace was collected. Dying-refund
accumulator collection, exotic owner modes and mod products surviving the
fixed damage amount remain unconfirmed; no special T2 behavioral rule is inferred.

**Implies an RWE change?** Supports the owner's reported factory-child cleanup
fix, with delivered-product survival and mobile/external-death distinctions.
Hash-visible; it does not authorize unrelated refund or generic cargo changes.

**Original-game check advisable?** Destroy a working advanced lab, then repeat
just after product completion and compare the referenced-product boundary.
None was run; static comparison only.

---
## BT-267 Briefing panorama wall-clock scroll, native-width ring and side mask

**Question.** Establish the exact timing, geometry, wrap/reset, clipping and
mask order behind the briefing panorama. The earlier campaign-menu audit says
one pixel every two game ticks but leaves implementation-critical details open.

**Reference and method.** Static spec-room reading on 2026-10-19 of
byte-tactics `97d7ac59b460417bcdfa629cf90da99f31818c65`, matching retail GOG
v3.1 `TotalA.exe`. Read-only inspection of installed `guis/MSNBRIEF.GUI`,
`anims/greenbrief.gaf` and `anims/lavabrief.gaf` in `totala1.hpi`, plus the
copies of the two GAFs in `ccdata.ccx` and `btdata.ccx`. No RWE source/tests
inspected or edited; no build, original briefing observation or demo oracle.
This refines `BT-FRONTEND-CAMPAIGN-MENU-PARITY.md` section 6; it does not
change the separate planet animation's millisecond deadline.

**Clock and strict deadline.** Panorama movement uses the application's
wall-clock counter, obtained by scaling milliseconds since boot to **30 ticks
per second**. It is not the simulation's current game tick. The callback
increments horizontal scroll by **one native pixel** only when the current
wall-clock tick is **strictly greater** than the saved deadline, then sets
the deadline to the freshly read current tick plus two. There is no loop to
catch up missed increments and no accumulated fractional movement. With dense
callbacks and no tick-boundary crossing between its separate clock reads,
successive increments are at least **three** wall-clock ticks apart, about
**10 native pixels per second**. Thus describing it as every two simulation
ticks or a continuous 15-pixel/second scroll is imprecise. Slower callbacks
make it slower; one late callback still advances by just one pixel.

**Reset and wrap.** The scroll offset (a signed 16-bit global) and deadline
(a 32-bit global) begin at zero with process initialization. The inspected
briefing entry/setup does not reset them, and the whole-reference source search
finds no other writer beyond this scroll callback. A new briefing widget sets
its own frame index to zero but reuses the persistent scroll/deadline. Each
increment wraps the offset to zero when it reaches the **sum of all frame
widths of the currently selected panorama**. A new, shorter panorama may
therefore start with an old offset until the next due increment wraps it.
No reset-on-briefing-entry policy is established by the reference.

**Ring drawing and direction.** All panorama frames are spatial strips in
sequence order, not alternative time-animation frames. The first is drawn at
`(gadgetX - scrollOffset, gadgetY)`, the next begins immediately after its
actual width, and so on. The drawing includes every frame and **one extra
copy of frame zero** after the last. Advancing the positive offset moves the
image **leftward**. Widths are summed and accumulated individually; the total
is not first-frame width times frame count. Every frame is drawn at native
size, with both embedded origin offsets forced to zero. Heights may differ;
all frames align at the same top Y, without fitting, vertical stretching or
centering. A separate time-derived gadget frame index is written, but the
ring draw still enumerates the whole sequence from frame zero; that field
does not substitute a single rotating frame for the scrolling strips.

The callback fills the panorama gadget rectangle first, but does **not**
install a panorama-local clipping rectangle around its native frame blits.
The ordinary surface clip applies. The subsequent mask supplies the intended
visible window. Extra-small or unusually sized mod strips are not generalized
to arbitrarily many repeat copies: the original draws exactly that single
extra frame zero, even if it would leave a gap in unusual content.

**Panorama fill color clarification, 2026-10-19.** The gadget rectangle is
filled from the current GUI's color-remap table using the gadget's `colorb`
value as the index. It is not an arbitrary literal black. The background index
is stored at gadget offset`0x23`; the GUI remap bytes begin at GUI offset`0x8B2`,
so the effective read address is `GUI base + 0x8B2 + colorb`. The reference's
pointer-shaped expression for that addition must not be mistaken for a
process-address-dependent palette selection. Shipped MSNBRIEF.GUI specifies
`PANORAMA colorb=0`, hence **GUI color-remap entry0**. Briefing setup remaps
GUI colors to the current screen palette before this callback. Use that
remapped palette index; a hardcoded RGB value or unremapped index0 is not
established by the fill call. The rectangle is inclusive from gadgetX,Y to
`gadgetX+width-1, gadgetY+height-1`. Evidence: `0x478790`, `0x478E80`, the
GUI common-field loader and `0x4AA8F0` (palette remap), plus the shipped GUI.
Static only; no rendered color sampled.

**Mask and ordering.** After drawing the panorama ring, the callback looks up
`Panmask` in the current briefing screen GAF and selects the frame by the
**local player's side index** (ARM zero, CORE one for shipped sides). Both
mask origin offsets are forced to zero and it is drawn natively at **(0,0)**.
It is a whole-screen overlay, not a mask scaled into the panorama widget.
The callback draws the wind/gravity readouts and blink words before the
panorama and mask. The planet is a separate callback; in the shipped GUI its
PLANET gadget precedes PANORAMA and the menu updater visits gadgets in ascending
order, so its update/draw is earlier than this panorama/mask callback when
both run in that pass. Generic text/label redraws remain separate GUI work;
this ordering is not a license to replace the entire briefing with one
panorama sprite or to stretch its mask. A full native 640x480 surface clip
corresponds to the shipped original canvas; widescreen framing outside that
canvas is an implementation presentation question, not evidence of an
original wider panorama clip.

**Real data.** Shipped MSNBRIEF.GUI is 640x480. PANORAMA is at (45,130),
size **550x123**; PLANET is at (0,0), size224x107. Both GreenPan and LavaPan
have **four frames of widths 640,640,640,520**, each height122, so the ring
width is **2440**, not2560. Their original frame offsets include X320 and
Y214/215, which the callback discards. PanMask has two **640x480** frames,
each with embedded origins (320,240), also discarded before drawing at(0,0).
The corresponding GAFs differ byte-for-byte across the three inspected
archives, but all have these same panorama/mask dimensions and offsets.
The named GAFs and GUI have no copies in `worlds.hpi` or `rev31.gp3` in this
inspection. Variable panorama width is therefore a shipped-data requirement,
not merely a hypothetical mod case.

**Evidence.** `0x478790` read in full (readouts, clock test, sum, native strip
loop and mask), `0x478E80` (briefing setup and side/sequence selection),
`0x4B62D0` / `0x4B6330` / `0x4B6340` (application tick scale and clock),
`0x49E830` (30Hz initialization), `0x4A9FD0` / `0x4A4B50` (ascending menu
callbacks), `0x4B7F30` / `0x4B7F90` (frame selection and native surface-clipped
blit), globals at `0x51E674` / `0x51E678` (scroll/deadline, source writer
search), and the installed GUI/GAF headers above. Prose, addresses, layouts,
constants and arithmetic only.

**Confidence and uncertainty.** High for wall-clock units, strict deadline,
single-step behavior, persistence, ring geometry, mask selection and shipped
dimensions. No actual scroll rate or screen output was measured. Clock-wrap,
zero-frame malformed sequences and totals exceeding signed16-bit offset range
are outside this finding. A callback crossing a wall-clock tick between reads
can postpone its next increment; no fixed-rate claim overrides that detail.

**Implies an RWE change?** Supports the owner-authorized briefing-panorama fix:
scroll the native strip ring, preserve the side mask and distinguish wall-clock
presentation timing from sim ticks. No sim hash change follows from this
presentation behavior; no unrelated globe/text redesign is authorized.

**Original-game check advisable?** Compare one normal briefing over several
seconds and reenter it to corroborate rate and persistent offset. None was run;
static comparison and shipped-data inspection only.

---## Implementation scope

Preserve current RWE death timing, existing factory cancellation economics and
unrelated globe/text behavior. Factory cleanup uses the retained product, not
a proximity query. Presentation scrolling uses persistent wall-clock state,
native strip sizes and the side mask. Full 640x480 surface clipping protects
RWE widescreen framing; it is presentation infrastructure without an original
OpenGL counterpart, not a new panorama-local clipping rule. New presentation
state does not enter sim hashes or saves. Validation results follow below.

Supplement 9244f448 clarifies direct cancellation damage and activation.
The panorama's preexisting GUI background/fill and wind/gravity omissions are
not redesigned here. Only spatial strips, timing, side mask and surface
clipping change. Factory teardown retains RWE's existing immediate death
model and makes no new bank credit from the unconfirmed dying refund ledger.

## Shipped fixtures and native reproduction

Factory fixtures use totala1.hpi ARMALAB (5x6, HP3005, build13520),
CORALAB (7x6, HP3170, build14000), ARMZEUS (HP875, build5478,
metal267/energy2228) and CORCAN (HP2800, build7500, metal420/energy3500).
ARMCK uses HP700, build5597, WorkerTime80, metal120/energy2410.
Inert COB/model and death media in focused tests isolate cancellation from
blast damage; the native run below uses actual scripts and weapons.

Executed RWE GameScene headlessly on shipped Metal Heck, seed 7, schema 0.
The existing debug spawner adds CORALAB at tick 30. A real queue command at
60 requests CORCAN; a self-destruct command at 900 destroys the lab at 1050.
LARGE_BUILDING (units.tdf) has default 1800 damage, AoE 400; the in-progress
Can survives it in the pre-fix Release build. At tick1050 the old build
retains CORCAN with 260 HP and 5520/7500 work completed. In the fixed Debug
build no Can remains once the lab is removed. Hashes agree through 1049 and
first differ exactly 1050. State samples before destruction show the same
lab and Can in both builds. This is executed RWE behavior, not an original
runtime check. Evidence: D:/tmp/rwe-factory-native-metal/result.json and
before/after state/hash logs. Initial Lava Run debug-spawn attempts were
blocked at the selected footprints and are not counted as reproductions.

The fixed GameScene run repeated with identical hashes for all 1199 logged
ticks, SHA256 e2958534684e6ca8eabcf638eebb47e01a97240dad29eb3e40ce8a0309d62180.
No original executable runtime or game visual comparison was performed.

## Validation

Focused factory-death, panorama, save-load, replay, guard-assist, self-destruct
and reclaim checks passed: 82 cases / 2542 assertions. Fixtures include both
advanced labs, creation and held-completion references, released-product
survival, external child-death preservation and ARMCK frame survival. Native
run resources start at 10000 of each to reach the late construction phase.
The new panorama tests use the actual 2440-pixel shipped ring and verify the
strict deadline, clock-read crossing, single late step, wrap and persistence.

Full allowed suite passed: 1835 cases / 193775 assertions, plus five packet
cases / 33 assertions, totaling 1840 distinct cases / 193808 assertions. Neither
prohibited TaChecksums test was run. Debug game and test executables built.

All Debug targets and the Release game built. The section-budget check passed
for all 1,059 Debug objects (limit 24,575); worst remains GameScene.cpp at
24,276, save_util.cpp at 24,107 and GameSimulation.cpp at 23,277.
FactoryConstruction.test.cpp verifies BT-266; UiPanorama.test.cpp verifies
BT-267. The Release native factory replay also passed, with all 1,199 tick
hashes identical to the Debug run. The --help smoke check exited zero.

Playable executable: D:/RWE/build-release/rwe.exe, built 2026-10-19 16:51:41
local time, 48,773,480 bytes. SHA256:
E2A31A5DB1BC9B0E3C18147F5214603CDFD23154E508D5ED29302637C501BA6D.
It was built before committing these fixes: generated source identity
is d9f785985d390380ab9d452b4dfacc92e9ebbf34-d, including all source changes
in this commit. No original runtime or visual briefing check was run.
