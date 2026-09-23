import * as crypto from "crypto";
import * as fs from "fs";
import * as path from "path";

/**
 * Detecting, hashing and comparing the game data two players are about to play
 * with. Issue #43.
 *
 * The mod name alone says nothing: two players can both have a mod called "ta"
 * and have put different archives in it, and the first they will hear of it is
 * a desync, or a map one of them does not have. What has to match is the
 * bytes, so each mod is reduced to the archives in it and the SHA-256 of each.
 *
 * Nothing here knows about Electron or about the lobby, so it runs in the
 * launcher, in the game server and in a test.
 */

/**
 * The archive extensions the engine loads, in the order it adds them to the
 * VFS and so in the order they take precedence: the plain directory first,
 * then these. CompositeVirtualFileSystem::readFile returns the first match, so
 * an entry in a .gp3 wins over the same entry in a .hpi.
 *
 * Kept in step with addToVfs in src/rwe/vfs/CompositeVirtualFileSystem.cpp,
 * which walks its own list backwards -- this is that list already reversed.
 */
export const archiveExtensions = [".gp3", ".gpf", ".ccx", ".ufo", ".hpi"];

/** One archive, as it is on disk. */
export interface ArchiveFile {
  /** The file name, as the engine and the other players see it. */
  name: string;
  path: string;
  size: number;
  mtimeMs: number;
}

/** One archive, as it goes to the other players. */
export interface ArchiveInfo {
  name: string;
  size: number;
  /** SHA-256, hex. */
  hash: string;
}

/** A mod's data, reduced to what has to match. */
export interface ModFingerprint {
  name: string;
  archives: ArchiveInfo[];
}

function isArchive(name: string): boolean {
  return archiveExtensions.includes(path.extname(name).toLowerCase());
}

/**
 * Sorts as the engine loads: by extension in the VFS's precedence order, then
 * by name.
 *
 * The engine takes the second part from fs::directory_iterator, whose order
 * the standard leaves unspecified, so this is not a reproduction of it so much
 * as a decision -- and it has to be a decision, because both players must
 * reduce the same files to the same list whatever their filesystems feel like
 * handing back. Name order within an extension is what they can agree on.
 */
export function sortArchivesInLoadOrder<T extends { name: string }>(
  archives: T[]
): T[] {
  return [...archives].sort((a, b) => {
    const extA = archiveExtensions.indexOf(path.extname(a.name).toLowerCase());
    const extB = archiveExtensions.indexOf(path.extname(b.name).toLowerCase());
    if (extA !== extB) {
      return extA - extB;
    }
    const nameA = a.name.toLowerCase();
    const nameB = b.name.toLowerCase();
    if (nameA === nameB) {
      return 0;
    }
    return nameA < nameB ? -1 : 1;
  });
}

/** The archives in one mod directory, in load order. A missing directory has none. */
export function listArchives(modDir: string): Promise<ArchiveFile[]> {
  return fs.promises
    .readdir(modDir, { withFileTypes: true })
    .catch((err: NodeJS.ErrnoException) => {
      if (err.code === "ENOENT" || err.code === "ENOTDIR") {
        return [] as fs.Dirent[];
      }
      throw err;
    })
    .then(entries =>
      Promise.all(
        entries
          .filter(e => e.isFile() && isArchive(e.name))
          .map(e => {
            const filePath = path.join(modDir, e.name);
            return fs.promises.stat(filePath).then(stats => ({
              name: e.name,
              path: filePath,
              size: stats.size,
              mtimeMs: stats.mtimeMs,
            }));
          })
      )
    )
    .then(sortArchivesInLoadOrder);
}

/**
 * SHA-256 of a file, read in a stream.
 *
 * Community packs run to gigabytes -- the V maps .ufo is 2.1 GB -- so this
 * never holds one in memory, and everything that calls it expects to wait.
 */
export function hashFile(filePath: string): Promise<string> {
  return new Promise<string>((resolve, reject) => {
    const hash = crypto.createHash("sha256");
    const stream = fs.createReadStream(filePath);
    stream.on("data", chunk => hash.update(chunk));
    stream.on("end", () => resolve(hash.digest("hex")));
    stream.on("error", reject);
  });
}

