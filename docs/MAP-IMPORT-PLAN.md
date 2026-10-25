# Map import plan

Status: proposed, 2026-10-25. Planning only; no map importer is implemented by
this document. Import management is an RWE feature with no original-game UI
counterpart. Existing map formats and runtime behavior remain subject to
ADR-0004 and the documented original-data findings.

## Intended experience

Choose **Import maps**, browse or drop files/a folder, review the maps found, then
import them into the chosen game profile. Show a thumbnail, map name, size,
player count and any missing dependencies before import. Newly imported maps
appear without restarting RWE. Keep the original map selector's artwork,
geometry and typography; put management in a separate RWE screen and the
optional launcher rather than redesigning the original dialog.

Provide the same import capability through `rwe_setup` for standalone users.
The native screen, launcher and command line share one backend and report the
same validation results. Proposed command shapes, not existing commands:

```
rwe_setup --inspect-maps <source> --json
rwe_setup --import-maps <source> --profile <profile-id> --json
```

No automatic internet downloads are needed for the first version.

## Accepted sources

| Source | Initial handling |
| --- | --- |
| `.ufo`, `.hpi`, `.ccx`, `.gpf`, `.gp3` | Inspect with RWE's archive reader; retain original archive bytes. |
| `.zip` | Inspect its contents; support nested folders and contained TA archives. |
| Loose `.tnt` and `.ota` | Pair by case-insensitive internal basename; selecting either scans its containing folder for the partner. |
| Folder | Find map pairs, TA archives, ZIP packs and required companion assets. |
| `.7z` and `.rar` | Later phase, after selecting and packaging a supported extraction library; report unsupported input clearly meanwhile. |

Inspect all map entries in a pack. Do not assume one archive contains one map.
Treat executable installers as unsupported inputs; importing game data never
requires executing an installer. A ZIP containing a TA mod is not necessarily
a map pack.

## Discovery and validation

Reuse `HpiArchive`, `TntArchive`, the OTA parser and VFS resolution. Validate
the TNT/OTA pair together, terrain/tile/minimap bounds, start positions and
available schemas. Do not invent a square-map or power-of-two restriction.
Keep descriptions intact using the original label font and layout.

Resolve features, GAFs, models, palettes and other required assets against the
candidate package plus the selected mod and base data. Report missing assets
by filename and affected map. Retain pack dependencies instead of extracting
only TNT/OTA and producing a map with missing trees or terrain features.

Distinguish skirmish maps, mission/campaign content and mixed mod packages.
The current bridge lists only maps with multiplayer schemas; import must not
silently promise that every imported mission will appear in that list.
Campaign registration is a separate follow-up, not automatic skirmish-map
conversion. A pack that replaces units, weapons or AI needs an explicit mod
import route rather than silently changing every game's definitions.

A useful result contains candidate maps, their internal paths, source package,
content hashes, schemas, required assets, resolved dependencies, collisions,
warnings and whether each map is ready for the selected profile. A readable
archive alone is not proof that the map can load.

## Storage, duplicates and removal

Store managed packages under a dedicated per-user `MapPacks/<package-id>/`
root, outside the repository and original TA installation. Use content hashes
for package identity; preserve display names and internal archive names.
Keep both the original package checksum and canonical map/dependency content
hashes, so different ZIP wrappers do not turn identical map content into
different playable variants. Keep a small registry recording package provenance, contained maps and profile
associations. Copy by default, so deleting a desktop download does not break
an imported map.

Identical content is already installed and needs no second copy. Different
content with the same map name remains a separate variant, shown with its
source/profile. Do not overwrite another pack or a base-game archive. Updating
a managed pack keeps the previous version available for rollback. Removing a
pack removes only its managed files and registry associations.

Stage, validate and register each package transactionally. Cancellation or a
failed copy leaves no half-installed entry. Validate extraction destinations
and bound archive expansion using the existing parsers' resource limits.
Retain diagnostics for invalid maps; do not register them as playable merely
because other maps in the same pack passed.

## Engine and launcher integration

The current VFS scans archives in an explicitly added root; putting them in
an arbitrary subfolder will not make them discoverable. The registry must
therefore participate in map enumeration and launch-path construction.

Enumerate each package's maps with its own source identity, using isolated
candidate VFS views. At game launch mount the selected package's required
roots with the selected mod and base roots in an explicit order. The proposed
default is active mod, selected map's dependencies, then base data; TNT/OTA
are read from the explicitly selected map source. Report conflicting dependency
overrides for that profile instead of pretending the combination is validated.
Do not mount every imported pack's feature or gameplay overrides globally.
Record and test this precedence against conflicting real packages before coding
the policy; map dependencies must not accidentally replace a mod's units.

Reuse the bridge's `map-list`, `map-info` and `get-minimap` results where
possible. Today these identify maps using a name/source pair and source-specific
TNT/OTA reads. Extend that identity with a stable package/map ID so duplicate
names stay distinct. Preserve existing unmanaged maps as readable entries.

Thumbnail and validation caches need the map/package content hash and active
mod fingerprint. Invalidate them after updates or profile changes. Refresh
the open map list when import completes, preserving its current selection.

Multiplayer launch checks must include the selected map package and companion
assets. The existing archive fingerprint is a useful foundation, but loose
TNT/OTA and loose feature files also need coverage. Check the complete selected
asset set, not just a display name or TNT hash. Report the missing/mismatched
package before the host starts the game. Automatic peer transfer is a later,
separate feature.

## Implementation sequence

1. Native discovery/inspection backend and JSON report. Test real shipped maps,
   loose pairs, multi-map packs and custom features without installing anything.
2. Managed package storage, registry, duplicate policy and transactional import,
   exposed by `rwe_setup`, plus registry-aware enumeration and selected-package
   launch resolution. Verify an imported map really lists and loads, along
   with cancellation, failed writes and rollback.
3. Separate import screen and optional launcher integration; thumbnails, profile
   selection, dependency results, progress and live map-list refresh.
4. Multiplayer asset verification, with
   duplicate-name and conflicting-dependency integration tests.
5. Optional additional archive formats, campaign import and peer transfer,
   each with its own requirements and verification.

## Acceptance evidence

Use the installed TA corpus and desktop TAESC package without committing or
redistributing their assets. Exercise both stock and mod profiles. Include
upper/lower-case names, nested ZIPs, multiple maps, loose pairs, missing pairs,
custom features, identical reimports, conflicting names, corrupt archives,
cancellation and a missing multiplayer dependency. After a successful import,
enumerate the map, render its thumbnail and load a bounded real game. Original
data/reference checks and actual original-game execution are separate claims.

The first implementation should deliver inspection plus transactional local
import. Full Escalation gameplay compatibility and automatic map downloading
are not prerequisites for designing this importer, and must not be implied
by successful map import.
