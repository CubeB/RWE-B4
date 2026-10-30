import CssBaseline from "@mui/material/CssBaseline";
import * as React from "react";
import { createRoot } from "react-dom/client";
import { Provider } from "react-redux";
import { ThemeProvider, createTheme } from "@mui/material/styles";
import App from "./components/App";
import "./style.css";
import { configureStore } from "@reduxjs/toolkit";
import { State } from "./state";
import { AppAction } from "./actions";
import { browserApi } from "./browserStore";

const stateReducer = (state: State | undefined, action: AppAction): State =>
  (action as any).type === "REMOTE_STATE" ? (action as any).state : state!;
let store: ReturnType<typeof configureStore>;
let actionQueue = Promise.resolve();
const showConnection = () => {
  const banner = document.getElementById("connection-status");
  if (banner) banner.hidden = false;
};
const hideConnection = () => {
  const banner = document.getElementById("connection-status");
  if (banner) banner.hidden = true;
};
const renderRemote = (initialState: State) => {
  store = configureStore({
    reducer: stateReducer,
    preloadedState: initialState,
  });
  store.dispatch = (original => (action: any) => {
    if (action.type !== "REMOTE_STATE") {
      actionQueue = actionQueue
        .then(() => browserApi.postAction(action))
        .catch(showConnection);
      return action;
    }
    return original(action);
  })(store.dispatch as any) as typeof store.dispatch;
  const events = new EventSource("events");
  events.onmessage = event => {
    try {
      hideConnection();
      store.dispatch({
        type: "REMOTE_STATE",
        state: JSON.parse(event.data),
      } as any);
    } catch {
      showConnection();
    }
  };
  events.onerror = showConnection;

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
};

void fetch("api/state")
  .then(response => {
    if (!response.ok) throw new Error("Launcher state unavailable");
    return response.json();
  })
  .then(state => {
    hideConnection();
    renderRemote(state);
  })
  .catch(showConnection);
