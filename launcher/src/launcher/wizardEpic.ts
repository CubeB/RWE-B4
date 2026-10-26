import { StateObservable } from "redux-observable";
import * as rx from "rxjs";
import * as rxop from "rxjs/operators";
import { AppAction } from "./actions";
import { State } from "./state";
import { EpicDependencies } from "./middleware/EpicDependencies";
import { NextAction, done, fail, open, startAutomatic } from "./wizardActions";
import {
  automaticallyImportTaData,
  checkExistingTaData,
  hasUsableTaMod,
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
  const setupExe = path.join(
    process.env.RWE_HOME || path.dirname(process.execPath),
    `rwe_setup${process.platform === "win32" ? ".exe" : ""}`
  );

  let startupHandled = false;
  let importInFlight = false;
  return action$.pipe(
    rxop.mergeMap((action: AppAction) => {
      if (action.type === "RECEIVE_INSTALLED_MODS") {
        if (startupHandled) return rx.EMPTY;
        startupHandled = true;
        importInFlight = true;
        // Existing usable data wins: don't import again and don't interrupt startup.
        return rx.from(checkExistingTaData(setupExe)).pipe(
          rxop.concatMap(alreadyAvailable => {
            const configuredTa = action.mods.find(mod => mod.name === "ta");
            return (
              alreadyAvailable
                ? rx.of(true)
                : configuredTa
                  ? rx.from(hasUsableTaMod(configuredTa.path))
                  : rx.of(false)
            ).pipe(
              rxop.concatMap(configuredDataUsable =>
                alreadyAvailable
                  ? rx.of({ type: "REFRESH_INSTALLED_MODS" } as AppAction)
                  : configuredDataUsable
                    ? rx.EMPTY
                    : rx.concat(
                        rx.of(open(), startAutomatic()),
                        rx.from(automaticallyImportTaData(setupExe)).pipe(
                          rxop.concatMap(error =>
                            error
                              ? rx.of(
                                  fail(
                                    `Automatic import could not finish: ${error}`
                                  )
                                )
                              : rx.of(done(), {
                                  type: "REFRESH_INSTALLED_MODS",
                                } as AppAction)
                          ),
                          rxop.catchError(error =>
                            rx.of(
                              fail(`Automatic import failed: ${String(error)}`)
                            )
                          )
                        )
                      )
              )
            );
          }),
          rxop.finalize(() => {
            importInFlight = false;
          })
        );
      }
      if (action.type === "wizard/START_AUTOMATIC") {
        // The reducer-only transition is emitted by startup; import runs from its
        // original startup branch so it cannot be mistaken for manual Next.
        return rx.EMPTY;
      }
      if (action.type !== "wizard/NEXT") return rx.EMPTY;
      if (importInFlight) return rx.EMPTY;
      importInFlight = true;
      const x = action as NextAction;
      return rx
        .defer(() => {
          if (x.path) {
            return rx
              .from(automaticallySetUpTaModManual(taModPath, [x.path]))
              .pipe(
                rxop.map(success => ({ kind: "manual" as const, success }))
              );
          }
          return rx
            .from(automaticallyImportTaData(setupExe))
            .pipe(rxop.map(error => ({ kind: "automatic" as const, error })));
        })
        .pipe(
          rxop.concatMap(result => {
            if (result.kind === "manual") {
              return result.success
                ? rx.of(done(), { type: "REFRESH_INSTALLED_MODS" } as AppAction)
                : rx.of(
                    fail(
                      "The selected folder did not contain all required game data."
                    )
                  );
            }
            return result.error
              ? rx.of(
                  fail(`Automatic import could not finish: ${result.error}`)
                )
              : rx.of(done(), { type: "REFRESH_INSTALLED_MODS" } as AppAction);
          }),
          rxop.catchError(error =>
            rx.of(
              fail(
                x.path
                  ? `Could not copy the selected game data: ${String(error)}`
                  : `Automatic import failed: ${String(error)}`
              )
            )
          ),
          rxop.finalize(() => {
            importInFlight = false;
          })
        );
    })
  );
}
