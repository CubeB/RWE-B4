import * as rx from "rxjs";
import * as rxop from "rxjs/operators";
import { execFile } from "child_process";
import { AppAction, receiveInstalledMods } from "../actions";
import { getRweModsPath, getRweUserPath } from "../util";
import { getInstalledMods, resourceDirectoriesForSelectedRoot } from "../../common/mods";
import path from "path";
import { InstalledModInfo } from "../state";

function inspectModResourceDirectories(mod: InstalledModInfo, setupExe: string): Promise<InstalledModInfo> {
  return new Promise(resolve => {
    execFile(
      setupExe,
      ["--inspect-package", mod.path, "--json"],
      { windowsHide: true, maxBuffer: 16 * 1024 * 1024 },
      (_error, stdout) => {
      // PACKAGE-003 inspection is read-only and bounded. Use only the single
      // discovered candidate whose root equals this selected data path; an
      // ambiguous nested root must not silently configure another package.
      let discovered;
      try {
        discovered = resourceDirectoriesForSelectedRoot(JSON.parse(stdout), mod.path);
      } catch {
        discovered = undefined;
      }
      // Explicit rwe_mod.json values override inferred roots for this package;
      // unknown metadata remains in the file and is never rewritten.
      resolve({
        ...mod,
        resourceDirectories: { ...discovered, ...mod.resourceDirectories },
      });
    });
  });
}

async function discoverInstalledMods(): Promise<InstalledModInfo[]> {
  const mods = await getInstalledMods(getRweModsPath(), path.join(getRweUserPath(), "Data"));
  const home = process.env.RWE_HOME || path.dirname(process.execPath);
  const setupExe = path.join(home, `rwe_setup${process.platform === "win32" ? ".exe" : ""}`);
  // One bounded package inspection per discovered data root at launcher
  // startup. Vanilla packages with no recognized custom root remain unmapped.
  return Promise.all(mods.map(mod => inspectModResourceDirectories(mod, setupExe)));
}

export const installedModsEpic = (
  action$: rx.Observable<AppAction>
): rx.Observable<AppAction> => {
  return action$.pipe(
    rxop.filter(action => action.type === "REFRESH_INSTALLED_MODS"),
    rxop.startWith({ type: "REFRESH_INSTALLED_MODS" } as AppAction),
    rxop.switchMap(() =>
      rx
        .from(discoverInstalledMods())
        .pipe(
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
