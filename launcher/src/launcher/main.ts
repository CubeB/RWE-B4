import * as path from "path";
import * as fs from "fs";
import { execFile, spawn } from "child_process";
import { createBackendStore } from "./backendStore";
import { startLocalServer } from "./localServer";
import { getRweLibraryPaths, getRweModsPath } from "./util";
import { listPackageRegistry } from "./packageLibrary";

// DOWNLOAD-001 / ADR-0007: Node owns launcher services and opens its local UI
// in the installed browser; this is distribution-only RWE-original work.
if (!process.env.RWE_HOME) process.env.RWE_HOME = path.resolve(__dirname, "..");
const home = process.env.RWE_HOME;
const setupPath = path.join(
  home,
  `rwe_setup${process.platform === "win32" ? ".exe" : ""}`
);

function chooseDirectory(): Promise<string | null> {
  if (process.platform === "win32") {
    const script =
      "[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding; Add-Type -AssemblyName System.Windows.Forms; $d = New-Object System.Windows.Forms.FolderBrowserDialog; if ($d.ShowDialog() -eq 'OK') { [Console]::Write($d.SelectedPath) }";
    return new Promise((resolve, reject) =>
      execFile(
        "powershell.exe",
        ["-NoProfile", "-STA", "-Command", script],
        { windowsHide: true },
        (error, stdout) =>
          error ? reject(error) : resolve(stdout.trim() || null)
      )
    );
  }
  return new Promise(resolve =>
    execFile(
      "zenity",
      ["--file-selection", "--directory"],
      { windowsHide: true },
      (error, stdout) => resolve(error ? null : stdout.trim() || null)
    )
  );
}

async function importData() {
  const selected = await chooseDirectory();
  if (!selected) return false;
  await new Promise<void>((resolve, reject) => {
    const child = spawn(setupPath, ["--from", selected], {
      cwd: home,
      windowsHide: true,
    });
    let output = "";
    child.stdout?.on("data", chunk => {
      output = (output + chunk).slice(-8000);
    });
    child.stderr?.on("data", chunk => {
      output = (output + chunk).slice(-8000);
    });
    child.on("error", reject);
    child.on("close", code =>
      code === 0
        ? resolve()
        : reject(new Error(output || `Import failed (${code})`))
    );
  });
  return true;
}

async function checkUpdates() {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 15_000);
  try {
    const response = await fetch(
      "https://api.github.com/repos/CubeB/RWE-B4/releases?per_page=30",
      {
        signal: controller.signal,
        headers: {
          Accept: "application/vnd.github+json",
          "User-Agent": "rwe-launcher",
        },
      }
    );
    if (!response.ok)
      throw new Error(`the release feed answered ${response.status}`);
    const releases = (await response.json()) as any[];
    return releases
      .filter(release => !release.draft)
      .map(release => ({
        tag: release.tag_name || "",
        name: release.name || release.tag_name || "",
        url: release.html_url || "",
        prerelease: release.prerelease === true,
        publishedAt: release.published_at || "",
        assets: (release.assets || [])
          .filter((asset: any) => asset.name && asset.browser_download_url)
          .map((asset: any) => ({
            name: asset.name,
            url: asset.browser_download_url,
          })),
      }));
  } finally {
    clearTimeout(timeout);
  }
}

const { store, close: closeStore } = createBackendStore();
let server: Awaited<ReturnType<typeof startLocalServer>>;
const operations = {
  "check-updates": checkUpdates,
  "list-packages": async () =>
    listPackageRegistry(setupPath, home, getRweLibraryPaths().root),
  platform: async () => process.platform,
  "choose-directory": chooseDirectory,
  "import-data": importData,
  "open-mods": async () => {
    const directory = getRweModsPath();
    fs.mkdirSync(directory, { recursive: true });
    const command = process.platform === "win32" ? "explorer.exe" : "xdg-open";
    if (process.platform === "win32") {
      await new Promise<void>((resolve, reject) => {
        const opener = spawn(command, [directory], { windowsHide: true });
        opener.once("error", reject);
        opener.once("spawn", () => resolve());
      });
    } else {
      await new Promise<void>((resolve, reject) =>
        execFile(command, [directory], { windowsHide: true }, error =>
          error ? reject(error) : resolve()
        )
      );
    }
  },
  "open-external": async (args: any) => {
    const url = new URL(args?.url);
    if (
      url.protocol !== "https:" ||
      url.hostname !== "github.com" ||
      url.username ||
      url.password
    )
      throw new Error("Only GitHub HTTPS links can be opened");
    const command = process.platform === "win32" ? "rundll32.exe" : "xdg-open";
    const commandArgs =
      process.platform === "win32"
        ? ["url.dll,FileProtocolHandler", url.href]
        : [url.href];
    await new Promise<void>((resolve, reject) =>
      execFile(command, commandArgs, { windowsHide: true }, error =>
        error ? reject(error) : resolve()
      )
    );
  },
};

void startLocalServer({
  store,
  staticDir: __dirname,
  operations,
  onIdle: () => {
    void shutdown();
  },
  idleMs: 60_000,
})
  .then(result => {
    server = result;
    console.log(result.url);
    if (process.env.RWE_LAUNCHER_NO_OPEN === "1") return;
    const command = process.platform === "win32" ? "rundll32.exe" : "xdg-open";
    const args =
      process.platform === "win32"
        ? ["url.dll,FileProtocolHandler", result.url]
        : [result.url];
    execFile(command, args, { windowsHide: true }, error => {
      if (error) console.error(`Could not open browser: ${error.message}`);
    });
  })
  .catch(error => {
    console.error(
      `Could not start launcher server: ${error instanceof Error ? error.message : String(error)}`
    );
    closeStore();
    process.exitCode = 1;
  });

let shuttingDown = false;
async function shutdown() {
  if (shuttingDown) return;
  shuttingDown = true;
  if (server) await server.close();
  closeStore();
  process.exit(0);
}
process.on("SIGINT", () => {
  void shutdown();
});
process.on("SIGTERM", () => {
  void shutdown();
});
