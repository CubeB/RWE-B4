# B4 application identity

INSTALL-UX-002 in docs/INSTALLER-UX-FINDINGS.md classifies this as RWE branding.

`b4logo.png` is the user's supplied Desktop image, copied without changes.
SHA-256: `D37D4A5775280E171BD54ECAFFD745FB67AF6FADFE76EC195788B1848DDAC53B`.

`b4-icon.png` is its full square image resized to 256x256 for Electron and
AppImage. `b4.ico` contains 16, 24, 32, 48, 64, 128 and 256 pixel PNG frames
for Windows. They were converted with System.Drawing high-quality bicubic
resampling; no crop or artwork redesign was applied. The original full-size
PNG is not installed into the runtime package.

The ICO is embedded as resource group 1 in rwe and rwe_setup. SDL's Windows
backend loads the first icon resource by default. Electron's packager embeds
it separately, and BrowserWindow receives the PNG for the running window.
NSIS uses it for the installer/uninstaller and the setup shortcut; WinForms
loads the installed ICO. AppImage uses the PNG as its desktop icon.
