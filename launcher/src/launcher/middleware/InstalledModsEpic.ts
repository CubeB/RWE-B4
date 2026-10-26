import * as rx from "rxjs";
import * as rxop from "rxjs/operators";
import { execFile } from "child_process";
import { promises as fs } from "fs";
import { AppAction, receiveInstalledMods } from "../actions";
import { getRweModsPath, getRweUserPath } from "../util";
import {
  getInstalledMods,
  hasUniqueInspectionRoot,
  resourceDirectoriesForSelectedRoot,
} from "../../common/mods";
import path from "path";
import { InstalledModInfo } from "../state";

export async function inspectModResourceDirectories(
  mod: InstalledModInfo,
  setupExe: string
): Promise<InstalledModInfo> {
  let selectedRoot: string;
  try {
    // PACKAGE-009: the CLI reports a canonical physical root. Inspect that
    // same path so older setup binaries and junction aliases use one identity.
    selectedRoot = await fs.realpath(mod.path);
  } catch (error) {
    const detail = error instanceof Error ? error.message : String(error);
    return {
      ...mod,
      resourceDirectoryError: `Could not resolve selected data root: ${detail}`,
    };
  }
  return new Promise(resolve => {
    execFile(
      setupExe,
      ["--inspect-package", selectedRoot, "--json"],
      { windowsHide: true, maxBuffer: 16 * 1024 * 1024 },
      (error, stdout) => {
        // PACKAGE-003 inspection is read-only and bounded. Use only the single
        // discovered candidate whose root equals this filesystem-resolved
        // selected data path; an ambiguous nested root must not silently
        // configure another package.
        if (error) {
          resolve({
            ...mod,
            resourceDirectoryError: `Package inspection failed: ${error.message}`,
          });
          return;
        }
        let report: unknown;
        try {
          report = JSON.parse(stdout);
        } catch (parseError) {
          resolve({
            ...mod,
            resourceDirectoryError: `Package inspection returned invalid JSON: ${parseError}`,
          });
          return;
        }
        if (!hasUniqueInspectionRoot(report, selectedRoot)) {
          resolve({
            ...mod,
            resourceDirectoryError:
              "Package inspection did not identify this data root uniquely",
          });
          return;
        }
        const discovered = resourceDirectoriesForSelectedRoot(
          report,
          selectedRoot
        );
        // Explicit rwe_mod.json values override inferred roots for this package;
        // unknown metadata remains in the file and is never rewritten.
        resolve({
          ...mod,
          resourceDirectories: { ...discovered, ...mod.resourceDirectories },
          resourceDirectoryError: undefined,
        });
      }
    );
  });
}

async function discoverInstalledMods(): Promise<InstalledModInfo[]> {
  const mods = await getInstalledMods(
    getRweModsPath(),
    path.join(getRweUserPath(), "Data")
  );
  const home = process.env.RWE_HOME || path.dirname(process.execPath);
  const setupExe = path.join(
    home,
    `rwe_setup${process.platform === "win32" ? ".exe" : ""}`
  );
  // One bounded package inspection per discovered data root at launcher
  // startup. Vanilla packages with no recognized custom root remain unmapped.
  return Promise.all(
    mods.map(mod => inspectModResourceDirectories(mod, setupExe))
  );
}

export const installedModsEpic = (
  action$: rx.Observable<AppAction>
): rx.Observable<AppAction> => {
  return action$.pipe(
    rxop.filter(action => action.type === "REFRESH_INSTALLED_MODS"),
    rxop.startWith({ type: "REFRESH_INSTALLED_MODS" } as AppAction),
    rxop.switchMap(() =>
      rx.from(discoverInstalledMods()).pipe(
        rxop.map(receiveInstalledMods),
        rxop.catchError(error =>
          rx.of<AppAction>({
            type: "LAUNCHER_ERROR",
            message: `Could not read mods: ${error}`,
          })
        )
      )
    )
  );
};
