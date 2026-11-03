import * as crypto from "crypto";
import * as fs from "fs";
import * as path from "path";
import {
  archiveExtensions,
  manifestFileName,
  sortArchivesInLoadOrder,
} from "./archiveOrder";
export {
  archiveExtensions,
  manifestFileName,
  sortArchivesInLoadOrder,
} from "./archiveOrder";

/**
 * Detecting, hashing and comparing the game data two players are about to play
 * with. Issue #43.
 *
 * The mod name alone says nothing: two players can both have a mod called "ta"
 * and have put different archives in it, and the first they will hear of it is
 * a desync, or a map one of them does not have. What has to match is the
 * bytes, so each mod is reduced to the archives in it and the SHA-256 of each,
 * and to its author manifest's bytes when it has one (MOD-SDK-002/MOD-SDK-003).
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

/**
 * The author manifest, as it goes to the other players.
 *
 * Format 1 of the manifest is `rwe_mod.json` in the mod directory. Its bytes
 * are part of what two peers must agree on: two mods whose archives are
 * identical but whose manifests differ can resolve to different load orders,
 * options or scripts, and agreeing on the archives alone would let that
 * through. MOD-SDK-003 records why the raw bytes are hashed rather than the
 * fields: an unknown key may matter to a future SDK, and re-serializing would
 * make whitespace and key order invisible.
 *
 * There is no `name`: a mod has at most one manifest, and `manifestFileName`
 * is the only file it can be.
 */
export interface ManifestInfo {
  size: number;
  /** SHA-256 of the manifest's bytes, hex. */
  hash: string;
}

/** A mod's data, reduced to what has to match. */
export interface ModFingerprint {
  name: string;
  archives: ArchiveInfo[];
  /**
   * The author manifest, or absent when the mod has none.
   *
   * Absent is not the same as an empty file: a legacy archive-only package and
   * a package that declares a manifest are different content, and the
   * comparison between players treats them as such.
   */
  manifest?: ManifestInfo;
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
 * The mod's author manifest, if it has one. A missing mod directory or a
 * missing manifest both mean "no manifest", which is a legacy package.
 */
export function listManifest(modDir: string): Promise<ArchiveFile | undefined> {
  const filePath = path.join(modDir, manifestFileName);
  return fs.promises
    .stat(filePath)
    .then(stats =>
      stats.isFile()
        ? {
            name: manifestFileName,
            path: filePath,
            size: stats.size,
            mtimeMs: stats.mtimeMs,
          }
        : undefined
    )
    .catch((err: NodeJS.ErrnoException) => {
      if (err.code === "ENOENT" || err.code === "ENOTDIR") {
        return undefined;
      }
      throw err;
    });
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
 * The hash of one file, from the cache where it can be trusted and from the
 * file where it cannot, leaving the cache holding what was used.
 */
async function cachedFileHash(
  file: ArchiveFile,
  cache: HashCache
): Promise<string> {
  const cached = cache[file.path];
  const hash = cachedHashIsValid(cached, file)
    ? cached.hash
    : await hashFile(file.path);
  cache[file.path] = { size: file.size, mtimeMs: file.mtimeMs, hash };
  return hash;
}

/**
 * Hashes every archive in a mod, using the cache where it can and adding to it
 * where it cannot. The cache is mutated in place, so a caller that is
 * interrupted keeps whatever was finished.
 *
 * The author manifest is hashed the same way and by the same cache, so it is
 * covered without a second hasher or a second cache file. A mod with no
 * manifest gets no `manifest` key at all: its fingerprint is exactly what it
 * was before manifests were covered.
 */
export async function fingerprintMod(
  name: string,
  modDir: string,
  cache: HashCache
): Promise<ModFingerprint> {
  const files = await listArchives(modDir);
  const archives: ArchiveInfo[] = [];

  for (const file of files) {
    archives.push({
      name: file.name,
      size: file.size,
      hash: await cachedFileHash(file, cache),
    });
  }

  const manifestFile = await listManifest(modDir);
  if (manifestFile === undefined) {
    return { name, archives };
  }

  return {
    name,
    archives,
    manifest: {
      size: manifestFile.size,
      hash: await cachedFileHash(manifestFile, cache),
    },
  };
}

export type {
  ArchiveAgreement,
  ArchiveDifference,
  ManifestDifference,
  ModDifference,
  ModSetComparison,
  PlayerArchiveState,
} from "./browserArchives";
export {
  checkArchiveAgreement,
  compareArchives,
  compareManifest,
  compareModSets,
} from "./browserArchives";

/**
 * A short label for one player's copy of the game data, for the lobby to show
 * beside their name.
 *
 * Over the active mods only: two players whose unrelated installed mods differ
 * are still going to play the same game, and a label that said otherwise would
 * be teaching players to ignore it. Six hex digits, because this is read by a
 * human comparing it with the one next to it and not by anything that has to
 * be sure -- the refusal to start is what is sure.
 *
 * The manifest joins the label when the mod has one, so a label cannot say two
 * players are alike while the refusal says they are not. A mod without a
 * manifest contributes exactly what it always did, and its label is unchanged.
 */
export function archiveDigest(
  activeMods: string[],
  mods: ModFingerprint[]
): string {
  const byName = new Map(mods.map(m => [m.name, m]));
  const canonical = [...activeMods]
    .sort()
    .map(modName => {
      const mod = byName.get(modName);
      if (mod === undefined) {
        return `${modName}:?`;
      }
      return [
        ...sortArchivesInLoadOrder(mod.archives).map(
          a => `${modName}/${a.name.toLowerCase()}:${a.hash}`
        ),
        ...(mod.manifest === undefined
          ? []
          : [`${modName}/${manifestFileName}:${mod.manifest.hash}`]),
      ].join("\n");
    })
    .join("\n");

  return crypto
    .createHash("sha256")
    .update(canonical)
    .digest("hex")
    .slice(0, 6);
}
