import fs from "fs";
import path from "path";
import crypto from "crypto";
import * as rx from "rxjs";
import * as rxop from "rxjs/operators";
import { chooseOp } from "../common/rxutil";
import { RweModJson } from "../common/mods";
import { enumerateValues, HKEY, RegistryValueType } from "./registry";

const taModFiles = new Map<string, string>([
  [
    "c3890a78e627e6e334f671f65ec418cd40d15ad2ad4110e931bb3dfced0814c3",
    "totala1.hpi",
  ],
  [
    "043e321169f73157812ec8e65b5fae29898ae3f41e527bc419801d18cf0cbe63",
    "totala2.hpi",
  ],
  [
    "981cf35f05979e800468e9e7b480e474ee626e7b5d1c721103bc69a66ee0aed8",
    "CCDATA.CCX",
  ],
  [
    "43b0029b3943edb290487d055e2b3c6df12f7d02ebf33ab5f2dda6fb33ccc70e",
    "CCMAPS.CCX",
  ],
  [
    "fa0da77b8fb4400ce606995703dec42eeab5b05d04ca8c0e43be324f04ce0441",
    "rev31.gp3",
  ],
]);

const taPathRegistryLocations: [HKEY, string, string][] = [
  [
    HKEY.HKEY_LOCAL_MACHINE,
    "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 298030",
    "InstallLocation",
  ],
  [HKEY.HKEY_LOCAL_MACHINE, "SOFTWARE\\GOG.com\\GOGTOTALANNIHILATION", "PATH"],
  [
    HKEY.HKEY_LOCAL_MACHINE,
    "SOFTWARE\\GOG.com\\GOGTOTALANNIHILATIONBT",
    "PATH",
  ],
  [
    HKEY.HKEY_LOCAL_MACHINE,
    "SOFTWARE\\GOG.com\\GOGTOTALANNIHILATIONCC",
    "PATH",
  ],
  [
    HKEY.HKEY_LOCAL_MACHINE,
    "SOFTWARE\\Wow6432Node\\GOG.com\\GOGTOTALANNIHILATION",
    "PATH",
  ],
  [
    HKEY.HKEY_LOCAL_MACHINE,
    "SOFTWARE\\Wow6432Node\\GOG.com\\GOGTOTALANNIHILATIONBT",
    "PATH",
  ],
  [
    HKEY.HKEY_LOCAL_MACHINE,
    "SOFTWARE\\Wow6432Node\\GOG.com\\GOGTOTALANNIHILATIONCC",
    "PATH",
  ],
  [
    HKEY.HKEY_LOCAL_MACHINE,
    "SOFTWARE\\Microsoft\\DirectPlay\\Applications\\Total Annihilation",
    "Path",
  ],
  [
    HKEY.HKEY_LOCAL_MACHINE,
    "SOFTWARE\\Wow6432Node\\Microsoft\\DirectPlay\\Applications\\Total Annihilation",
    "Path",
  ],
  [
    HKEY.HKEY_LOCAL_MACHINE,
    "HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GOGPACKTOTALANNIHILATIONCOMMANDERPACK_is1",
    "InstallLocation",
  ],
  [
    HKEY.HKEY_LOCAL_MACHINE,
    "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GOGPACKTOTALANNIHILATIONCOMMANDERPACK_is1",
    "InstallLocation",
  ],
  [
    HKEY.HKEY_LOCAL_MACHINE,
    "HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Total Annihilation",
    "Dir",
  ],
];

function getSearchDirectories(): Promise<string[]> {
  if (process.platform !== "win32") {
    return Promise.resolve([]);
  }
  const rawPaths = [...getHardcodedTaPathGuesses()];

  return findRegistryTaInstallLocations().then(paths => {
    rawPaths.push(...paths);
    const normalizedPaths = rawPaths.map(x => path.resolve(x.toUpperCase()));
    const deduplicatedPaths = [...new Set(normalizedPaths).values()];
    return deduplicatedPaths;
  });
}

function getHardcodedTaPathGuesses(): string[] {
  const systemDrive = process.env["SYSTEMDRIVE"] || "C:";
  const programFiles =
    process.env["PROGRAMFILES"] || path.join(systemDrive, "Program Files");
  const programFilesX86 =
    process.env["ProgramFiles(x86)"] ||
    path.join(systemDrive, "Program Files (x86)");

  return [
    path.join(systemDrive, "CAVEDOG", "TOTALA"),
    path.join(
      programFilesX86,
      "Steam",
      "SteamApps",
      "common",
      "Total Annihilation"
    ),
    path.join(
      programFiles,
      "Steam",
      "SteamApps",
      "common",
      "Total Annihilation"
    ),
    path.join(systemDrive, "GOG Games", "Total Annihilation - Commander Pack"),
  ];
}

/** May contain duplicate paths -- it's up to the caller to dedupe them. */
function findRegistryTaInstallLocations(): Promise<string[]> {
  const promises = taPathRegistryLocations.map(([hkey, key, name]) =>
    enumerateValues(hkey, key).then(values => {
      const installPathEntry = values.find(x => x.name === name);
      if (
        installPathEntry &&
        installPathEntry.type === RegistryValueType.REG_SZ
      ) {
        return [installPathEntry.data];
      }
      return [];
    })
  );

  return Promise.all(promises).then(values => values.flat());
}

export function automaticallySetUpTaMod(outDir: string): Promise<boolean> {
  return getSearchDirectories().then(searchDirectories => {
    console.log("automatically setting up mods");
    console.log(searchDirectories);
    return automaticallySetUpTaModManual(outDir, searchDirectories);
  });
}

function toMapOp<K, V>(): rx.OperatorFunction<readonly [K, V], Map<K, V>> {
  return rxop.reduce((m, [k, v]) => {
    m.set(k, v);
    return m;
  }, new Map<K, V>());
}

