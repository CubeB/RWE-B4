import * as rx from "rxjs";
import * as rxop from "rxjs/operators";
import { AppAction, receiveInstalledMods } from "../actions";
import { getRweModsPath, getRweUserPath } from "../util";
import { getInstalledMods } from "../../common/mods";
import path from "path";

export const installedModsEpic = (
  action$: rx.Observable<AppAction>
): rx.Observable<AppAction> => {
  const modsPath = getRweModsPath();
  return action$.pipe(
    rxop.filter(action => action.type === "REFRESH_INSTALLED_MODS"),
    rxop.startWith({ type: "REFRESH_INSTALLED_MODS" } as AppAction),
    rxop.switchMap(() =>
      rx
        .from(getInstalledMods(modsPath, path.join(getRweUserPath(), "Data")))
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
