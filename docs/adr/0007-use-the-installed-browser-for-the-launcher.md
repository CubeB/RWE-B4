# ADR-0007: Use the installed browser for the launcher

The optional launcher uses the installed browser and a bundled Node backend,
retaining its React interface and existing game/lobby logic while removing
Electron's bundled Chromium. A system webview would also avoid Chromium but
requires a new platform host and bridge; the browser keeps the migration
smaller and needs no additional webview installation. DOWNLOAD-001 records
the RWE-original reference boundary and the local-session security contract.