function translateOp<K, J, V>(
  mapping: Map<J, V>
): rx.OperatorFunction<readonly [K, J], readonly [K, V]> {
  return chooseOp(([k, j]) => {
    const v = mapping.get(j);
    if (!v) {
      return undefined;
    }
    return [k, v] as const;
  });
}

export function automaticallySetUpTaModManual(
  outDir: string,
  searchDirectories: string[]
): Promise<boolean> {
  return new Promise((resolve, reject) => {
    rx.from(searchDirectories)
      .pipe(
        rxop.concatMap(getHashes),
        rxop.tap(x => console.log(x)),
        translateOp(taModFiles),
        toMapOp(),
        rxop.tap(x => console.log(x)),
        rxop.map(x => (hasAllValues(x, taModFiles) ? x : undefined))
      )
      .subscribe({
        next: info => {
          if (info) {
            createMod(outDir, info);
            resolve(true);
          } else {
            resolve(false);
          }
        },
        error: err => {
          reject(err);
        },
      });
  });
}

function hasAllValues<K, V>(a: Map<K, V>, b: Map<K, V>): boolean {
  const aS = new Set(a.values());
  const bS = new Set(b.values());
  for (const v of bS) {
    if (!aS.has(v)) {
      return false;
    }
  }
  return true;
}

function getHash(filePath: string): Promise<string> {
  return new Promise<string>((resolve, reject) => {
    const stream = fs
      .createReadStream(filePath)
      .pipe(crypto.createHash("sha256"));
    stream.on("data", data => {
      resolve(data.toString("hex"));
    });
    stream.on("error", err => {
      reject(err);
    });
  });
}

function readDir(dir: string): Promise<fs.Dirent[]> {
  return new Promise<fs.Dirent[]>((resolve, reject) => {
    fs.readdir(dir, { withFileTypes: true }, (err, data) => {
      if (err) {
        if (err.code === "ENOENT") {
          resolve([]);
        } else {
          reject(err);
        }
      } else {
        resolve(data);
      }
    });
  });
}

type DirentPair = [string, fs.Dirent[]];
type FlatDirentPair = [string, fs.Dirent];

function getHashes(
  searchDirectory: string
): rx.Observable<readonly [string, string]> {
  return rx.from(readDir(searchDirectory)).pipe(
    rxop.map<fs.Dirent[], DirentPair>(entries => [searchDirectory, entries]),
    rxop.catchError<DirentPair, rx.Observable<DirentPair>>(err => {
      if (err.code === "ENOTDIR") {
        const parentName = path.dirname(searchDirectory);
        return rx
          .from(readDir(path.dirname(searchDirectory)))
          .pipe(rxop.map<fs.Dirent[], DirentPair>(x => [parentName, x]));
      }
      throw err;
    }),
    rxop.concatMap<DirentPair, rx.Observable<[string, fs.Dirent]>>(x =>
      rx.from(x[1].map<FlatDirentPair>(y => [x[0], y]))
    ),
    rxop.filter(x => x[1].isFile()),
    rxop.map(f => path.join(f[0], f[1].name)),
    rxop.concatMap(f => rx.from(getHash(f).then(hash => [f, hash] as const)))
  );
}

function createMod(outPath: string, filePaths: Map<string, string>): void {
  fs.mkdirSync(outPath, { recursive: true });
  for (const [sourcePath, name] of filePaths.entries()) {
    fs.copyFileSync(sourcePath, path.join(outPath, name));
  }

  const manifest: RweModJson = {
    shortname: "ta",
    name: "Total Annihilation",
    author: "Cavedog Entertainment",
  };

  fs.writeFileSync(
    path.join(outPath, "rwe_mod.json"),
    JSON.stringify(manifest)
  );
}

/**
 * The places the user's RWE directory may be, most preferred first.
 * Windows: %APPDATA%\RWE. Elsewhere: "rwe" under the XDG data home, which is
 * ~/.local/share unless XDG_DATA_HOME is set and absolute (issue #216), then
 * the legacy ~/.rwe. The engine applies the same rule in src/rwe/util.cpp;
 * keep the two in step.
 */
export function rweUserPathCandidates(
  platform: NodeJS.Platform,
  env: NodeJS.ProcessEnv
): string[] {
  if (platform === "win32") {
    const appData = env["APPDATA"];
    if (appData === undefined) {
      throw new Error("Failed to find AppData path");
    }
    return [path.join(appData, "RWE")];
  }
  const home = env["HOME"];
  if (home === undefined || home === "") {
    throw new Error("Failed to find home directory");
  }
  const xdgDataHome = env["XDG_DATA_HOME"];
  const dataHome =
    xdgDataHome !== undefined &&
    xdgDataHome !== "" &&
    path.isAbsolute(xdgDataHome)
      ? xdgDataHome
      : path.join(home, ".local", "share");
  return [path.join(dataHome, "rwe"), path.join(home, ".rwe")];
}

/**
 * The first candidate that exists, or the first candidate when none does: a
 * fresh install lands in the new place, an old one stays where its files are.
 */
export function chooseRweUserPath(
  candidates: string[],
  exists: (p: string) => boolean
): string {
  const found = candidates.find(exists);
  return found !== undefined ? found : candidates[0];
}

export function getRweUserPath(): string {
  return chooseRweUserPath(
    rweUserPathCandidates(process.platform, process.env),
    (p) => {
      try {
        return fs.statSync(p).isDirectory();
      } catch {
        return false;
      }
    }
  );
}

export function getRweModsPath(): string {
  return path.join(getRweUserPath(), "mods");
}

export function getRweConfigPath(): string {
  return path.join(getRweUserPath(), "rwe.cfg");
}
