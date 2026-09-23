import {
  app,
  BrowserWindow,
  BrowserWindowConstructorOptions,
  Menu,
} from "electron";
import * as path from "path";
import { installExtensions } from "./install-devtools-extensions";

import { init } from "@sentry/electron/main";

const development = !!process.env["RWE_LAUNCHER_IS_DEV"];
console.log(`Running in ${development ? "development" : "production"} mode`);

if (!development) {
  init({
    dsn: "https://205f2f8999194f90ac7f4ce17d36be24@sentry.io/5188334",
  });
}

// Dev content is served remotely (webpack-dev-server) for hot reload,
// so the usual Electron security warnings do not apply here.
if (development) {
  process.env["ELECTRON_DISABLE_SECURITY_WARNINGS"] = "1";
  if (!process.env["RWE_MASTER_SERVER"]) {
    process.env["RWE_MASTER_SERVER"] = "http://localhost:5000";
  }
}

if (!process.env["RWE_HOME"]) {
  // When packaged, getAppPath() will return path/to/launcher/resources/app.asar.
  // The rwe binary will be three levels up from here.
  process.env["RWE_HOME"] = path.resolve(app.getAppPath(), "..", "..", "..");
}

let mainWindow: Electron.BrowserWindow | null;

function createWindow() {
  const windowOptions: BrowserWindowConstructorOptions = {
    height: 600,
    width: 800,
    webPreferences: {
      // Enabling node integration is safe
      // because we only ever run local, trusted code.
      nodeIntegration: true,
      contextIsolation: false,
    },
  };
  // disable web security in development, to permit accessing local files
  // even though the page is served from a remote URL
  if (development) {
    windowOptions.webPreferences!.webSecurity = false;
  }
  mainWindow = new BrowserWindow(windowOptions);

  if (development) {
    mainWindow.loadURL("http://localhost:8080/index.html");
  } else {
    mainWindow.loadFile("index.html");
  }

  // Open the DevTools.
  if (development) {
    mainWindow.webContents.once("dom-ready", () => {
      mainWindow!.webContents.openDevTools();
    });
  }

  mainWindow.on("closed", () => {
    mainWindow = null;
  });
}

app.on("ready", () =>
  installExtensions().then(() => {
    // Hide menu bar in release mode to discourage users
    // from poking at developer tools.
    if (!development) {
      Menu.setApplicationMenu(null);
    }
    createWindow();
  })
);

app.on("window-all-closed", () => {
  // On OS X it is common for applications and their menu bar
  // to stay active until the user quits explicitly with Cmd + Q
  if (process.platform !== "darwin") {
    app.quit();
  }
});

app.on("activate", () => {
  // On OS X it"s common to re-create a window in the app when the
  // dock icon is clicked and there are no other windows open.
  if (mainWindow === null) {
    createWindow();
  }
});
