import path from "path";
import { chooseRweUserPath, rweUserPathCandidates } from "./util";

describe("rweUserPathCandidates", () => {
  it("uses %APPDATA%\\RWE on Windows", () => {
    expect(
      rweUserPathCandidates("win32", { APPDATA: "C:\\Users\\x\\AppData\\Roaming" })
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
      rweUserPathCandidates("linux", { HOME: "/home/someone", XDG_DATA_HOME: "" })[0]
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
      chooseRweUserPath(candidates, (p) => p === "/home/someone/.rwe")
    ).toEqual("/home/someone/.rwe");
  });
  it("prefers the new place once it exists", () => {
    expect(chooseRweUserPath(candidates, () => true)).toEqual(candidates[0]);
  });
});
