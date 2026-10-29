import {
  app,
  BrowserWindow,
  BrowserWindowConstructorOptions,
  Menu,
  dialog,
  ipcMain,
  net,
} from "electron";
import { spawn } from "child_process";
import * as path from "path";
import { installExtensions } from "./install-devtools-extensions";
import { getRweLibraryPaths } from "./util";
import { listPackageRegistry } from "./packageLibrary";

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

// The launcher resolves the same per-user package root as rwe_setup and lets
// the native registry reader provide the authoritative access diagnostic.
ipcMain.handle("rwe:list-packages", async () => {
  const home = process.env["RWE_HOME"]!;
  const setup = path.join(
    home,
    "rwe_setup" + (process.platform === "win32" ? ".exe" : "")
  );
  const library = getRweLibraryPaths();
  return listPackageRegistry(setup, home, library.root);
});

ipcMain.handle("rwe:import-data", async () => {
  const selected = await dialog.showOpenDialog({
    title: "Select your Total Annihilation installation",
    properties: ["openDirectory"],
  });
  if (selected.canceled || !selected.filePaths[0]) {
    return false;
  }
  const home = process.env["RWE_HOME"]!;
  await new Promise<void>((resolve, reject) => {
    const importer = spawn(
      path.join(
        home,
        "rwe_setup" + (process.platform === "win32" ? ".exe" : "")
      ),
      ["--from", selected.filePaths[0]],
      { cwd: home, windowsHide: true }
    );
    let output = "";
    importer.stdout.on("data", chunk => {
      output = (output + chunk).slice(-8000);
    });
    importer.stderr.on("data", chunk => {
      output = (output + chunk).slice(-8000);
    });
    importer.on("error", reject);
    importer.on("close", code =>
      code === 0
        ? resolve()
        : reject(new Error(output || `Import failed (${code})`))
    );
  });
  return true;
});

/**
 * Release feed for the updater: GitHub's published releases for this
 * repository, reduced to what the launcher shows. Drafts are dropped here;
 * prereleases are kept because this project's releases are prereleases.
 *
 * Fetched in the main process so the renderer needs no network permission and
 * the proxy settings Electron already knows about still apply.
 */
ipcMain.handle("rwe:check-updates", async () => {
  const response = await net.fetch(
    "https://api.github.com/repos/CubeB/RWE-B4/releases?per_page=30",
    {
      headers: {
        Accept: "application/vnd.github+json",
        "User-Agent": "rwe-launcher",
      },
    }
  );
  if (!response.ok) {
    throw new Error(`the release feed answered ${response.status}`);
  }
  const releases = (await response.json()) as Array<{
    tag_name?: string;
    name?: string;
    html_url?: string;
    prerelease?: boolean;
    published_at?: string;
    draft?: boolean;
    assets?: Array<{ name?: string; browser_download_url?: string }>;
  }>;
  return releases
    .filter(release => !release.draft)
    .map(release => ({
      tag: release.tag_name || "",
      name: release.name || release.tag_name || "",
      url: release.html_url || "",
      prerelease: release.prerelease === true,
      publishedAt: release.published_at || "",
      assets: (release.assets || [])
        .filter(asset => asset.name && asset.browser_download_url)
        .map(asset => ({
          name: asset.name!,
          url: asset.browser_download_url!,
        })),
    }));
});

function createWindow() {
  const windowOptions: BrowserWindowConstructorOptions = {
    height: 760,
    width: 1040,
    minWidth: 800,
    minHeight: 640,
    // INSTALL-UX-002: shared RWE branding, including the live window/taskbar.
    icon: path.join(__dirname, "b4-icon.png"),
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
    mainWindow.loadFile(path.join(__dirname, "index.html"));
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
