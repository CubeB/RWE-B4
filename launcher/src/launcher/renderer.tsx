import CssBaseline from "@mui/material/CssBaseline";
import * as React from "react";
import { createRoot } from "react-dom/client";
import { Provider } from "react-redux";
import { configureStore } from "@reduxjs/toolkit";
import { Store, StoreEnhancer, Middleware } from "redux";
import { ThemeProvider, createTheme } from "@mui/material/styles";
import path from "path";
import { getRweUserPath } from "./util";
import {
  defaultPreferences,
  loadPreferences,
  savePreferences,
} from "./preferences";
import { createEpicMiddleware } from "redux-observable";
import { AppAction, gameEnded } from "./actions";
import App from "./components/App";
import rootReducer from "./reducers";
import { SideEffect, State } from "./state";
import { GameClientService } from "../game-server/game-client";

import { RweBridge } from "./bridge";
import { MasterClientService } from "../master-server/master-client";
import "./style.css";
import { masterServer } from "../common/util";
import { EpicDependencies } from "./middleware/EpicDependencies";
import { rootEpic } from "./middleware/RootEpic";
import { createEnhancer } from "./sideEffects";
import { execRwe } from "./rwe";
import * as rx from "rxjs";
import * as rxop from "rxjs/operators";

import { init } from "@sentry/electron/renderer";

const development = !!process.env["RWE_LAUNCHER_IS_DEV"];

if (!development) {
  init({
    dsn: "https://205f2f8999194f90ac7f4ce17d36be24@sentry.io/5188334",
  });
}

const masterClentService = new MasterClientService();
masterClentService.connectToServer(`${masterServer()}/master`);

const epicDeps = {
  clientService: new GameClientService(),
  masterClentService,
  bridgeService: new RweBridge(),
};

const epicMiddleware = createEpicMiddleware<
  AppAction,
  AppAction,
  State,
  EpicDependencies
>({
  dependencies: epicDeps,
});

// eslint-disable-next-line prefer-const
let store: Store<any>;

const executeSideEffect = (se: SideEffect) => {
  switch (se.type) {
    case "LAUNCH_RWE": {
      return rx
        .from(execRwe(se.args).finished)
        .pipe(
          rxop.mapTo(undefined),
          rxop.catchError(error => {
            store.dispatch({ type: "LAUNCHER_ERROR", message: String(error) });
            return rx.of(undefined);
          }),
          rxop.mapTo(gameEnded())
        )
        .subscribe(store.dispatch);
    }
  }
};

// configureStore rather than createStore: it wires the devtools extension
// itself, so the __REDUX_DEVTOOLS_EXTENSION_COMPOSE__ dance this used to do
// by hand is gone, and it brings the development-only immutability and
// serializability checks with it.
const preferencesPath = path.join(getRweUserPath(), "launcher.json");
const persistPreferences: Middleware<unknown, State> =
  api => next => action => {
    const result = next(action);
    if (
      [
        "SET_PLAYER_NAME",
        "CHANGE_SINGLE_PLAYER_MODS",
        "HOST_GAME_FORM_CONFIRM",
        "JOIN_SELECTED_GAME_CONFIRM",
      ].includes(action.type)
    ) {
      const state = api.getState();
      try {
        savePreferences(preferencesPath, {
          playerName: state.playerName || "Player",
          activeMods: state.activeMods,
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
  reducer: rootReducer as any, // shhhhh
  middleware: getDefaultMiddleware =>
    getDefaultMiddleware().concat(persistPreferences, epicMiddleware),
  enhancers: defaultEnhancers =>
    defaultEnhancers.concat(createEnhancer(executeSideEffect) as StoreEnhancer),
});

try {
  store.dispatch({
    type: "RECEIVE_LAUNCHER_PREFERENCES",
    ...loadPreferences(preferencesPath),
  });
} catch (error) {
  store.dispatch({
    type: "RECEIVE_LAUNCHER_PREFERENCES",
    ...defaultPreferences,
  });
  store.dispatch({
    type: "LAUNCHER_ERROR",
    message: `Could not read launcher settings: ${error}`,
  });
}
epicMiddleware.run(rootEpic);

// React 18: createRoot, not ReactDOM.render. The old call still exists in 18
// but runs in legacy mode and warns, and concurrent features are off.
const container = document.getElementById("app");
if (!container) {
  throw new Error("index.html has no #app to mount the launcher into");
}

const theme = createTheme({
  palette: {
    mode: "dark",
    primary: { main: "#f0b461" },
    background: { default: "#0e151c", paper: "#18232d" },
  },
  typography: { fontFamily: '"Roboto", sans-serif' },
  shape: { borderRadius: 10 },
});

createRoot(container).render(
  <ThemeProvider theme={theme}>
    <CssBaseline />
    <Provider store={store}>
      <App />
    </Provider>
  </ThemeProvider>
);
