# Windows release signing investigation

Research date: 2026-10-21. Scope/classification: INSTALL-UX-002 in
[the installer findings](INSTALLER-UX-FINDINGS.md). This is distribution
identity, not original-game conformance.

## Current state

The published v1.1.0-pre3 installer returns `NotSigned` from
Get-AuthenticodeSignature. No code-signing certificate was found in the
local CurrentUser/My or LocalMachine/My signing-certificate stores. No
repository Actions secrets or environments are configured. No signing
identity has been enrolled, credentials acquired, certificate generated,
service terms accepted or external application submitted by this work.
The installer remains unsigned; the new icon is not a signature.

## Practical routes

1. **SignPath Foundation** is a candidate for free open-source signing,
   subject to its eligibility review and conditions. Its foundation-issued
   certificate identifies SignPath Foundation as publisher. The maintainer
   must approve/apply, authorize the GitHub App, configure the project,
   artifact policy and release approvals, and supply a signing token.
   See [eligibility and conditions](https://signpath.org/terms.html),
   [application](https://signpath.org/), and
   [GitHub integration](https://docs.signpath.io/trusted-build-systems/github).
   RWE's existing public release and open-source distribution make this worth
   investigating; approval and eligibility have not been established.
2. **Microsoft Artifact Signing**, formerly Trusted Signing, is the managed
   paid option. Microsoft's developer guidance currently lists a starting
   price of $9.99/month. Public-trust identity validation is required;
   individual eligibility currently covers US/Canada, while organizations
   have wider geographic coverage including the UK. Do not infer residence
   or eligibility from a timezone. See the
   [setup/eligibility guide](https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart).
3. A public CA certificate with a supported hardware/cloud signing provider
   is another route when those services are unsuitable. Procurement and
   identity validation remain maintainer decisions; no purchase is proposed.

Signing displays a verified publisher and supports accumulating reputation;
it cannot promise that a new build never produces SmartScreen warnings.
Self-signed certificates do not solve public trust, and EV certificates no
longer provide automatic SmartScreen bypass. Antivirus detection and
SmartScreen reputation must be diagnosed separately. These conclusions come
from [Microsoft's SmartScreen guidance](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation),
not an executed clean-Windows reputation test.

## Integration once a maintainer-controlled identity is available

- Complete icon/version/resource changes first. Sign own engine, bridge,
  importer and modified launcher executables before packaging; preserve valid
  third-party signatures. The Foundation's signing scope/rules must be checked
  for bundled upstream components rather than assumed to permit signing all.
- Package signed payloads. Sign the final NSIS installer and applicable
  scripts/uninstaller through the chosen service's supported integration.
- Use SHA-256 and RFC3161 timestamping; verify actual signer, chain and
  timestamp on Windows. A signature blob alone does not prove trust.
- Restrict signing credentials to trusted release jobs, never pull requests.
  Pin integration actions, require appropriate release approvals and use
  service-backed keys/identity rather than putting private keys in this repo.
- Compute/upload checksums only after signing. Download and validate the final
  artifacts; do not change icons or other executable bytes after signing.

Tracked as [issue #746](https://github.com/CubeB/RWE-B4/issues/746). Enabling
production signing is pending maintainer enrollment/identity approval, not a
missing local implementation test.
