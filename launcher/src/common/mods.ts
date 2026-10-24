import * as fs from "fs";
import * as path from "path";
import { choose } from "./util";

export interface InstalledModResult {
  name: string;
  path: string;
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
}

function readRweModJson(path: string): RweModJson | undefined {
  try {
    const value = JSON.parse(fs.readFileSync(path, "utf8"));
    return value &&
      typeof value.shortname === "string" &&
      value.shortname.trim()
      ? (value as RweModJson)
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
