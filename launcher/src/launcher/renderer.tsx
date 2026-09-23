import CssBaseline from "@mui/material/CssBaseline";
import * as React from "react";
import { createRoot } from "react-dom/client";
import { Provider } from "react-redux";
import { configureStore } from "@reduxjs/toolkit";
import { Store, StoreEnhancer } from "redux";
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
          rxop.catchError(() => rx.of(undefined)),
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
store = configureStore({
  reducer: rootReducer as any, // shhhhh
  middleware: getDefaultMiddleware =>
    getDefaultMiddleware().concat(epicMiddleware),
  enhancers: defaultEnhancers =>
    defaultEnhancers.concat(createEnhancer(executeSideEffect) as StoreEnhancer),
});

epicMiddleware.run(rootEpic);

// React 18: createRoot, not ReactDOM.render. The old call still exists in 18
// but runs in legacy mode and warns, and concurrent features are off.
const container = document.getElementById("app");
if (!container) {
  throw new Error("index.html has no #app to mount the launcher into");
}

createRoot(container).render(
  <React.Fragment>
    <CssBaseline />
    <Provider store={store}>
      <App />
    </Provider>
  </React.Fragment>
);
