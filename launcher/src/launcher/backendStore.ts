import path from "path";
import { configureStore, Middleware, StoreEnhancer } from "@reduxjs/toolkit";
import { createEpicMiddleware } from "redux-observable";
import * as rx from "rxjs";
import * as rxop from "rxjs/operators";
import { AppAction, gameEnded } from "./actions";
import {
  defaultPreferences,
  loadPreferences,
  savePreferences,
} from "./preferences";
import { State, SideEffect } from "./state";
import { GameClientService } from "../game-server/game-client";
import { RweBridge } from "./bridge";
import { MasterClientService } from "../master-server/master-client";
import { masterServer } from "../common/util";
import { EpicDependencies } from "./middleware/EpicDependencies";
import { rootEpic } from "./middleware/RootEpic";
import { createEnhancer } from "./sideEffects";
import { execRwe } from "./rwe";
import rootReducer from "./reducers";
import { getRweUserPath } from "./util";

// DOWNLOAD-001: launcher services now live in the Node backend; this is
// distribution-only RWE-original work, preserving the existing launcher logic.
export function createBackendStore() {
  const masterClientService = new MasterClientService();
  masterClientService.connectToServer(`${masterServer()}/master`);
  const bridgeService = new RweBridge();
  const clientService = new GameClientService();
  const epicMiddleware = createEpicMiddleware<
    AppAction,
    AppAction,
    State,
    EpicDependencies
  >({
    dependencies: {
      clientService,
      masterClentService: masterClientService,
      bridgeService,
    },
  });
  // eslint-disable-next-line prefer-const
  let store: ReturnType<typeof configureStore>;
  const executeSideEffect = (effect: SideEffect) => {
    if (effect.type === "LAUNCH_RWE") {
      return rx
        .from(execRwe(effect.args).finished)
        .pipe(
          rxop.mapTo(undefined),
          rxop.catchError(error => {
            store.dispatch({
              type: "LAUNCHER_ERROR",
              message: String(error),
            } as any);
            return rx.of(undefined);
          }),
          rxop.mapTo(gameEnded())
        )
        .subscribe(store.dispatch);
    }
  };
  const preferencesPath = path.join(getRweUserPath(), "launcher.json");
  const persistPreferences: Middleware<unknown, State> =
    api => next => action => {
      const result = next(action);
      if (
        [
          "SET_PLAYER_NAME",
          "SET_COMMUNITY_PATCH",
          "CHANGE_SINGLE_PLAYER_MODS",
          "HOST_GAME_FORM_CONFIRM",
          "JOIN_SELECTED_GAME_CONFIRM",
        ].includes((action as any).type)
      ) {
        const state = api.getState();
        try {
          savePreferences(preferencesPath, {
            playerName: state.playerName || "Player",
            activeMods: state.activeMods,
            communityPatch: state.communityPatch,
          });
          api.dispatch({ type: "LAUNCHER_ERROR", message: undefined });
        } catch (error) {
          api.dispatch({
            type: "LAUNCHER_ERROR",
            message: `Could not save launcher settings: ${error}`,
          });
        }
      }
      return result;
    };
  store = configureStore({
    reducer: rootReducer as any,
    middleware: getDefault =>
      getDefault().concat(
        persistPreferences as any,
        epicMiddleware as any
      ) as any,
    enhancers: defaults =>
      defaults.concat(createEnhancer(executeSideEffect) as StoreEnhancer),
  });
  try {
    store.dispatch({
      type: "RECEIVE_LAUNCHER_PREFERENCES",
      ...loadPreferences(preferencesPath),
    } as any);
  } catch (error) {
    store.dispatch({
      type: "RECEIVE_LAUNCHER_PREFERENCES",
      ...defaultPreferences,
    } as any);
    store.dispatch({
      type: "LAUNCHER_ERROR",
      message: `Could not read launcher settings: ${error}`,
    } as any);
  }
  epicMiddleware.run(rootEpic);
  return {
    store,
    close: () => {
      bridgeService.close();
      clientService.disconnect();
      masterClientService.disconnect();
    },
  };
}
