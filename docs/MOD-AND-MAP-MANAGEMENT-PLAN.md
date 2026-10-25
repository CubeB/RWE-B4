# Mod and map library management

Status: proposed, 2026-10-25. User requirement: the launcher accepts a folder,
RAR or other supported package, unpacks it, identifies and labels its contents,
and manages installation and removal. Maps receive the same treatment.
This expands `MAP-IMPORT-PLAN.md`; neither document implements the manager.
Package management is an RWE feature, separate from verified gameplay
compatibility with the original engine or a mod's extensions.

## Launcher experience

Add a **Library** with **Mods** and **Maps** tabs and an **Add content** action.
Support file/folder browsing and drag-and-drop. Initial inputs include folders,
ZIP, RAR, 7z, TA archives and loose TNT/OTA pairs. RAR and 7z are initial
requirements, not deferred formats. Bundle a supported extraction backend so
the user does not have to install WinRAR or configure an external command.

The flow is:

1. Select a folder or archive. Inspect nested folders/archives and unpack into
   temporary staging as needed, with progress and cancellation.
2. Show the detected mod/map names, versions where known, package size, maps
   included, dependencies and any conflicts. Allow the user to correct the
   display name and choose among multiple detected packages.
3. **Install** copies validated content into RWE-managed storage and registers
   it. Refresh the Library, mod selector and map list immediately.
4. **Play** uses the selected mod/profile and map with the required resource
   roots. Installing a mod does not activate it in every existing profile.

Show clear lifecycle states: available to install, installing, installed,
update available, needs attention and uninstalling. Show compatibility status
separately: recognized, load-tested or requiring unsupported features. A mod
can be installed successfully without being fully supported by the engine.

## Identification and labeling

Use package metadata (`rwe_mod.json` where present), known archive/layout
signatures and supplied version/readme metadata. Folder and archive filenames
are fallback hints, not authoritative identity. Do not invent an author or
version when the package does not provide one.

Recognize the desktop TAESC package even when selected at its outer folder:
find the actual Step_2 data root, label it **Total Annihilation: Escalation**,
and record the detected package version. Its AE, UNITSE, WEAPONE, GUIE,
GAMEDATE, DOWNLOADSE and UNITPICE mappings belong to its launch profile.
Optional wrapper/shader folders do not become extra mods or replace RWE's
renderer. Do not execute bundled installers or TA engine DLLs to import data.

Classify original-AI profile packs as **AI mods**, retaining their profiles and
companion units/build definitions. Apply original TA AI packs to the classic
engine; do not silently convert them into RWE-planner personalities. Allow
them to depend on a particular base mod or version.

Recognize map packs with multiple maps and custom feature/texture dependencies.
Classify mixed mod/map packages without splitting away assets needed to play.
An unrecognized but structurally valid package can use a user-supplied label;
unresolved layout or dependencies remain visible for review.

## Management actions

| Action | Behavior |
| --- | --- |
| Install | Copy a selected package into managed storage, verify it, then register atomically. |
| Activate/deactivate | Change a profile's selected content without deleting installed files. |
| Create/edit profile | Choose a base mod, compatible AI/unit add-ons, map packs and explicit override order. |
| Update | Inspect a new local package, identify its relationship to an installed version and show changes before switching. |
| Roll back | Restore a retained prior package version and its profile association. |
| Verify/repair | Check managed files against recorded hashes; repair from the original source or a newly selected package. |
| Rename/display details | Edit the library label; show detected identity, version, source, size and dependencies. |
| Open folder/rescan | Locate managed files and refresh externally added or missing content. |
| Uninstall | Remove the selected managed installation after showing dependent profiles/packages. |

Uninstall preserves original source folders/archives, base TA data, saves,
replays, screenshots and player settings. Shared dependencies remain while
other installed packages use them. Offer removal of unused dependencies as a
separate choice. If a profile depends on removed content, report what must be
changed rather than silently substituting stock TA or another mod.

Distinguish uninstalling an installed package from removing its downloaded or
catalogued source. Support exact-content deduplication and side-by-side versions.
Never identify two different mods solely by the same display name/shortname.

## Shared storage and installation backend

Use one package registry for mods, AI add-ons and map packs, with stable IDs,
content/version hashes, original-source provenance, package type, managed
files, dependencies, resource mappings, profile associations and installation
state. Keep original archive hashes and canonical asset hashes distinct.
Store content outside the repository and original TA installation.

Reuse the existing per-user Mods location for managed mod installations where
possible. Add managed map-pack storage and migrate/adopt existing launcher
entries explicitly. Existing unmanaged folders remain usable; deleting files
from an external installation must never be disguised as uninstalling an
RWE-owned copy. Copying into managed storage is the default import operation.

Extraction, inspection, install, verification and removal use one backend
shared by the launcher and `rwe_setup`. Native engine launch and standalone
map management consume the same registry. Proposed commands, not existing APIs:

```
rwe_setup --inspect-package <source> --json
rwe_setup --install-package <source> --json
rwe_setup --list-packages --json
rwe_setup --verify-package <package-id> --json
rwe_setup --uninstall-package <package-id> --json
```

Keep transaction journals and confined temporary staging. Validate extracted
paths, archive expansion and dependencies before registration; cancellation
or failure must leave the previous installed version usable. Package IDs,
not display names or archive paths, determine managed destinations/removal.
Preserve TA archive bytes and their internal structure instead of flattening
unrelated nested versions into one directory.

## Engine and multiplayer integration

Compile a profile into an explicit ordered asset stack and resource-directory
configuration. Keep unrelated mods isolated. Resolve the selected map's
TNT/OTA by its package/map identity, even when another pack uses the same name.
Retain map dependencies and detect conflicts with the chosen mod.

Extend the existing launcher mod discovery, saved preferences, engine launch
arguments and fingerprint machinery instead of creating a second independent
list of active content. Current discovery accepts a directory with archives
or `rwe_mod.json`; its name-based deduplication is insufficient for managed
versions and must move to stable package/profile IDs.

Multiplayer checks include the complete active mod/AI/map asset stack, including
loose files. Installation alone is not enough: the host and guests must select
matching content and versions. Peer downloading/distribution is separate from
local import and does not belong to the initial release.

## Delivery sequence and acceptance

1. Shared registry and inspect/extract service for folders, ZIP, RAR, 7z and TA
   archives. Detect real TAESC roots, map packs and original-AI packs; preview
   classification, labels, dependencies and conflicts without installing.
2. Transactional install/uninstall, duplicate/version handling, verification,
   dependency ownership and CLI operations. Register and load an imported
   map/mod through the real engine before considering installation complete.
3. Launcher Library screens, file/folder selection, drag-and-drop, progress,
   corrected labels, profile activation, updates and live list refresh.
4. Multiplayer fingerprint integration, rollback/repair flows and tests across
   Windows/Linux. Expose the same installed map library in standalone RWE.

Acceptance uses the actual desktop TAESC package, available community AI packs,
real map archives and loose maps. Cover nested package roots, multiple maps,
custom features, duplicate names/content, missing dependencies, differing
versions, interrupted installs, shared-dependency uninstall and preserved user
data. Verify listing, profile selection, thumbnails and bounded real game
loads. Record unsupported engine features separately from import failures.

No proprietary assets are committed or redistributed as fixtures. References
for actual data/AI behavior remain the clean-room findings; management tests
establish RWE's installation behavior rather than original-game parity.
