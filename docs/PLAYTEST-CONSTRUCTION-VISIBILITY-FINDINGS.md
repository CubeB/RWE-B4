# Playtest construction and visibility findings

Research date: 2026-10-21. Dedicated ADR-0004 spec-room research, performed
from the separate `D:/RWE-spec-room` documentation checkout. Only this prose
finding is an implementation-room input; no RWE source or test was inspected
or changed by this researcher.

## Reference and evidence level

All findings below compare against the read-only byte-tactics checkout at
revision `e89721c46c3fa06e59558419b54fe26061fafb73`. Its original binary is
GOG Total Annihilation v3.1, SHA-256
`3b9c0fadabf3dc67ed5f05a70f1e1505a0c65deadd1a3c930adfe30e2a84995e`.
Evidence is static reading of the matching decompilation, cross-checked
against the existing `TOTALA-EXE-VISION.md` sections 2, 17a and 18 where
applicable. No original executable, demo oracle or shipped-data scenario was
run during this investigation. These findings do not establish RWE runtime
conformance or diagnose the particular playtest save.

## CV-01: An unfinished nanoframe already supplies normal sight

**Original result:** sight does not wait for construction completion. Once a
unit is placed as an unfinished unit it is a live LOS source with its unit
definition's full `SightDistance`; there is no construction-progress scaling
or completion condition on the normal LOS update.

**Evidence:** initialization at `0x485A40` sets the live bit for both finished
and unfinished units. The finished/unfinished distinction writes remaining
construction as respectively zero or one and initializes health differently;
it does not withhold the live bit. Creation at `0x485F50` places/registers the
unit and reaches the initial sight setup at `0x482AC0` for both cases.
That sight setup uses the definition's `SightDistance` and model eye height,
with the eye position clamped above the water surface. The per-player tick at
`0x464F80` subsequently invokes the sight updater for every live unit, with no
construction test. The updater at `0x4827B0` also has no construction test and
uses the same definition sight distance. Its incremental terrain/grid work
is the mechanism already recorded by section 2.

**Timing qualification:** initial sight setup has mode-dependent work;
subsequent normal tick processing also supplies sight. Thus "when the frame
is placed, rather than when it finishes" is established; no sub-tick visual
latency measurement is claimed. Normal terrain occlusion, sight modes and
range quantization still apply. This finding is about LOS, not radar, sonar,
economy operation, weapon readiness or scripted activation.

**Implementation acceptance:** a real shipped unit placed unfinished should
supply the same definition sight range as its completed counterpart at the
same position/height and in the same sight mode. Completion must not be the
sole event that adds that source. Verify using shipped unit data and record
whether validation is an RWE check or an executed original-game check.

## CV-02: Seeing ground does not by itself reveal a building model

**Original result:** world unit visibility is tested using four corners of
the unit definition's axis-aligned extent, projected into the fog grid. It is not
a center-only test, and not a test of whether the terrain tile or metal-patch
image beneath the unit is exposed. A corner that passes the relevant fog
lookup is sufficient, after ownership, cloak and underwater rules.

The four samples, described geometrically, are the two horizontal extremes
at the definition box's front/top edge and the two horizontal extremes at its
back/bottom edge. The horizontal bounds are derived from the FBI/movement footprint; the
vertical bounds run from zero to the loaded object's maximum height.
For each sample, its fog coordinates use 32-world-unit cells: horizontal
coordinate divided into 32-unit cells and depth shifted by half the sample's
height before the same division. Whole-coordinate extraction and signed
arithmetic shifts matter at boundaries. This height projection means that
a model and its ground position can cross the visibility edge at different
times. The horizontal half-extents are footprint dimensions times eight world units;
the full horizontal extents are footprint dimensions times sixteen.

**Evidence:** `0x465AC0`, especially the samples at `0x465B6A`, `0x465BDB`,
`0x465C73` and `0x465D19`; definition bounds at offsets `0x15E`, `0x162`,
`0x166`, `0x16A`, `0x16E`, `0x172` and extents at `0x176`, `0x17A`, `0x17E`.
Existing section 17a contains the addresses and grid-backend details. The
world draw-list builder at `0x48BAE0` uses this unit visibility predicate for
non-owned units; no special metal-extractor exception is present there.

**Implication for the reported mex:** a visible metal patch alone is not
proof that an enemy extractor should already be rendered under the original
rule. A closer approach may legitimately bring one of the definition box's projected
samples into currently visible cells. Conversely a center-only RWE gate
would not implement the original rule and could delay reveal incorrectly.
The user's case remains unclassified until the exact unit, footprint bounds,
height, sight mode, ownership and fog cells have been examined.

