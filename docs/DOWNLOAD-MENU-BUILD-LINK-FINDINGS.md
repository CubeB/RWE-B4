# Download menu records: build links and button coordinates are separate

Spec-room findings, 2026-10-25, ADR-0004. Documentation only; no RWE
implementation or tests were inspected or changed.

## DOWNLOAD-LINK-01: omitted MENU/BUTTON do not discard a valid build link

Original download-TDF loading at `0x42DCF0` resolves each record's
`UNITMENU` against loaded builder unit names. On a successful match it stores
that builder, the `UNITNAME` product string, and page/button values.
**MENU and BUTTON are optional integers, each defaulting to zero.** They
are not prerequisites for storing a record whose builder resolves.

The subsequent build-option augmentation at `0x42BE30` matches the stored
builder and resolves the product name. A nonzero product ID is appended to
that builder's existing build-option list. It does not check MENU or BUTTON.
The original classic AI chooser at `0x40BDB0` iterates that list and scores
its products; it does not require the product to have a visible GUI button.
Other scoring/limit/side checks still decide whether a listed product can
actually be chosen. This establishes the missing-coordinate records'
relevance to the original AI without claiming that a link forces production.

Original augmentation requires the builder's build-option storage to exist.
That storage is allocated for appropriate builders while reading their
`CANBUILD` lists from sidedata. The download augmentation is not an
unconditional grant to a nonbuilder that lacks the list. Product resolution
must succeed too. Missing coordinates alone, however, cannot justify dropping
an otherwise valid builder/product relationship.

## DOWNLOAD-LAYOUT-01: zero page does not mean an ordinary visible first page

The completed-unit build-panel routine at `0x41ACE0` overlays a download
product into a GUI control only when the stored page minus one equals the
current page index. It locates that control using the stored button offset
plus four in the GUI entry array, including its panel header at entry zero.
Thus a stored MENU value1 corresponds to internal current page0. Internal
page zero is the orders view for ordinary builders; build page one loads
the unit's `1.GUI`, build page two its `2.GUI`. In particular, the stock
ARMACK download records' MENU=3 maps to internal page2 and ARMACK2.GUI,
the human second build page. There is only one subtraction. A default MENU
value0 would require current page-1,
rather than becoming a normal first-page placement.

Accordingly, absent MENU/BUTTON can leave a build option in the classic AI's
candidate list without inserting it onto an ordinary human GUI page. Do not
convert every absent-coordinate record into an arbitrary visible first-page
button while claiming that this follows the original.

The stock ARMACK records' BUTTON=3/4/5 address entries7/8/9, the three
IGPATCH placeholders. Their overlay enables per-product gadget GAF binding
and keeps the placeholders' authored geometry; this is distinct from the
ordinary page/common GAF button route. See BUILD-DOWNLOAD-850 in
`ARMACK-SECOND-PAGE-BUTTON-FINDINGS.md` for the full static trace, actual
stock product resources, dimensions, hotspots and inclusive hit rectangles.

The original keeps its finite build-list storage and has the previously
documented less-than-or-equal-to30 append guard, an off-by-one against a
30-ID allocation (`BT-UNITS-UNIT-TYPES-PARITY.md` rule9). This is not a
safe promise to support31 entries, nor a reason to reproduce memory corruption
when accepting a large mod. RWE can use safely bounded/dynamic storage while
recording that representation choice separately.

## DOWNLOAD-PROMOTION-01: the first successfully stored row matters

The promotion loop in `0x42DCF0` and recheck `0x42BD40` compare a unit name
with the **stored first record's product name** for each download file. A
match promotes that product's effective Downloadable flag and issues the
missing-flag warning if necessary. This check is independent of MENU/BUTTON;
it does not promote every product named anywhere in the file.

Crucially, the loader stores UNITNAME only inside successful UNITMENU
resolution. A raw text UNITNAME in an unresolved first row is not a valid
stored first-row product. `UNITMENU=BLANK` has no special disabled-builder
interpretation in this original loader: it simply fails to resolve unless
there is an actual loaded unit named BLANK. Such unresolved rows retain
unsafe/uninitialized original-storage behavior. They are not a behavioral
contract for manufacturing links or promoting a product by reading text that
the original did not successfully store. A safe RWE parser should distinguish
unresolved rows from valid rows rather than copying that memory defect.

## Real Escalation data and patched-engine uncertainty

The GOLD10.2.0 package's `TAESC.gp3`, entry `downloadse/armaaa.tdf`, was
independently read through the existing archive reader. Its five records
all name product ARMAAA. Records3 and4 actively name builders ARMACV and
ARMACH; records1,2,5 actively say BLANK, with former builder names in comments.
All five MENU and BUTTON declarations are commented out.

Under the original valid-record semantics, and with these builder/product
names loaded and builder lists allocated, the ARMACV and ARMACH records
establish build links despite having no explicit coordinates. The BLANK
records do not establish ordinary builder links. Since the first record is
BLANK, this file alone does not establish a safely resolved original first-row
promotion of ARMAAA; inspect its FBI and other valid promotion sources before
deciding the effective Downloadable state.

Escalation's `downloadse` directory is itself a patched-data namespace,
covered by `TA-MOD-COMPATIBILITY-FINDINGS.md` MOD-DIRS-01. This pass did
not inspect the patched executable's download-loader instructions or run
the patched game. Whether Escalation adds auto-layout, different disabled-row
handling or additional build-link rules remains unestablished here.
The original comparison supports accepting valid missing-coordinate records
as AI build links; it does not prove every patched-engine rule.

## Evidence and acceptance limits

- Read-only byte-tactics revision
  `e89721c46c3fa06e59558419b54fe26061fafb73`, representing GOG/v3.1
  `TotalA.exe` MD5 `8e74a1dffa1f5988624c52048f5b20cd`.
- Addresses: download loading/promotion `0x42DCF0`; build-list augmentation
  `0x42BE30`; promotion recheck `0x42BD40`; sidedata/build-list initialization
  `0x42D2E0`; classic option selection `0x40BDB0`; GUI placement `0x41ACE0`.
- Existing prose: `BT-UNITS-UNIT-TYPES-PARITY.md` rule9;
  `TA-MOD-COMPATIBILITY-FINDINGS.md` MOD-AI-02 and MOD-DIRS-01.
- This is a **static original-reference and real shipped-data check**. No
  original or patched game was launched, no demo oracle/matching checker
  ran, and no RWE current behavior was inspected. The observed data record
  is not a claim that ARMAAA was constructed in either engine.
- Proposed checks: a valid builder/product with omitted coordinates enters
  the classic candidate list; adding MENU/BUTTON changes placement rather
  than deleting eligibility; BLANK creates no fabricated builder; invalid
  product names create no valid product link; first-row promotion is checked
  separately from later valid links. Real construction and patched-engine
  parity remain execution work.
