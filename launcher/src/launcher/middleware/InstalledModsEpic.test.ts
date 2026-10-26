jest.mock("child_process", () => ({ execFile: jest.fn() }));

import { execFile } from "child_process";
import { inspectModResourceDirectories } from "./InstalledModsEpic";

const mockExecFile = execFile as unknown as jest.Mock;

describe("installed mod resource inspection", () => {
  beforeEach(() => mockExecFile.mockReset());

  it("runs read-only package inspection and attaches the matching builder root", async () => {
    mockExecFile.mockImplementation((_exe, _args, _options, callback) => {
      callback(
        null,
        JSON.stringify({
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