/**
 * What a hash was computed from, so a cached one can be trusted.
 *
 * Size and modification time rather than content, which is the whole point:
 * re-reading 2.1 GB to find out whether it changed would cost exactly what the
 * cache exists to avoid. It follows that a file edited within the filesystem's
 * timestamp granularity and keeping its size goes unnoticed -- the trade every
 * build system makes.
 */
export interface CachedHash {
  size: number;
  mtimeMs: number;
  hash: string;
}

export interface HashCache {
  [filePath: string]: CachedHash;
}

export function cachedHashIsValid(
  cached: CachedHash | undefined,
  file: ArchiveFile
): boolean {
  return (
    cached !== undefined &&
    cached.size === file.size &&
    cached.mtimeMs === file.mtimeMs
  );
}

/**
 * Hashes every archive in a mod, using the cache where it can and adding to it
 * where it cannot. The cache is mutated in place, so a caller that is
 * interrupted keeps whatever was finished.
 */
export async function fingerprintMod(
  name: string,
  modDir: string,
  cache: HashCache
): Promise<ModFingerprint> {
  const files = await listArchives(modDir);
  const archives: ArchiveInfo[] = [];

  for (const file of files) {
    const cached = cache[file.path];
    const hash = cachedHashIsValid(cached, file)
      ? cached.hash
      : await hashFile(file.path);
    cache[file.path] = { size: file.size, mtimeMs: file.mtimeMs, hash };
    archives.push({ name: file.name, size: file.size, hash });
  }

  return { name, archives };
}

/** How one player's copy of a mod differs from another's. */
export interface ArchiveDifference {
  archiveName: string;
  /** Relative to the second set: what it is missing, has extra, or has a different copy of. */
  kind: "missing" | "extra" | "different";
}

/**
 * Compares two copies of the same mod, naming every archive that differs
 * rather than only saying that something does.
 *
 * Names are compared without case, because the engine's own extension match is
 * case-insensitive and half the shipped archives are upper case.
 */
export function compareArchives(
  mine: ArchiveInfo[],
  theirs: ArchiveInfo[]
): ArchiveDifference[] {
  const theirsByName = new Map(theirs.map(a => [a.name.toLowerCase(), a]));
  const mineNames = new Set(mine.map(a => a.name.toLowerCase()));
  const differences: (ArchiveDifference & { name: string })[] = [];

  for (const archive of mine) {
    const other = theirsByName.get(archive.name.toLowerCase());
    if (other === undefined) {
      differences.push({
        archiveName: archive.name,
        kind: "missing",
        name: archive.name,
      });
    } else if (other.hash !== archive.hash) {
      differences.push({
        archiveName: archive.name,
        kind: "different",
        name: archive.name,
      });
    }
  }

  for (const archive of theirs) {
    if (!mineNames.has(archive.name.toLowerCase())) {
      differences.push({
        archiveName: archive.name,
        kind: "extra",
        name: archive.name,
      });
    }
  }

  return sortArchivesInLoadOrder(differences).map(({ archiveName, kind }) => ({
    archiveName,
    kind,
  }));
}

/** Which of the active mods two players disagree about, and how. */
export interface ModDifference {
  modName: string;
  differences: ArchiveDifference[];
}

export interface ModSetComparison {
  differences: ModDifference[];
  /** Active mods one side has not finished hashing, or does not have at all. */
  unknownMods: string[];
}

/**
 * Compares two players across the mods the game is actually going to use.
 *
 * A mod neither has active is not the lobby's business. A mod one of them has
 * not fingerprinted yet -- the hashing runs in the background and a 2 GB pack
 * takes a moment -- is reported as unknown rather than as a mismatch, so that a
 * slow disk does not read as a wrong file.
 */
