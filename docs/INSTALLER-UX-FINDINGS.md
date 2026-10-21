ï»¿# Installer and first-run setup UX comparison

## INSTALL-UX-001: RWE packaging and data-import presentation are RWE-original

Research date: 2026-10-21. Dedicated ADR-0004 spec-room work from the separate
`D:/RWE-spec-room` documentation checkout. Only this prose document is changed;
no implementation or tests are changed. Reference access was read-only.

Reference: byte-tactics revision
`e89721c46c3fa06e59558419b54fe26061fafb73`, matching the GOG Total
Annihilation v3.1 game executable, SHA-256
`3b9c0fadabf3dc67ed5f05a70f1e1505a0c65deadd1a3c930adfe30e2a84995e`.
Evidence is static inspection of reference scope and original game startup/data
registration. No original game, original installer, GOG installer, packaged
RWE installer or import scenario was executed in this research.

### What the comparison establishes

The reference reconstructs `TotalA.exe`, the game executable. Its startup and
runtime data acquisition operate on an existing game-data installation and,
where applicable, CD media. For example, archive registration at `0x41D4C0`
changes to the executable directory and registers existing revision archives,
expansion archives and base archives, with CD-drive discovery in the applicable
path. CD/path checks are part of the game's existing-data consumption. This
is evidence about how an already present game obtains its content, not about
how a separate distribution installer creates that installation.

RWE's CPack/NSIS distribution, RWE desktop and Start-menu shortcut defaults,
a Windows graphical wrapper around RWE's existing `rwe_setup` import CLI,
and a Linux/AppImage shell front end around the same importer
are outside the original game executable's gameplay and data-consumption
contract. A wrapper for that RWE CLI has no original executable counterpart:
that CLI and the separate-engine import step are RWE facilities. Their
presentation, process launch, shortcut naming and packaging defaults are
**RWE-original distribution/setup UX**. They must not be described as
reproducing an original gameplay behaviour or original installer layout.

This classification does not depend on claiming that the historical product
had no graphical installer or shortcuts. Such an absence is **not** established.
The original external distribution installer, CD autorun/setup programs and
GOG packaging are outside this matching game-executable reference and were
not inspected. The reference cannot establish their wizard pages, default
choices, install directories, shortcut policies, privilege requirements,
content copy rules or licensing presentation. Modern byte-tactics development
setup/build scripts likewise are not evidence for the historical installer.

### Scope of the proposed RWE change

The supplied implementation-room proposal changes only packaging defaults,
desktop/Start-menu entry presentation and graphical front ends invoking the
existing import command. On Linux/AppImage, the shell front end may use
Zenity for folder selection and progress presentation, while invocations
with CLI arguments retain the existing command-line path. It does not propose changing content selection,
archive ordering, source-file discovery, copy/import semantics, destination
rules or the simulation. Those existing behaviours remain their own contracts;
this finding neither re-specifies them nor asserts that they conform to the
original. If a wrapper changes one of those behaviours, that additional scope
requires its own documented comparison rather than being covered implicitly
by this UX finding.

No deliberate departure from a decoded original installer policy is recorded
because no corresponding original external-installer policy has been decoded.
The rationale is an RWE-specific need: help a user install or launch the
separate engine and complete its existing data setup through familiar platform
controls without requiring command-line knowledge. The Linux/AppImage shell,
Zenity dialogs and argument-routing presentation have the same RWE-original
classification; they do not acquire original parity from using the same
underlying importer. This finding establishes the
original-comparison classification before implementation, not implementation
success or permission to redistribute game assets.

### Verification boundaries

Implementation-room verification should exercise the generated package's
actual defaults and shortcut targets, including paths containing spaces, and
confirm the graphical setup invokes the existing importer with the intended
arguments and reports its actual completion/failure. That verifies the RWE
product contract. An original-game parity claim is neither necessary nor
supported for these packaging and presentation choices. The Linux/AppImage checks should also cover graphical folder selection,
progress/error reporting and preservation of the CLI path when arguments are
supplied. No such RWE package or wrapper validation was performed by this
spec-room researcher.
