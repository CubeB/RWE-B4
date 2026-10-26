import fs from "fs";
import os from "os";
import path from "path";
import { PassThrough } from "stream";
jest.mock("child_process", () => ({ execFile: jest.fn() }));
import { execFile } from "child_process";
import {
  automaticallyImportTaData,
  automaticallySetUpTaModManual,
  checkExistingTaData,
  chooseRweUserPath,
  rweLibraryPaths,
  rweUserPathCandidates,
} from "./util";

const mockExecFile = execFile as unknown as jest.Mock;

describe("native TA data setup", () => {
  beforeEach(() => mockExecFile.mockReset());

  it("checks usable installed data through the native setup command", async () => {
    mockExecFile.mockImplementation((_exe, _args, _options, callback) =>
      callback(null, "", "")
    );
    await expect(checkExistingTaData("rwe_setup.exe")).resolves.toBe(true);
    expect(mockExecFile).toHaveBeenCalledWith(
      "rwe_setup.exe",
      ["--check-existing"],
      expect.objectContaining({ windowsHide: true }),
      expect.any(Function)
    );
  });

  it("uses native automatic discovery and verifies imported data", async () => {
    mockExecFile.mockImplementation((_exe, _args, _options, callback) =>
      callback(null, "", "")
    );
    await expect(
      automaticallyImportTaData("rwe_setup.exe")
    ).resolves.toBeUndefined();
    expect(mockExecFile.mock.calls.map(call => call[1])).toEqual([
      [],
      ["--check-existing"],
    ]);
  });

  it("returns the native discovery or copy error for manual fallback", async () => {
    mockExecFile.mockImplementation((_exe, _args, _options, callback) =>
      callback(new Error("No installation found"), "", "No installation found")
    );
    await expect(automaticallyImportTaData("rwe_setup.exe")).resolves.toContain(
      "No installation found"
    );
  });
});

describe("rweUserPathCandidates", () => {
  it("uses %APPDATA%\\RWE on Windows", () => {
    expect(
      rweUserPathCandidates("win32", {
        APPDATA: "C:\\Users\\x\\AppData\\Roaming",
      })
    ).toEqual([path.join("C:\\Users\\x\\AppData\\Roaming", "RWE")]);
  });
  it("throws without APPDATA on Windows", () => {
    expect(() => rweUserPathCandidates("win32", {})).toThrow();
  });
  it("defaults to ~/.local/share/rwe with ~/.rwe as the fallback", () => {
    expect(rweUserPathCandidates("linux", { HOME: "/home/someone" })).toEqual([
      "/home/someone/.local/share/rwe",
      "/home/someone/.rwe",
    ]);
  });
  it("honours an absolute XDG_DATA_HOME and ignores an invalid one", () => {
    expect(
      rweUserPathCandidates("linux", {
        HOME: "/home/someone",
        XDG_DATA_HOME: "/mnt/data",
      })[0]
    ).toEqual("/mnt/data/rwe");
    expect(
      rweUserPathCandidates("linux", {
        HOME: "/home/someone",
        XDG_DATA_HOME: "share",
      })[0]
    ).toEqual("/home/someone/.local/share/rwe");
    expect(
      rweUserPathCandidates("linux", {
        HOME: "/home/someone",
        XDG_DATA_HOME: "",
      })[0]
    ).toEqual("/home/someone/.local/share/rwe");
  });
  it("throws without a home directory", () => {
    expect(() => rweUserPathCandidates("linux", {})).toThrow();
  });
});

describe("chooseRweUserPath", () => {
  const candidates = ["/home/someone/.local/share/rwe", "/home/someone/.rwe"];
  it("picks the new place on a fresh machine", () => {
    expect(chooseRweUserPath(candidates, () => false)).toEqual(candidates[0]);
  });
  it("keeps an existing ~/.rwe until the new place exists", () => {
    expect(
      chooseRweUserPath(candidates, p => p === "/home/someone/.rwe")
    ).toEqual("/home/someone/.rwe");
  });
  it("prefers the new place once it exists", () => {
    expect(chooseRweUserPath(candidates, () => true)).toEqual(candidates[0]);
  });
});

describe("rweLibraryPaths", () => {
  it("shares the native layout beneath the selected per-user RWE directory", () => {
    const userData = path.join("user", "rwe");
    expect(rweLibraryPaths(userData)).toEqual({
      root: path.join(userData, "Library"),
      registry: path.join(userData, "Library", "registry.json"),
      packages: path.join(userData, "Library", "packages"),
      mods: path.join(userData, "mods"),
      mapPacks: path.join(userData, "MapPacks"),
      staging: path.join(userData, "Library", "staging"),
    });
  });
});

describe("automaticallySetUpTaModManual", () => {
  let directory: string;
  beforeEach(() => {
    directory = fs.mkdtempSync(path.join(os.tmpdir(), "rwe-launcher-"));
  });
  afterEach(() => {
    fs.rmSync(directory, { recursive: true, force: true });
  });

  it("resolves false for an empty search list instead of waiting forever", async () => {
    await expect(
      automaticallySetUpTaModManual(path.join(directory, "mods", "ta"), [])
    ).resolves.toBe(false);
  });

  it("resolves false when no candidate directory holds the TA archives", async () => {
    fs.writeFileSync(path.join(directory, "readme.txt"), "not TA");
    await expect(
      automaticallySetUpTaModManual(path.join(directory, "mods", "ta"), [
        directory,
      ])
    ).resolves.toBe(false);
  });

  it("rejects when a candidate file cannot be read instead of hanging", async () => {
    fs.writeFileSync(path.join(directory, "totala1.hpi"), "base");
    const readStream = new PassThrough();
    const createReadStream = jest
      .spyOn(fs, "createReadStream")
      .mockImplementation((() => {
        setImmediate(() => readStream.emit("error", new Error("EBUSY")));
        return readStream;
      }) as unknown as typeof fs.createReadStream);

    try {
      await expect(
        automaticallySetUpTaModManual(path.join(directory, "mods", "ta"), [
          directory,
        ])
      ).rejects.toThrow("EBUSY");
    } finally {
      createReadStream.mockRestore();
    }
  });
});
