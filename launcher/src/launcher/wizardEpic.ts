import { StateObservable, ofType } from "redux-observable";
import * as rx from "rxjs";
import * as rxop from "rxjs/operators";
import { AppAction } from "./actions";
import { State } from "./state";
import { EpicDependencies } from "./middleware/EpicDependencies";
import { NextAction, done, fail } from "./wizardActions";
import {
  automaticallySetUpTaMod,
  getRweModsPath,
  automaticallySetUpTaModManual,
} from "./util";
import path from "path";

export function wizardEpic(
  action$: rx.Observable<AppAction>,
  state$: StateObservable<State>,
  deps: EpicDependencies
): rx.Observable<AppAction> {
  const modsPath = getRweModsPath();
  const taModPath = path.join(modsPath, "ta");

  // The wizard is only ever opened on request. Importing game data through the
  // package library is the install path, so a missing mods/ta directory is not
  // on its own a reason to interrupt the player with this dialog.
  return action$.pipe(
    ofType<AppAction, NextAction["type"], NextAction>("wizard/NEXT"),
    rxop.concatMap(x =>
      rx
        .from(
          x.path
            ? automaticallySetUpTaModManual(taModPath, [x.path])
            : automaticallySetUpTaMod(taModPath)
        )
        .pipe(
          rxop.map(success => (success ? done() : fail())),
          // A failed lookup or copy must leave the working step, never spin.
          rxop.catchError(() => rx.of(fail()))
        )
    )
  );
}