**Uncertainty:** this investigation did not measure that metal spot in the
saved match, load its shipped feature artwork, or reconstruct its model/fog
coordinates. It therefore does not assert that the reported delay is correct.
The original's terrain/feature and unit rendering have different gates;
feature visibility in every feature class is not fully specified here.

## CV-03: A factory product is independently admitted to the draw list

**Original result:** the normal world draw-list visibility gate tests each
existing unit on its own definition extent and ownership. It does not require the
factory, builder or carrier also to be visible. Construction progress itself
does not exclude a unit from that list. Consequently a blanket rule that an
unfinished factory product must always inherit its factory's hidden state is
not supported by this original visibility path.

**Evidence:** `0x48BAE0` iterates units with a nonzero type, performs screen
extent culling, and admits owned units or units passing `0x465AC0`.
Neither gate examines a factory/builder/carrier visibility result or remaining
construction. The rendering pass at `0x468CF0` consumes that independently
built list. Attachment does affect a unit's actual position and orientation;
the position update at `0x43D6D0` follows the attachment piece when attached.
That can change which projected definition samples land inside sight.

**Implication for the report:** a product becoming visible before its factory
is not sufficient evidence of an original-behaviour bug. Different footprint bounds,
attachment position and height can produce different sample results. This
finding does not assert that every instance is legitimate: a wrong product
height, wrong definition bounds, wrong grid projection or wrong RWE fog lookup
could create the same visible symptom.

**Uncertainty:** no original in-game factory-edge reveal was executed, and no
specific shipped factory/product pair from the user's report was measured.
This finding establishes absence of a parent-visibility prerequisite in the
normal list/predicate, not the correctness of every later rendering detail.

## Remaining checks for implementation-room triage

Record RWE's current nanoframe LOS policy separately from the original answer.
For the two reveal reports, compare the four original definition-bound samples
against the current RWE rule using the actual shipped footprints, heights, positions and
fog cells. Preserve the user's save before any controlled replay. An
original-game or demo observation would strengthen the acceptance evidence;
it has not happened in this investigation. No RWE change or divergence is
authorized or recorded by this research document alone.


## CV-02a: Correction after tracing definition writes

This amendment supersedes the initial CV-02 assertion that horizontal bounds
came from actual model geometry. That assertion relied on provisional field
labels in the reference rather than tracing initialization, and was incorrect.
The accepted footprint-based account in `TOTALA-EXE-RENDER.md` at `0x42D079`
and `TOTALA-EXE-VISION.md` section 17a remains the standard. CV-02/CV-03 above
have been corrected to use definition bounds.

The definition loader at `0x42BF40` reads movement properties from the named
movement class, or from the FBI when no class is named, and stores footprint
X/Z. At `0x42D079` through `0x42D0C5` it writes horizontal minima as negative
eight times those footprint dimensions and maxima as positive eight times
them, in world units. It then derives all three extent differences. This is
why the horizontal full widths are sixteen times the footprint dimensions.
There is no mesh-derived horizontal bound in those stores.

The complete type-loading routine at `0x42D2E0` calls that FBI loader before
loading the corresponding object. Following object load, horizontal mirroring
and texture binding, it overwrites only the vertical minimum, maximum and
extent: minimum zero, maximum the object's computed height, and extent their
difference. It does not replace the footprint-derived X/Z values. Sorting and
compacting definitions use the assignment at `0x42B370`, which copies these
values rather than recomputing them. The type reload at `0x42D1F0` invokes the
same FBI loader and reloads the COB data; it has no mesh-horizontal overwrite.
The relevant named fields and raw offset occurrences were searched throughout
the reference to cross-check these writers and their consumers. No alternate
writer that replaces the normal horizontal definition bounds with mesh bounds
was found. This is a static source-level trace, not a runtime write watch.

The height computation at `0x4CB5F0` considers vertices with their accumulated
piece Y offsets and starts from zero. Its result is in the original fixed
coordinate representation. The horizontal mirror at `0x4CB590` negates X/Z
vertices and offsets and leaves Y unaffected. This does not turn the definition
rectangle into geometry bounds. No geometry-derived X/Z conversion, Z sign
rule, or shipped-model horizontal example is needed or justified for CV-02.
The visibility samples remain the definition rectangle's front pair at
unit Y plus this height and back pair at unit Y, as already documented by
section 17a. Unit orientation does not rotate these four samples.

This correction does not change CV-01's construction-sight conclusion or
CV-03's absence of parent-visibility gating. It removes the proposed
mesh-horizontal distinction as an explanation for RWE mismatches. Shipped
ARMMEX/factory sample values have not been calculated here; no claim of such
validation is made.

