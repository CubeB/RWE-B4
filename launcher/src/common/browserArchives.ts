import type { ArchiveInfo, ManifestInfo, ModFingerprint } from "./archives";
import { sortArchivesInLoadOrder } from "./archiveOrder";

export interface ArchiveDifference {
  archiveName: string;
  kind: "missing" | "extra" | "different";
}
/**
 * The author manifest, when it is the manifest that differs.
 *
 * `missing` and `extra` are stated from the reference player's side, as they
 * are for archives: `missing` is a manifest the reference has and the other
 * player has not, `extra` is the reverse. A manifest on one side and not the
 * other is a difference in both directions, because a legacy archive-only
 * package and a package that declares a manifest do not resolve the same way.
 */
export interface ManifestDifference {
  kind: "missing" | "extra" | "different";
}
export interface ModDifference {
  modName: string;
  differences: ArchiveDifference[];
  manifest?: ManifestDifference;
}
export interface ModSetComparison {
  differences: ModDifference[];
  unknownMods: string[];
}
export interface PlayerArchiveState {
  playerId: number;
  mods: ModFingerprint[] | undefined;
}
export interface ArchiveAgreement {
  uncheckedPlayerIds: number[];
  mismatches: { playerId: number; mods: ModDifference[] }[];
}

export function compareArchives(
  mine: ArchiveInfo[],
  theirs: ArchiveInfo[]
): ArchiveDifference[] {
  const theirsByName = new Map(theirs.map(a => [a.name.toLowerCase(), a]));
  const mineNames = new Set(mine.map(a => a.name.toLowerCase()));
  const differences: (ArchiveDifference & { name: string })[] = [];
  for (const archive of mine) {
    const other = theirsByName.get(archive.name.toLowerCase());
    if (other === undefined)
      differences.push({
        archiveName: archive.name,
        kind: "missing",
        name: archive.name,
      });
    else if (other.hash !== archive.hash)
      differences.push({
        archiveName: archive.name,
        kind: "different",
        name: archive.name,
      });
  }
  for (const archive of theirs)
    if (!mineNames.has(archive.name.toLowerCase()))
      differences.push({
        archiveName: archive.name,
        kind: "extra",
        name: archive.name,
      });
  return sortArchivesInLoadOrder(differences).map(({ archiveName, kind }) => ({
    archiveName,
    kind,
  }));
}

export function compareManifest(
  mine: ManifestInfo | undefined,
  theirs: ManifestInfo | undefined
): ManifestDifference | undefined {
  if (mine === undefined) {
    return theirs === undefined ? undefined : { kind: "extra" };
  }
  if (theirs === undefined) {
    return { kind: "missing" };
  }
  return mine.hash === theirs.hash ? undefined : { kind: "different" };
}

export function compareModSets(
  activeMods: string[],
  mine: ModFingerprint[],
  theirs: ModFingerprint[]
): ModSetComparison {
  const mineByName = new Map(mine.map(mod => [mod.name, mod]));
  const theirsByName = new Map(theirs.map(mod => [mod.name, mod]));
  const differences: ModDifference[] = [],
    unknownMods: string[] = [];
  for (const modName of activeMods) {
    const a = mineByName.get(modName),
      b = theirsByName.get(modName);
    if (a === undefined || b === undefined) {
      unknownMods.push(modName);
      continue;
    }
    const archiveDifferences = compareArchives(a.archives, b.archives);
    const manifest = compareManifest(a.manifest, b.manifest);
    // Either half is enough to refuse the game: agreeing on the archives while
    // differing on the manifest is how two peers compose differently.
    if (archiveDifferences.length || manifest !== undefined)
      differences.push(
        manifest === undefined
          ? { modName, differences: archiveDifferences }
          : { modName, differences: archiveDifferences, manifest }
      );
  }
  return { differences, unknownMods };
}

export function checkArchiveAgreement(
  activeMods: string[],
  players: PlayerArchiveState[],
  referencePlayerId: number | undefined
): ArchiveAgreement {
  const reference =
    players.find(player => player.playerId === referencePlayerId) ??
    players.find(player => player.mods !== undefined);
  if (reference === undefined || reference.mods === undefined)
    return {
      uncheckedPlayerIds: players.map(player => player.playerId),
      mismatches: [],
    };
  const uncheckedPlayerIds: number[] = [],
    mismatches: ArchiveAgreement["mismatches"] = [];
  for (const player of players) {
    if (player.playerId === reference.playerId) continue;
    if (player.mods === undefined) {
      uncheckedPlayerIds.push(player.playerId);
      continue;
    }
    const { differences, unknownMods } = compareModSets(
      activeMods,
      reference.mods,
      player.mods
    );
    if (unknownMods.length) uncheckedPlayerIds.push(player.playerId);
    if (differences.length)
      mismatches.push({ playerId: player.playerId, mods: differences });
  }
  return { uncheckedPlayerIds, mismatches };
}
