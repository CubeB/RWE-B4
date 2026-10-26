import fs from "fs";
import os from "os";
import path from "path";
import {
  loadPreferences,
  savePreferences,
  normalizePlayerName,
  normalizePreferences,
} from "./preferences";
import { getInstalledMods, mergeResourceDirectories, resourceDirectoriesForSelectedRoot } from "../common/mods";
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
    });
    savePreferences(filename, {
      playerName: " Oscar ",
      activeMods: ["Escalation", "ta"],
    });
    expect(loadPreferences(filename)).toEqual({
      playerName: "Oscar",
      activeMods: ["Escalation", "ta"],
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
    ).toEqual({ playerName: "Player", activeMods: ["ta"] });
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
      resourceDirectories: { builderGuis: "customGui", futureRoot: "leave-alone" },
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
      candidates: [
        { root: selected, resourceDirectories: { builderGuis: "guiE", guis: "other" } },
        { root: path.join(directory, "nested"), resourceDirectories: { builderGuis: "wrong" } },
      ],
    };
    expect(resourceDirectoriesForSelectedRoot(report, selected)).toEqual({ builderGuis: "guiE", guis: "other" });
    report.candidates.push({ root: selected.toUpperCase(), resourceDirectories: { builderGuis: "ambiguous" } });
    expect(resourceDirectoriesForSelectedRoot(report, selected)).toBeUndefined();
  });

  it("refuses conflicting selected resource roots rather than guessing across mods", () => {
    expect(mergeResourceDirectories([
      { name: "TAESC", resourceDirectories: { builderGuis: "guiE" } },
      { name: "Other mod", resourceDirectories: { builderGuis: "customGui" } },
    ]).conflict).toContain("builderGuis is mapped to both guiE");
  });

  it("passes the profile and selected overlays to the game with base data last", () => {
    let { state } = reducer(undefined, { type: "LAUNCHER_ERROR" });
    state = reducer(state, {
      type: "RECEIVE_LAUNCHER_PREFERENCES",
      playerName: "Oscar",
      activeMods: ["ta"],
    }).state;
    state = reducer(state, {
      type: "RECEIVE_INSTALLED_MODS",
      mods: [
        { name: "ta", path: "/base data" },
        { name: "mod", path: "/my mod", resourceDirectories: { builderGuis: "guiE" } },
      ],
    }).state;
    state = reducer(state, {
      type: "CHANGE_SINGLE_PLAYER_MODS",
      newMods: ["ta", "mod"],
    }).state;
    expect(state.masterClient.masterServerConnectionStatus).toBe("disconnected");
    expect(canLaunchRwe(state)).toBe(true);
    const result = reducer(state, { type: "LAUNCH_RWE" });
    expect(result.sideEffects).toEqual([
      {
        type: "LAUNCH_RWE",
        args: { playerName: "Oscar", dataPaths: ["/my mod", "/base data"], resourceDirectories: { builderGuis: "guiE" } },
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
