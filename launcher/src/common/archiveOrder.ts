export const archiveExtensions = [".gp3", ".gpf", ".ccx", ".ufo", ".hpi"];

function extname(name: string): string {
  const base = name.slice(
    Math.max(name.lastIndexOf("/"), name.lastIndexOf("\\")) + 1
  );
  const dot = base.lastIndexOf(".");
  if (dot <= 0 || dot === base.length - 1) return "";
  return base.slice(dot);
}

export function sortArchivesInLoadOrder<T extends { name: string }>(
  archives: T[]
): T[] {
  return [...archives].sort((a, b) => {
    const extA = archiveExtensions.indexOf(extname(a.name).toLowerCase());
    const extB = archiveExtensions.indexOf(extname(b.name).toLowerCase());
    if (extA !== extB) return extA - extB;
    const nameA = a.name.toLowerCase(),
      nameB = b.name.toLowerCase();
    return nameA === nameB ? 0 : nameA < nameB ? -1 : 1;
  });
}

/**
 * The author manifest's file name. Read exactly as `mods.ts` reads it, so a
 * mod is the same package to the launcher and to the fingerprint. It lives
 * here, beside the load order, because the renderer bundle imports it and
 * `archives.ts` pulls in Node's `fs`, `path` and `crypto`.
 */
export const manifestFileName = "rwe_mod.json";

/**
 * The engine's rule for a manifest's `entry` (validEntryPath in
 * src/rwe/setup/ModManifest.cpp): relative, `/`-separated, no empty, `.` or
 * `..` component, no backslash, colon or NUL, at most 260 characters. A path
 * the engine would refuse is not followed or accepted here either, so a
 * manifest cannot point the hasher outside its own mod directory and a peer
 * cannot report one. Here rather than in `archives.ts` for the same reason as
 * `manifestFileName`: it is pure, and the game server's validation uses it.
 */
export function isValidEntryPath(text: string): boolean {
  if (text.length === 0 || text.length > 260) return false;
  if (text.startsWith("/") || /[\\:\0]/.test(text)) return false;
  return text
    .split("/")
    .every(
      component => component !== "" && component !== "." && component !== ".."
    );
}
