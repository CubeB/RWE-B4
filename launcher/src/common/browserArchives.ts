import type { ArchiveInfo, ModFingerprint } from "./archives";
import { sortArchivesInLoadOrder } from "./archiveOrder";

export interface ArchiveDifference {
  archiveName: string;
  kind: "missing" | "extra" | "different";
}
export interface ModDifference {
  modName: string;
  differences: ArchiveDifference[];
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

export function compareModSets(
  activeMods: string[],
  mine: ModFingerprint[],
  theirs: ModFingerprint[]
): ModSetComparison {
  const mineByName = new Map(mine.map(mod => [mod.name, mod.archives]));
  const theirsByName = new Map(theirs.map(mod => [mod.name, mod.archives]));
  const differences: ModDifference[] = [],
    unknownMods: string[] = [];
  for (const modName of activeMods) {
    const a = mineByName.get(modName),
      b = theirsByName.get(modName);
    if (a === undefined || b === undefined) {
      unknownMods.push(modName);
      continue;
    }
    const modDifferences = compareArchives(a, b);
    if (modDifferences.length)
      differences.push({ modName, differences: modDifferences });
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
