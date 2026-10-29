import fs from "fs";
import os from "os";
import path from "path";
import {
  loadPreferences,
  savePreferences,
  normalizePlayerName,
  normalizePreferences,
} from "./preferences";
import {
  getInstalledMods,
  hasUniqueInspectionRoot,
  resourceDirectoriesForMods,
  resourceDirectoriesForSelectedRoot,
} from "../common/mods";
import { mergeConfig } from "./rweConfig";
import reducer from "./reducers";
import { canLaunchRwe } from "./state";

describe("launcher profile and mod selection", () => {
  let directory: string;
  beforeEach(() => {
    directory = fs.mkdtempSync(path.join(os.tmpdir(), "rwe-launcher-"));
  });
  afterEach(() => {
    fs.rmSync(directory, { recursive: true, force: true });
  });

  it("persists names and load order without touching engine settings", () => {
    const config = path.join(directory, "rwe.cfg");
    fs.writeFileSync(config, "camera-zoom = 125\n");
    const filename = path.join(directory, "launcher.json");
    expect(loadPreferences(filename)).toEqual({
      playerName: "Player",
      activeMods: ["ta"],
      communityPatch: false,
    });
    savePreferences(filename, {
      playerName: " Oscar ",
      activeMods: ["Escalation", "ta"],
      communityPatch: true,
    });
    expect(loadPreferences(filename)).toEqual({
      playerName: "Oscar",
      activeMods: ["Escalation", "ta"],
      communityPatch: true,
    });
    expect(fs.readFileSync(config, "utf8")).toBe("camera-zoom = 125\n");
    expect(fs.existsSync(filename + ".tmp")).toBe(false);
  });

  it("bounds Unicode names to the LAN field without splitting characters", () => {
    expect(normalizePlayerName("  Oscar;\nSmith  ")).toBe("OscarSmith");
    expect(normalizePlayerName("🤖".repeat(8))).toBe("🤖".repeat(5));
    expect(
      normalizePreferences({
        playerName: "\n",
        activeMods: ["ta", 1, "ta", ""],
      })
    ).toEqual({ playerName: "Player", activeMods: ["ta"], communityPatch: false });
  });

  it("discovers imported base data and ordinary mod folders with or without a manifest", async () => {
    const mods = path.join(directory, "mods");
    const data = path.join(directory, "Data");
    fs.mkdirSync(data);
    fs.writeFileSync(path.join(data, "totala1.hpi"), "base");
    for (const name of ["Escalation", "Broken manifest", "Empty"]) {
      fs.mkdirSync(path.join(mods, name), { recursive: true });
    }
    fs.writeFileSync(path.join(mods, "Escalation", "units.UFO"), "mod");
    fs.writeFileSync(
      path.join(mods, "Broken manifest", "rwe_mod.json"),
      "{invalid"
    );
    fs.writeFileSync(path.join(mods, "Broken manifest", "units.gp3"), "mod");
    const found = await getInstalledMods(mods, data);
    expect(found.map(mod => mod.name)).toEqual([
      "ta",
      "Broken manifest",
      "Escalation",
    ]);
    expect(found[0].path).toBe(data);
  });

  it("preserves explicit resource roots in mod metadata and leaves unknown fields on disk", async () => {
    const mods = path.join(directory, "mods");
    fs.mkdirSync(path.join(mods, "Escalation"), { recursive: true });
    fs.writeFileSync(path.join(mods, "Escalation", "TAESC.gp3"), "archive");
    const metadata = path.join(mods, "Escalation", "rwe_mod.json");
    const original = JSON.stringify({
      shortname: "Escalation",
      name: "TA Escalation",
      author: "Example",
      resourceDirectories: {
        builderGuis: "customGui",
        futureRoot: "leave-alone",
      },
      futureMetadata: { enabled: true },
    });
    fs.writeFileSync(metadata, original);
    const found = await getInstalledMods(mods);
    expect(found[0].resourceDirectories).toEqual({ builderGuis: "customGui" });
    expect(fs.readFileSync(metadata, "utf8")).toBe(original);
  });

  it("uses only a unique inspection candidate matching the selected data root", () => {
    const selected = path.join(directory, "TAESC");
    const report = {
      complete: true,
      candidates: [
        {
          root: selected,
          resourceDirectories: { builderGuis: "guiE", guis: "other" },
        },
        {
          root: path.join(directory, "nested"),
          resourceDirectories: { builderGuis: "wrong" },
        },
      ],
    };
    expect(resourceDirectoriesForSelectedRoot(report, selected)).toEqual({
      builderGuis: "guiE",
      guis: "other",
    });
    expect(
      hasUniqueInspectionRoot(
        { complete: true, candidates: [{ root: selected.toUpperCase() }] },
        selected
      )
    ).toBe(process.platform === "win32");
    expect(
      hasUniqueInspectionRoot(
        { complete: false, candidates: [{ root: selected }] },
        selected
      )
    ).toBe(false);
    report.candidates.push({
      root: selected,
      resourceDirectories: { builderGuis: "ambiguous" },
    });
    expect(
      resourceDirectoriesForSelectedRoot(report, selected)
    ).toBeUndefined();
  });

  it("refuses conflicting selected resource roots rather than guessing across mods", () => {
    expect(
      resourceDirectoriesForMods([
        { name: "TAESC", resourceDirectories: { builderGuis: "guiE" } },
        {
          name: "Other mod",
          resourceDirectories: { builderGuis: "customGui" },
        },
      ])
    ).toMatchObject({
      error: expect.stringContaining("builderGuis is mapped to both guiE"),
    });
  });

  it("reports inspection failures only for selected mod roots", () => {
    const failed = {
      name: "broken",
      resourceDirectoryError: "could not run rwe_setup",
    };
    expect(resourceDirectoriesForMods([failed]).error).toContain(
      "selected mod broken"
    );
    expect(
      resourceDirectoriesForMods([
        { name: "selected", resourceDirectories: { builderGuis: "guiE" } },
      ])
    ).toEqual({ resourceDirectories: { builderGuis: "guiE" } });
  });

  it("passes the profile and selected overlays to the game with base data last", () => {
    let { state } = reducer(undefined, { type: "LAUNCHER_ERROR" });
    state = reducer(state, {
      type: "RECEIVE_LAUNCHER_PREFERENCES",
      playerName: "Oscar",
      activeMods: ["ta"],
      communityPatch: true,
    }).state;
    state = reducer(state, {
      type: "RECEIVE_INSTALLED_MODS",
      mods: [
        { name: "ta", path: "/base data" },
        {
          name: "mod",
          path: "/my mod",
          resourceDirectories: { builderGuis: "guiE" },
        },
      ],
    }).state;
    state = reducer(state, {
      type: "CHANGE_SINGLE_PLAYER_MODS",
      newMods: ["ta", "mod"],
    }).state;
    expect(state.masterClient.masterServerConnectionStatus).toBe(
      "disconnected"
    );
    expect(canLaunchRwe(state)).toBe(true);
    const result = reducer(state, { type: "LAUNCH_RWE" });
    expect(result.sideEffects).toEqual([
      {
        type: "LAUNCH_RWE",
        args: {
          playerName: "Oscar",
          dataPaths: ["/my mod", "/base data"],
          resourceDirectories: { builderGuis: "guiE" },
          communityPatch: true,
        },
      },
    ]);
  });

  it("preserves unrelated native game settings when changing video options", () => {
    const merged = mergeConfig(
      "# my settings\ncamera-zoom = 125\nwidth = 800\nwindow-mode = fullscreen\n",
      { width: 1280, fullscreen: false }
    );
    expect(merged).toContain("camera-zoom = 125");
    expect(merged).toContain("# my settings");
    expect(merged).toContain("width = 1280");
    expect(merged).toContain("window-mode = windowed");
    expect(merged).not.toContain("width = 800");
  });
});
