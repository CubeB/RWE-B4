import { EventEmitter } from "events";
import { PassThrough } from "stream";
import { ChildProcess, spawn, SpawnOptions } from "child_process";
import { listPackageRegistry } from "./packageLibrary";

function fakeSpawner(stdout: string, stderr: string, exitCode: number) {
  return ((executable: string, args: string[], options: SpawnOptions) => {
    expect(executable).toEqual("rwe_setup");
    expect(args).toEqual(["--list-packages", "--library-root", "library"]);
    expect(options).toMatchObject({ cwd: "rwe-home", windowsHide: true });
    const child = new EventEmitter() as ChildProcess;
    const output = new PassThrough();
    const errors = new PassThrough();
    Object.assign(child, { stdout: output, stderr: errors });
    process.nextTick(() => {
      output.end(stdout);
      errors.end(stderr);
      child.emit("close", exitCode);
    });
    return child;
  }) as typeof spawn;
}

describe("listPackageRegistry", () => {
  it("parses registries larger than 8 KB without mixing stderr into stdout", async () => {
    const registry = {
      schemaVersion: 1,
      packages: Array.from({ length: 120 }, (_, index) => ({
        id: `p-${String(index).padStart(32, "0")}`,
        name: `Package ${index} ${"x".repeat(100)}`,
      })),
    };
    const json = JSON.stringify(registry);
    expect(Buffer.byteLength(json)).toBeGreaterThan(8000);
    await expect(
      listPackageRegistry(
        "rwe_setup",
        "rwe-home",
        "library",
        fakeSpawner(json, "diagnostic noise", 0)
      )
    ).resolves.toEqual(registry);
  });

  it("returns native inaccessible-root diagnostics on a failed listing", async () => {
    const response = JSON.stringify({
      error: { message: "Cannot access package library root" },
    });
    await expect(
      listPackageRegistry(
        "rwe_setup",
        "rwe-home",
        "library",
        fakeSpawner(response, "stderr", 1)
      )
    ).rejects.toThrow("Cannot access package library root");
  });
});