export function compareModSets(
  activeMods: string[],
  mine: ModFingerprint[],
  theirs: ModFingerprint[]
): ModSetComparison {
  const mineByName = new Map(mine.map(m => [m.name, m.archives]));
  const theirsByName = new Map(theirs.map(m => [m.name, m.archives]));

  const differences: ModDifference[] = [];
  const unknownMods: string[] = [];

  for (const modName of activeMods) {
    const a = mineByName.get(modName);
    const b = theirsByName.get(modName);
    if (a === undefined || b === undefined) {
      unknownMods.push(modName);
      continue;
    }

    const modDifferences = compareArchives(a, b);
    if (modDifferences.length !== 0) {
      differences.push({ modName, differences: modDifferences });
    }
  }

  return { differences, unknownMods };
}

/** One player's archive data, as the room knows it. */
export interface PlayerArchiveState {
  playerId: number;
  /** Undefined until that player's hashing has finished and its message arrived. */
  mods: ModFingerprint[] | undefined;
}

export interface ArchiveAgreement {
  /** Players whose data could not be compared, so nothing can be concluded about them. */
  uncheckedPlayerIds: number[];
  /** Players whose data is known to differ from the reference player's. */
  mismatches: { playerId: number; mods: ModDifference[] }[];
}

/**
 * Whether everyone in a room is about to play with the same data.
 *
 * Everyone is compared against one reference player -- the host, whose copy is
 * what the game is going to be -- rather than against each other, so a room of
 * six says "you differ from the host" six times at worst instead of naming
 * fifteen pairs.
 *
 * A player is unchecked, not agreed, until their fingerprints arrive: hashing
 * runs in the background and a community pack takes a while, and a lobby that
 * let a game start during that window would be no gate at all. A mod an active
 * player has no fingerprint for at all is unchecked for the same reason --
 * whether it is missing or merely unhashed is a question the installed-mods
 * check already answers, and it answers it better.
 */
export function checkArchiveAgreement(
  activeMods: string[],
  players: PlayerArchiveState[],
  referencePlayerId: number | undefined
): ArchiveAgreement {
  const reference =
    players.find(p => p.playerId === referencePlayerId) ??
    players.find(p => p.mods !== undefined);

  if (reference === undefined || reference.mods === undefined) {
    return {
      uncheckedPlayerIds: players.map(p => p.playerId),
      mismatches: [],
    };
  }

  const referenceMods = reference.mods;
  const uncheckedPlayerIds: number[] = [];
  const mismatches: { playerId: number; mods: ModDifference[] }[] = [];

  for (const player of players) {
    if (player.playerId === reference.playerId) {
      continue;
    }
    if (player.mods === undefined) {
      uncheckedPlayerIds.push(player.playerId);
      continue;
    }

    const { differences, unknownMods } = compareModSets(
      activeMods,
      referenceMods,
      player.mods
    );
    if (unknownMods.length !== 0) {
      uncheckedPlayerIds.push(player.playerId);
    }
    if (differences.length !== 0) {
      mismatches.push({ playerId: player.playerId, mods: differences });
    }
  }

  return { uncheckedPlayerIds, mismatches };
}

/**
 * A short label for one player's copy of the game data, for the lobby to show
 * beside their name.
 *
 * Over the active mods only: two players whose unrelated installed mods differ
 * are still going to play the same game, and a label that said otherwise would
 * be teaching players to ignore it. Six hex digits, because this is read by a
 * human comparing it with the one next to it and not by anything that has to
 * be sure -- the refusal to start is what is sure.
 */
export function archiveDigest(
  activeMods: string[],
  mods: ModFingerprint[]
): string {
  const byName = new Map(mods.map(m => [m.name, m.archives]));
  const canonical = [...activeMods]
    .sort()
    .map(modName => {
      const archives = byName.get(modName);
      if (archives === undefined) {
        return `${modName}:?`;
      }
      return sortArchivesInLoadOrder(archives)
        .map(a => `${modName}/${a.name.toLowerCase()}:${a.hash}`)
        .join("\n");
    })
    .join("\n");

  return crypto
    .createHash("sha256")
    .update(canonical)
    .digest("hex")
    .slice(0, 6);
}
