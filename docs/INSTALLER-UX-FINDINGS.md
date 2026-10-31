# Installer and first-run setup UX comparison

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

Implementation scope clarification, 2026-10-31: suppressing RWE's empty NSIS
component-selection page by packaging the application as one monolithic
installation falls under INSTALL-UX-001. RWE offers no optional installation
components; dependency component metadata is not a user-facing product choice.
This changes the RWE wizard presentation, not game-data import semantics.
The reference revision, static evidence and external-installer uncertainty
above still apply; no original-installer parity is claimed.

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


## INSTALL-UX-002: Brand icons and signed releases are distribution identity

Research extension: 2026-10-21. The reference revision, matching GOG v3.1
binary/hash and static-only evidence level are the same as INSTALL-UX-001.
No additional original executable, installer, certificate or signing metadata
was inspected for this extension, and no implementation or tests were changed.

Using the user's supplied `b4logo.png` as RWE's application identity across
Windows executable and installer icon resources, taskbar/window icons,
Electron application icons and Linux/AppImage icons is RWE-original branding
and distribution presentation. The original reference is a different product's
game executable. Reproducing its gameplay does not establish a required RWE
brand image, icon size set, resource identifier or desktop-integration choice.
The supplied image's selection is a user requirement, not a claim of decoded
original behaviour. Image-format conversions and platform resource embedding
needed to use it remain presentation work under that same classification.

Windows Authenticode signing of RWE executables/installers and the release
publication process likewise establish the identity and delivery of RWE's
builds. They have no original gameplay counterpart that this reference can
specify. Certificate ownership, certificate procurement, signing service,
timestamping and publisher/release choices must be assessed against the RWE
release contract and available credentials; this finding does not establish
that any signing credential exists or that a signature is already valid.
It does not authorize publication or credential acquisition by itself.

No claim is made that the historical Total Annihilation executable or any
external CD/GOG installer lacked icons, signing or publisher metadata. Those
external installer packages and signing histories were not investigated.
The fact that a matching game-executable reference exists is not evidence of
absence of those distribution features. This finding records RWE-specific
identity/presentation scope, not a deliberate departure from a decoded
original signing or installer policy.

Fixing an error in the graphical import wrapper's Browse/folder-selection
control also remains within INSTALL-UX-001's RWE-original setup presentation
scope when it restores selection of the source folder and preserves the
existing importer arguments and import semantics. A graphical control repair
does not require inventing an original-game counterpart; changing discovery,
file selection rules or import results would exceed that classification.

Implementation-room verification should inspect the built artifacts' actual
icons on each affected platform and distinguish embedded executable resources
from installer branding, live window/taskbar icons and desktop-entry icons.
Signing investigation should distinguish an unsigned artifact, an embedded
signature, cryptographic validity and an operating system's trust result;
none implies the others automatically. Report the actual certificate/publisher
and validation result only after inspecting the artifact. No icon-rendering,
Browse-control, signing, trust or release-publication validation was performed
by this spec-room researcher.
