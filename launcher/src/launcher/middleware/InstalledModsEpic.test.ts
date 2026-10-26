jest.mock("child_process", () => ({ execFile: jest.fn() }));

import { execFile } from "child_process";
import * as fs from "fs";
import * as os from "os";
import * as path from "path";
import { inspectModResourceDirectories } from "./InstalledModsEpic";

const mockExecFile = execFile as unknown as jest.Mock;

describe("installed mod resource inspection", () => {
  beforeEach(() => {
    mockExecFile.mockReset();
    jest.spyOn(fs.promises, "realpath").mockImplementation(async value =>
      String(value)
    );
  });
  afterEach(() => jest.restoreAllMocks());

  it("runs read-only package inspection and attaches the matching builder root", async () => {
    mockExecFile.mockImplementation((_exe, _args, _options, callback) => {
      callback(
        null,
        JSON.stringify({
          complete: true,
          candidates: [
            {
              root: "C:/mods/Escalation",
              resourceDirectories: { builderGuis: "guiE" },
            },
          ],
        })
      );
    });
    const mod = await inspectModResourceDirectories(
      { name: "Escalation", path: "C:/mods/Escalation" },
      "C:/rwe/rwe_setup.exe"
    );
    expect(mockExecFile).toHaveBeenCalledWith(
      "C:/rwe/rwe_setup.exe",
      ["--inspect-package", "C:/mods/Escalation", "--json"],
      expect.objectContaining({ windowsHide: true }),
      expect.any(Function)
    );
    expect(mod.resourceDirectories).toEqual({ builderGuis: "guiE" });
    expect(mod.resourceDirectoryError).toBeUndefined();
  });

  it("inspects a junction through its physical path and preserves the selected alias", async () => {
    jest.restoreAllMocks();
    const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "rwe-selected-root-"));
    try {
      const physical = path.join(fixture, "physical");
      const selected = path.join(fixture, "selected");
      fs.mkdirSync(physical);
      fs.symlinkSync(
        physical,
        selected,
        process.platform === "win32" ? "junction" : "dir"
      );
      const physicalRoot = fs.realpathSync(physical);
      mockExecFile.mockImplementation((_exe, args, _options, callback) => {
        expect(args).toEqual(["--inspect-package", physicalRoot, "--json"]);
        callback(
          null,
          JSON.stringify({
            complete: true,
            candidates: [
              {
                root: physicalRoot,
                kind: "base",
                resourceDirectories: {},
              },
            ],
          })
        );
      });
      const mod = await inspectModResourceDirectories(
        { name: "ta", path: selected },
        "setup.exe"
      );
      expect(mod.path).toBe(selected);
      expect(mod.resourceDirectoryError).toBeUndefined();
      expect(mod.resourceDirectories).toEqual({});
    } finally {
      fs.rmSync(fixture, { recursive: true, force: true });
    }
  });

  it("keeps root-resolution failures diagnostic and skips native inspection", async () => {
    jest
      .spyOn(fs.promises, "realpath")
      .mockRejectedValue(new Error("selected path missing"));
    const mod = await inspectModResourceDirectories(
      { name: "ta", path: "missing" },
      "setup.exe"
    );
    expect(mod.resourceDirectoryError).toContain("selected path missing");
    expect(mockExecFile).not.toHaveBeenCalled();
  });

  it("retains execution failures as per-mod selection diagnostics", async () => {
    mockExecFile.mockImplementation((_exe, _args, _options, callback) => {
      callback(new Error("setup missing"), "");
    });
    const mod = await inspectModResourceDirectories(
      { name: "TAESC", path: "C:/mods/TAESC" },
      "C:/rwe/rwe_setup.exe"
    );
    expect(mod.resourceDirectoryError).toContain("setup missing");
  });

  it("retains malformed inspection output as a per-mod diagnostic", async () => {
    mockExecFile.mockImplementation((_exe, _args, _options, callback) => {
      callback(null, "not json");
    });
    const mod = await inspectModResourceDirectories(
      { name: "TAESC", path: "C:/mods/TAESC" },
      "C:/rwe/rwe_setup.exe"
    );
    expect(mod.resourceDirectoryError).toContain("invalid JSON");
  });
});
