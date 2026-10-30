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
