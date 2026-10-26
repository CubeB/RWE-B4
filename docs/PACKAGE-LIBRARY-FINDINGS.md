# Package library: first implementation contract

2026-10-25, issue #762, parent #761. This is RWE-original package management,
not a reproduction of an original-game installer. Inspection, JSON registry
persistence, managed ownership and library states have no claimed original
counterpart. The user authorized implementation from the shared management
plan. This document defines the first implementation slice before code.

## PACKAGE-001: reference boundary

MOD-ARCHIVE-01 in `TA-MOD-COMPATIBILITY-FINDINGS.md` records the static
byte-tactics comparison at revision e89721c46c3fa06e59558419b54fe26061fafb73,
GOG/v3.1 MD5 8e74a1dffa1f5988624c52048f5b20cd. Original archive registration
at0x41D4C0 and the existing HAPI prose establish archive discovery/resolution,
not a launcher-managed content registry. Arbitrary GP3/GPF support and explicit
mod roots are existing RWE extensions. Inspection reuses RWE's HPI reader and
does not implement a new archive decoder or change engine precedence.

This work does not inspect the decompilation. The comparison above is the
documented spec-room handoff, not a newly executed original-game check.

## PACKAGE-002: versioned registry

Registry JSON has schemaVersion1 and a packages array. Package IDs are
`p-` followed by32 or64 lowercase hexadecimal characters, independent of
display names. Records distinguish mod, AI, maps, base and asset packages;
name/version, source provenance, managed/external ownership, dependencies,
resource roots and file manifests are separate fields. Compatibility remains
explicitly unverified unless separate execution evidence establishes more.

Managed content paths are exactly `packages/<id>`, relative to the library.
Manifest paths are normalized relative paths with no parent traversal, empty
components, backslashes, drive/stream prefixes or embedded NUL. This is an RWE
ownership contract for later installation/removal, not an original format rule.

Absent registries read as empty without creating directories. Corrupt,
unsupported-version or invalid records fail explicitly rather than being
replaced with an empty library. Updates validate before writing and atomically
replace the registry from a unique temporary file in the same directory.
Cross-process registry transaction coordination and stale-snapshot rejection
are defined in PACKAGE-006; install journals and job lifecycle remain #765.

## PACKAGE-003: read-only inspection

The native `rwe_setup --inspect-package <source> --json` returns candidates,
source/root identities, supported TA archive inventories, loose resource
layouts, map pairs and warnings. It never copies, installs or deletes source
files. `--list-packages --library-root <dir> --json` reads that library's
registry without importing base TA data as a side effect.

Folder inspection groups archives by their actual data root rather than
calling every archive a separate mod. It scans nested package folders, skips
directory links and reports traversal bounds. Loose TNT/OTA selection finds
its partner case-insensitively. Archive maps are initially inventoried by name;
terrain, schema, dependency and preview validation remains #768.

Existing rwe_mod.json supplies display metadata. The known Escalation layout
AE/UNITSE/WEAPONE/GAMEDATE identifies the mod, with all seven configured roots
recorded when present. A supplied GOLD release marker can supply version;
folder names alone do not establish identity/version. Plain original-AI text
uses the existing classic profile parser for detection. These are recognition
results, not gameplay-compatibility certificates; broader detection is #764.

ZIP/RAR/7z containers return explicit extraction-required results until #763
provides the bundled extraction backend. No installer executable or native
mod DLL is executed. This does not drop the initial-release RAR/7z requirement.

First inspection limits are12 nested directories,100000 visited entries,
256 candidate roots and1MiB external metadata files. Limit/permission failures
are reported as incomplete inspection, not successful installation. Existing
HPI directory/decompression limits remain unchanged.

## Verification and remaining work

Executed, 2026-10-25: Release importer/game/tests built. With retail data
enabled, all1937 regression cases and215159 assertions passed (checksum tests
excluded). All7 native CLI integration tests passed, including a Unicode
source path, malformed registry preservation, missing-registry reads without
creating directories, real stored-HPI inventory, pending-command guards and
the real desktop TAESC folder. The existing base-data importer regression
also passed. Artifacts are outside the repository in
`D:\RWE-package-manager-20261025`.

