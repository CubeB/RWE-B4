import * as fs from "fs";
import * as path from "path";
import { choose } from "./util";

export const resourceDirectoryKeys = [
  "ai", "anims", "bitmaps", "builderGuis", "camps", "downloads",
  "fonts", "gamedata", "guis", "maps", "objects3d", "palettes",
  "scripts", "sounds", "textures", "unitpics", "units", "weapons",
] as const;
export type ResourceDirectoryKey = typeof resourceDirectoryKeys[number];
export type ResourceDirectories = Partial<Record<ResourceDirectoryKey, string>>;

export interface InstalledModResult {
  name: string;
  path: string;
  resourceDirectories?: ResourceDirectories;
}
export function getInstalledMods(
  baseDir: string,
  baseGameDataDir?: string
): Promise<InstalledModResult[]> {
  return new Promise<fs.Dirent[]>((resolve, reject) => {
    fs.readdir(baseDir, { withFileTypes: true }, (err, files) => {
      if (err) {
        if (err.code === "ENOENT") {
          resolve([]);
        } else {
          reject(err);
        }
      } else {
        resolve(files);
      }
    });
  }).then(entries => {
    const mappedEntries = entries
      .sort((a, b) => a.name.localeCompare(b.name))
      .filter(e => e.isDirectory())
      .map<InstalledModResult | undefined>(e => {
        const modPath = path.join(baseDir, e.name);
        const json = readRweModJson(path.join(modPath, "rwe_mod.json"));
        if (!json && !hasArchives(modPath)) {
          return undefined;
        }
        return {
          name: json?.shortname || e.name,
          path: modPath,
          resourceDirectories: json?.resourceDirectories,
        };
      });

    const mods = choose(mappedEntries, x => x);
    if (baseGameDataDir && hasArchives(baseGameDataDir)) {
      return [
        { name: "ta", path: baseGameDataDir },
        ...mods.filter(mod => mod.name !== "ta"),
      ];
    }
    return mods.filter(
      (mod, index) => mods.findIndex(other => other.name === mod.name) === index
    );
  });
}

export interface RweModJson {
  shortname: string;
  name: string;
  author: string;
  resourceDirectories?: ResourceDirectories;
}

/** Keep unknown rwe_mod.json metadata untouched; only forward safe known roots. */
export function parseResourceDirectories(value: unknown): ResourceDirectories | undefined {
  if (typeof value !== "object" || value === null || Array.isArray(value)) {
    return undefined;
  }

  const result: ResourceDirectories = {};
  for (const key of resourceDirectoryKeys) {
    const candidate = (value as Record<string, unknown>)[key];
    if (typeof candidate === "string" && isSafeResourceDirectory(candidate)) {
      result[key] = candidate;
    }
  }
  return Object.keys(result).length ? result : undefined;
}

function isSafeResourceDirectory(value: string): boolean {
  const trimmed = value.trim();
  if (!trimmed || path.isAbsolute(trimmed) || /^[a-z]:/i.test(trimmed)) {
    return false;
  }
  return trimmed.split(/[\\/]/).every(part => part.length > 0 && part !== "." && part !== "..");
}

function canonicalPath(value: string): string {
  return path.resolve(value).replace(/[\\/]+$/, "").toLowerCase();
}

/** Accept discovered roots only from the single package candidate matching the selected data path. */
export function resourceDirectoriesForSelectedRoot(
  report: unknown,
  selectedRoot: string
): ResourceDirectories | undefined {
  if (typeof report !== "object" || report === null || Array.isArray(report)) {
    return undefined;
  }
  const candidates = (report as { candidates?: unknown }).candidates;
  if (!Array.isArray(candidates)) {
    return undefined;
  }
  const matching = candidates.filter(candidate => {
    if (typeof candidate !== "object" || candidate === null || Array.isArray(candidate)) {
      return false;
    }
    const root = (candidate as Record<string, unknown>).root;
    return typeof root === "string" && canonicalPath(root) === canonicalPath(selectedRoot);
  });
  if (matching.length !== 1) {
    return undefined;
  }
  return parseResourceDirectories((matching[0] as Record<string, unknown>).resourceDirectories);
}

/** Merge paths in data-path precedence order and report conflicting package claims. */
export function mergeResourceDirectories(
  mods: Array<Pick<InstalledModResult, "name" | "resourceDirectories">>
): { resourceDirectories: ResourceDirectories; conflict?: string } {
  const resolved: ResourceDirectories = {};
  const owners: Partial<Record<ResourceDirectoryKey, string>> = {};
  for (const mod of mods) {
    for (const key of resourceDirectoryKeys) {
      const value = mod.resourceDirectories?.[key];
      if (!value) continue;
      if (resolved[key] && resolved[key]!.toLowerCase() !== value.toLowerCase()) {
        return {
          resourceDirectories: resolved,
          conflict: `${key} is mapped to both ${resolved[key]} (${owners[key]}) and ${value} (${mod.name})`,
        };
      }
      resolved[key] = value;
      owners[key] = mod.name;
    }
  }
  return { resourceDirectories: resolved };
}

function readRweModJson(filename: string): RweModJson | undefined {
  try {
    const value = JSON.parse(fs.readFileSync(filename, "utf8"));
    return value &&
      typeof value.shortname === "string" &&
      value.shortname.trim()
      ? { ...value, resourceDirectories: parseResourceDirectories(value.resourceDirectories) } as RweModJson
      : undefined;
  } catch {
    return undefined;
  }
}

function hasArchives(directory: string): boolean {
  try {
    return fs
      .readdirSync(directory, { withFileTypes: true })
      .some(
        entry => entry.isFile() && /\.(hpi|ufo|ccx|gpf|gp3)$/i.test(entry.name)
      );
  } catch {
    return false;
  }
}