TAESC recognition returned GOLD10.2.0,8 TA archives,7 resource mappings and177
map records; these include metadata overrides/unpaired records, not177 proven
playable maps. The folder also contains one ZIP still awaiting #763.
Snapshots of every source file's name, size and modification time matched
before/after inspection. No original/patched executable or installer ran.

Installation state is separate from compatibility: registry records allow
available, installed or needs-attention; in-flight job states belong to the
future installation controller. Empty optional author/version strings mean
unknown. Managed ownership and file hashes are structurally validated here;
actual installation, hash calculation/verification, journals and writer
serialization remain the separately tracked tasks.

### PACKAGE-004: native Windows path arguments

The Windows smoke test found that narrow argv lost a non-ASCII folder path.
Package commands use the native Unicode command line through
[CommandLineToArgvW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-commandlinetoargvw),
with LocalFree ownership. This is an RWE CLI policy, not original-game parity.
Empty optional author/version metadata means unknown, not a malformed package.
Registry manifests also reject Windows device names and trailing-dot/space
aliases to maintain the managed relative-path ownership contract.

Meaningful tests cover registry replacement/preservation, invalid IDs/versions,
duplicate identities, source ownership, map pairing, nested data roots,
metadata errors, corrupt TA archives and extraction-required containers. Run
the actual desktop TAESC folder through the native CLI and record recognition,
archive/map counts and lack of writes. No proprietary fixtures are committed.

Installation, uninstall, archive extraction, launcher screens, runtime profile
activation and multiplayer checks are separately tracked tasks. No original
or patched game execution, full map validation or complete mod parity is
asserted by this contract.

### PACKAGE-006: serialized registry transactions

Issue #775, under ADR-0004. Cross-process coordination is RWE-original
package-library behavior. PACKAGE-001's documented static comparison at
byte-tactics revision `e89721c46c3fa06e59558419b54fe26061fafb73` (GOG/v3.1
`TotalA.exe` MD5 `8e74a1dffa1f5988624c52048f5b20cd`) establishes original archive
discovery and resolution, not a managed registry or concurrent install
contract. No original runtime behavior is changed or claimed here.

Registry updates use a transaction API that acquires an operating-system
exclusive lock on a stable sibling lock file, reads the latest registry while
holding that lock, applies one mutation, validates it, increments its
generation and atomically replaces the registry file from a same-directory
temporary. Unix-like builds use `flock`; Windows builds use `LockFileEx`.
Process exit releases either OS lock, so recovery does not depend on deciding
whether a recorded PID is stale. The lock file intentionally remains in place:
deleting and recreating it could let two writers lock different file objects.
Package-library maintenance must not unlink or replace this sidecar while any
writer could be active. POSIX descriptors are close-on-exec so helper processes
cannot keep a parent's lock alive accidentally; Windows lock handles are
non-inheritable.

The public snapshot-save operation is serialized too. It compares the
generation supplied by the caller with the current generation and rejects a
stale snapshot instead of overwriting a newer update. A transaction holds the
lock over read, mutation and commit, so concurrent transaction callers retain
both changes. Existing version-1 registries with no generation load as
generation zero; the first successful write stores generation one. Read-only
loads of an absent registry continue returning an empty value without creating
the library directory or lock file.

Readers do not acquire the writer lock. They open the registry path and parse
one complete old or new file because writers publish through atomic same-volume
replacement. Windows readers allow delete sharing on the opened registry handle,
so replacement can publish while an existing reader finishes with the old
snapshot. A failed mutation or validation leaves the old file intact and
unlocks on scope exit; a process crash likewise releases the OS lock. The
cross-process regression runs two independently launched test processes behind
a start barrier and verifies both package records and the resulting generation.
It also checks stale-snapshot rejection and that a failed transaction leaves
the prior JSON intact and permits a subsequent update. These are RWE registry
tests, not original-game execution or gameplay-compatibility evidence.
