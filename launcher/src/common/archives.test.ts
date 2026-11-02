import * as fs from "fs";
import * as os from "os";
import * as path from "path";
import {
  ArchiveInfo,
  archiveDigest,
  HashCache,
  cachedHashIsValid,
  compareArchives,
  checkArchiveAgreement,
  compareManifest,
  compareModSets,
  fingerprintMod,
  hashFile,
  listArchives,
  listManifest,
  manifestFileName,
  sortArchivesInLoadOrder,
} from "./archives";

function archive(name: string, hash: string, size = 1): ArchiveInfo {
  return { name, size, hash };
}

function makeTempDir(): string {
  return fs.mkdtempSync(path.join(os.tmpdir(), "rwe-archives-test-"));
}

/** Two manifests whose bytes differ, and the SHA-256 of each. */
const manifestA = '{"shortname":"ta"}';
const manifestAHash =
  "cb8dfb954ed97845b07898a1a79942a1c28c8a6a262c4ac2b5c07fb7cffe5b0f";
const manifestB = '{"shortname":"ta","version":"1.0.0"}';
const manifestBHash =
  "306d40416f2ddd626c40c3d2da8084c38b436e66c9f93fabb374793ed180cbe0";

describe("sortArchivesInLoadOrder", () => {
  it("puts the extensions in the engine's precedence order", () => {
    const sorted = sortArchivesInLoadOrder([
      { name: "a.hpi" },
      { name: "b.ccx" },
      { name: "c.gp3" },
      { name: "d.ufo" },
      { name: "e.gpf" },
    ]);
    expect(sorted.map(x => x.name)).toEqual([
      "c.gp3",
      "e.gpf",
      "b.ccx",
      "d.ufo",
      "a.hpi",
    ]);
  });

  it("sorts by name within an extension, without case", () => {
    const sorted = sortArchivesInLoadOrder([
      { name: "totala2.hpi" },
      { name: "Totala1.hpi" },
      { name: "aaa.hpi" },
    ]);
    expect(sorted.map(x => x.name)).toEqual([
      "aaa.hpi",
      "Totala1.hpi",
      "totala2.hpi",
    ]);
  });

  it("does not disturb the input", () => {
    const input = [{ name: "b.hpi" }, { name: "a.gp3" }];
    sortArchivesInLoadOrder(input);
    expect(input.map(x => x.name)).toEqual(["b.hpi", "a.gp3"]);
  });
});

describe("listArchives", () => {
  it("finds the archives and ignores everything else", async () => {
    const dir = makeTempDir();
    fs.writeFileSync(path.join(dir, "totala1.hpi"), "one");
    fs.writeFileSync(path.join(dir, "CCDATA.CCX"), "two");
    fs.writeFileSync(path.join(dir, "rwe_mod.json"), "{}");
    fs.writeFileSync(path.join(dir, "readme.txt"), "hello");
    fs.mkdirSync(path.join(dir, "maps.hpi"));

    const archives = await listArchives(dir);

    // The directory called maps.hpi is not an archive, whatever it is named.
    expect(archives.map(a => a.name)).toEqual(["CCDATA.CCX", "totala1.hpi"]);
    expect(archives[0].size).toBe(3);
  });

  it("matches the extension without case", async () => {
    const dir = makeTempDir();
    fs.writeFileSync(path.join(dir, "MAPS.UFO"), "x");
    const archives = await listArchives(dir);
    expect(archives.map(a => a.name)).toEqual(["MAPS.UFO"]);
  });

  it("has nothing to say about a directory that is not there", async () => {
    const archives = await listArchives(
      path.join(makeTempDir(), "no-such-mod")
    );
    expect(archives).toEqual([]);
  });
});

describe("listManifest", () => {
  it("finds the author manifest when the mod has one", async () => {
    const dir = makeTempDir();
    const file = path.join(dir, manifestFileName);
    fs.writeFileSync(file, manifestA);

    const manifest = await listManifest(dir);

    expect(manifest?.name).toBe(manifestFileName);
    expect(manifest?.path).toBe(file);
    expect(manifest?.size).toBe(Buffer.byteLength(manifestA));
  });

  it("has nothing to say about a mod with no manifest", async () => {
    const dir = makeTempDir();
    fs.writeFileSync(path.join(dir, "a.hpi"), "abc");
    expect(await listManifest(dir)).toBeUndefined();
  });

  it("has nothing to say about a directory that is not there", async () => {
    expect(
      await listManifest(path.join(makeTempDir(), "no-such-mod"))
    ).toBeUndefined();
  });
});

describe("hashFile", () => {
  it("computes the SHA-256 of the contents", async () => {
    const dir = makeTempDir();
    const file = path.join(dir, "a.hpi");
    fs.writeFileSync(file, "abc");

    // The published SHA-256 of "abc".
    expect(await hashFile(file)).toBe(
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    );
  });
});

describe("cachedHashIsValid", () => {
  const file = { name: "a.hpi", path: "/x/a.hpi", size: 10, mtimeMs: 1000 };

  it("is not valid when there is nothing cached", () => {
    expect(cachedHashIsValid(undefined, file)).toBe(false);
  });

  it("is valid when the size and the time both match", () => {
    expect(
      cachedHashIsValid({ size: 10, mtimeMs: 1000, hash: "x" }, file)
    ).toBe(true);
  });

  it("is not valid when the file has changed size", () => {
    expect(
      cachedHashIsValid({ size: 11, mtimeMs: 1000, hash: "x" }, file)
    ).toBe(false);
  });

  it("is not valid when the file has been written since", () => {
    expect(cachedHashIsValid({ size: 10, mtimeMs: 999, hash: "x" }, file)).toBe(
      false
    );
  });
});

describe("fingerprintMod", () => {
  it("hashes every archive in the mod", async () => {
    const dir = makeTempDir();
    fs.writeFileSync(path.join(dir, "a.hpi"), "abc");
    const cache: HashCache = {};

    const fingerprint = await fingerprintMod("ta", dir, cache);

    expect(fingerprint.name).toBe("ta");
    expect(fingerprint.archives).toEqual([
      {
        name: "a.hpi",
        size: 3,
        hash: "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
      },
    ]);
  });

  it("fills the cache, and then reads it rather than the file", async () => {
    const dir = makeTempDir();
    const file = path.join(dir, "a.hpi");
    fs.writeFileSync(file, "abc");
    const cache: HashCache = {};

    await fingerprintMod("ta", dir, cache);
    expect(Object.keys(cache)).toEqual([file]);

    // A hash that could only have come from the cache: the file says "abc".
    cache[file].hash = "cached";
    const second = await fingerprintMod("ta", dir, cache);
    expect(second.archives[0].hash).toBe("cached");
  });

  it("re-hashes a file whose size has changed", async () => {
    const dir = makeTempDir();
    const file = path.join(dir, "a.hpi");
    fs.writeFileSync(file, "abc");
    const cache: HashCache = {};
    await fingerprintMod("ta", dir, cache);

    fs.writeFileSync(file, "abcd");
    const second = await fingerprintMod("ta", dir, cache);
    expect(second.archives[0].hash).toBe(
      "88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589"
    );
  });
});

describe("fingerprintMod, on the author manifest", () => {
  it("covers rwe_mod.json's bytes when the mod has one", async () => {
    const dir = makeTempDir();
    fs.writeFileSync(path.join(dir, "a.hpi"), "abc");
    fs.writeFileSync(path.join(dir, manifestFileName), manifestA);

    const fingerprint = await fingerprintMod("ta", dir, {});

    expect(fingerprint.manifest).toEqual({
      size: Buffer.byteLength(manifestA),
      hash: manifestAHash,
    });
    // The archive half is exactly what it is for a mod without a manifest.
    expect(fingerprint.archives).toEqual([
      {
        name: "a.hpi",
        size: 3,
        hash: "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
      },
    ]);
  });

  it("is the same for the same content in different directories", async () => {
    const one = makeTempDir();
    const two = makeTempDir();
    for (const dir of [one, two]) {
      fs.writeFileSync(path.join(dir, "a.hpi"), "abc");
      fs.writeFileSync(path.join(dir, manifestFileName), manifestA);
    }

    // Nothing in the fingerprint is a path, so where the mod lives cannot
    // change it -- and two players never share a directory anyway.
    expect(await fingerprintMod("ta", one, {})).toEqual(
      await fingerprintMod("ta", two, {})
    );
  });

  it("changes when the manifest's bytes change", async () => {
    const dir = makeTempDir();
    fs.writeFileSync(path.join(dir, "a.hpi"), "abc");
    fs.writeFileSync(path.join(dir, manifestFileName), manifestA);
    const before = await fingerprintMod("ta", dir, {});

    fs.writeFileSync(path.join(dir, manifestFileName), manifestB);
    const after = await fingerprintMod("ta", dir, {});

    expect(before.manifest?.hash).toBe(manifestAHash);
    expect(after.manifest?.hash).toBe(manifestBHash);
    expect(after).not.toEqual(before);
  });

  it("leaves a mod with no manifest exactly as it was", async () => {
    const dir = makeTempDir();
    fs.writeFileSync(path.join(dir, "a.hpi"), "abc");

    const fingerprint = await fingerprintMod("ta", dir, {});

    expect(fingerprint).toEqual({
      name: "ta",
      archives: [
        {
          name: "a.hpi",
          size: 3,
          hash: "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        },
      ],
    });
    expect("manifest" in fingerprint).toBe(false);
  });

  it("caches the manifest hash like an archive's, and re-hashes it when it changes", async () => {
    const dir = makeTempDir();
    const manifestPath = path.join(dir, manifestFileName);
    fs.writeFileSync(manifestPath, manifestA);
    const cache: HashCache = {};

    await fingerprintMod("ta", dir, cache);
    expect(Object.keys(cache)).toEqual([manifestPath]);

    // A hash that could only have come from the cache: the file says manifestA.
    cache[manifestPath].hash = "cached";
    expect((await fingerprintMod("ta", dir, cache)).manifest?.hash).toBe(
      "cached"
    );

    fs.writeFileSync(manifestPath, manifestB);
    expect((await fingerprintMod("ta", dir, cache)).manifest?.hash).toBe(
      manifestBHash
    );
  });
});

describe("compareArchives", () => {
  it("says nothing about two identical sets", () => {
    const set = [archive("a.hpi", "1"), archive("b.ccx", "2")];
    expect(compareArchives(set, [...set])).toEqual([]);
  });

  it("does not care what order they arrived in", () => {
    const mine = [archive("a.hpi", "1"), archive("b.ccx", "2")];
    const theirs = [archive("b.ccx", "2"), archive("a.hpi", "1")];
    expect(compareArchives(mine, theirs)).toEqual([]);
  });

  it("names an archive whose contents differ", () => {
    const mine = [archive("totala1.hpi", "1")];
    const theirs = [archive("totala1.hpi", "2")];
    expect(compareArchives(mine, theirs)).toEqual([
      { archiveName: "totala1.hpi", kind: "different" },
    ]);
  });

  it("names an archive the other player has not got", () => {
    expect(compareArchives([archive("maps.ufo", "1")], [])).toEqual([
      { archiveName: "maps.ufo", kind: "missing" },
    ]);
  });

  it("names an archive the other player has as well", () => {
    expect(compareArchives([], [archive("maps.ufo", "1")])).toEqual([
      { archiveName: "maps.ufo", kind: "extra" },
    ]);
  });

  it("matches names without case, as the engine does", () => {
    const mine = [archive("CCDATA.CCX", "1")];
    const theirs = [archive("ccdata.ccx", "1")];
    expect(compareArchives(mine, theirs)).toEqual([]);
  });

  it("reports the differences in load order", () => {
    const mine = [archive("z.hpi", "1"), archive("a.ufo", "1")];
    const theirs = [archive("z.hpi", "2"), archive("a.ufo", "2")];
    expect(compareArchives(mine, theirs).map(d => d.archiveName)).toEqual([
      "a.ufo",
      "z.hpi",
    ]);
  });

  it("does not mind a size that differs when the hash does not", () => {
    // The hash is what decides; a size is carried for the player to read.
    const mine = [archive("a.hpi", "1", 10)];
    const theirs = [archive("a.hpi", "1", 20)];
    expect(compareArchives(mine, theirs)).toEqual([]);
  });
});

describe("compareModSets", () => {
  const ta = (hash: string) => ({
    name: "ta",
    archives: [archive("totala1.hpi", hash)],
  });

  it("passes two players with the same data", () => {
    expect(compareModSets(["ta"], [ta("1")], [ta("1")])).toEqual({
      differences: [],
      unknownMods: [],
    });
  });

  it("names the mod and the archive that differ", () => {
    expect(compareModSets(["ta"], [ta("1")], [ta("2")])).toEqual({
      differences: [
        {
          modName: "ta",
          differences: [{ archiveName: "totala1.hpi", kind: "different" }],
        },
      ],
      unknownMods: [],
    });
  });

  it("ignores a mod the game is not going to use", () => {
    const mine = [
      ta("1"),
      { name: "other", archives: [archive("x.hpi", "1")] },
    ];
    const theirs = [
      ta("1"),
      { name: "other", archives: [archive("x.hpi", "2")] },
    ];
    expect(compareModSets(["ta"], mine, theirs).differences).toEqual([]);
  });

  it("calls a mod unknown rather than wrong when one side has not hashed it", () => {
    expect(compareModSets(["ta"], [ta("1")], [])).toEqual({
      differences: [],
      unknownMods: ["ta"],
    });
  });

  it("calls a mod unknown when neither side has it", () => {
    expect(compareModSets(["ta"], [], []).unknownMods).toEqual(["ta"]);
  });
});

describe("compareModSets, on the author manifest", () => {
  const ta = (hash: string, manifestHash?: string) => ({
    name: "ta",
    archives: [archive("totala1.hpi", hash)],
    ...(manifestHash === undefined
      ? {}
      : { manifest: { size: 1, hash: manifestHash } }),
  });

  it("passes two players whose archives and manifests are the same", () => {
    expect(compareModSets(["ta"], [ta("1", "m")], [ta("1", "m")])).toEqual({
      differences: [],
      unknownMods: [],
    });
  });

  it("still passes archive-only mods that have no manifest on either side", () => {
    expect(compareModSets(["ta"], [ta("1")], [ta("1")])).toEqual({
      differences: [],
      unknownMods: [],
    });
  });

  it("names the mod when only the manifest differs", () => {
    expect(compareModSets(["ta"], [ta("1", "m")], [ta("1", "n")])).toEqual({
      differences: [
        { modName: "ta", differences: [], manifest: { kind: "different" } },
      ],
      unknownMods: [],
    });
  });

  it("treats a manifest on one side only as a difference", () => {
    // Reference has one, the other player does not.
    expect(compareModSets(["ta"], [ta("1", "m")], [ta("1")])).toEqual({
      differences: [
        { modName: "ta", differences: [], manifest: { kind: "missing" } },
      ],
      unknownMods: [],
    });
    // The other way round, from the same reference.
    expect(compareModSets(["ta"], [ta("1")], [ta("1", "m")])).toEqual({
      differences: [
        { modName: "ta", differences: [], manifest: { kind: "extra" } },
      ],
      unknownMods: [],
    });
  });

  it("reports the archives and the manifest together", () => {
    expect(compareModSets(["ta"], [ta("1", "m")], [ta("2", "n")])).toEqual({
      differences: [
        {
          modName: "ta",
          differences: [{ archiveName: "totala1.hpi", kind: "different" }],
          manifest: { kind: "different" },
        },
      ],
      unknownMods: [],
    });
  });
});

describe("compareManifest", () => {
  it("says nothing when neither side has a manifest", () => {
    expect(compareManifest(undefined, undefined)).toBeUndefined();
  });

  it("says nothing when the hashes agree, whatever the size", () => {
    expect(
      compareManifest({ size: 1, hash: "m" }, { size: 9, hash: "m" })
    ).toBeUndefined();
  });

  it("names which side is missing the manifest", () => {
    expect(compareManifest({ size: 1, hash: "m" }, undefined)).toEqual({
      kind: "missing",
    });
    expect(compareManifest(undefined, { size: 1, hash: "m" })).toEqual({
      kind: "extra",
    });
  });

  it("says the manifest differs when the hashes do", () => {
    expect(
      compareManifest({ size: 1, hash: "m" }, { size: 1, hash: "n" })
    ).toEqual({ kind: "different" });
  });
});

describe("checkArchiveAgreement", () => {
  const mods = (hash: string) => [
    { name: "ta", archives: [archive("totala1.hpi", hash)] },
  ];

  it("passes a room where everyone has the host's data", () => {
    const result = checkArchiveAgreement(
      ["ta"],
      [
        { playerId: 1, mods: mods("1") },
        { playerId: 2, mods: mods("1") },
      ],
      1
    );
    expect(result).toEqual({ uncheckedPlayerIds: [], mismatches: [] });
  });

  it("names the player, the mod and the archive that differ", () => {
    const result = checkArchiveAgreement(
      ["ta"],
      [
        { playerId: 1, mods: mods("1") },
        { playerId: 2, mods: mods("2") },
      ],
      1
    );
    expect(result.mismatches).toEqual([
      {
        playerId: 2,
        mods: [
          {
            modName: "ta",
            differences: [{ archiveName: "totala1.hpi", kind: "different" }],
          },
        ],
      },
    ]);
  });

  it("refuses a room whose archives agree but whose manifests do not", () => {
    const withManifest = (hash: string) => [
      {
        name: "ta",
        archives: [archive("totala1.hpi", "1")],
        manifest: { size: 1, hash },
      },
    ];
    const result = checkArchiveAgreement(
      ["ta"],
      [
        { playerId: 1, mods: withManifest("m") },
        { playerId: 2, mods: withManifest("n") },
      ],
      1
    );
    expect(result).toEqual({
      uncheckedPlayerIds: [],
      mismatches: [
        {
          playerId: 2,
          mods: [
            {
              modName: "ta",
              differences: [],
              manifest: { kind: "different" },
            },
          ],
        },
      ],
    });
  });

  it("compares against the host, not against whoever came first", () => {
    // Players 2 and 3 agree with each other and not with the host, which is
    // still two players in the wrong and not one.
    const result = checkArchiveAgreement(
      ["ta"],
      [
        { playerId: 1, mods: mods("host") },
        { playerId: 2, mods: mods("other") },
        { playerId: 3, mods: mods("other") },
      ],
      1
    );
    expect(result.mismatches.map(m => m.playerId)).toEqual([2, 3]);
  });

  it("leaves a player unchecked until their hashes arrive", () => {
    const result = checkArchiveAgreement(
      ["ta"],
      [
        { playerId: 1, mods: mods("1") },
        { playerId: 2, mods: undefined },
      ],
      1
    );
    expect(result).toEqual({ uncheckedPlayerIds: [2], mismatches: [] });
  });

  it("checks nobody while the host is still hashing", () => {
    const result = checkArchiveAgreement(
      ["ta"],
      [
        { playerId: 1, mods: undefined },
        { playerId: 2, mods: mods("1") },
      ],
      1
    );
    expect(result.uncheckedPlayerIds).toEqual([1, 2]);
    expect(result.mismatches).toEqual([]);
  });

  it("falls back to the first player with data when there is no host yet", () => {
    const result = checkArchiveAgreement(
      ["ta"],
      [
        { playerId: 1, mods: mods("1") },
        { playerId: 2, mods: mods("1") },
      ],
      undefined
    );
    expect(result).toEqual({ uncheckedPlayerIds: [], mismatches: [] });
  });

  it("leaves a player unchecked when they have no fingerprint for an active mod", () => {
    const result = checkArchiveAgreement(
      ["ta", "extra"],
      [
        {
          playerId: 1,
          mods: [...mods("1"), { name: "extra", archives: [] }],
        },
        { playerId: 2, mods: mods("1") },
      ],
      1
    );
    expect(result.uncheckedPlayerIds).toEqual([2]);
    expect(result.mismatches).toEqual([]);
  });

  it("has nothing to say about an empty room", () => {
    expect(checkArchiveAgreement(["ta"], [], undefined)).toEqual({
      uncheckedPlayerIds: [],
      mismatches: [],
    });
  });
});

describe("archiveDigest", () => {
  const ta = (hash: string) => [
    { name: "ta", archives: [archive("totala1.hpi", hash)] },
  ];

  it("is the same for two players with the same data", () => {
    expect(archiveDigest(["ta"], ta("x"))).toBe(archiveDigest(["ta"], ta("x")));
  });

  it("differs when the data differs", () => {
    expect(archiveDigest(["ta"], ta("x"))).not.toBe(
      archiveDigest(["ta"], ta("y"))
    );
  });

  it("ignores mods the game is not going to use", () => {
    const withExtra = [
      ...ta("x"),
      { name: "other", archives: [archive("z.hpi", "q")] },
    ];
    expect(archiveDigest(["ta"], withExtra)).toBe(
      archiveDigest(["ta"], ta("x"))
    );
  });

  it("does not depend on the order the archives arrived in", () => {
    const a = [
      { name: "ta", archives: [archive("a.hpi", "1"), archive("b.hpi", "2")] },
    ];
    const b = [
      { name: "ta", archives: [archive("b.hpi", "2"), archive("a.hpi", "1")] },
    ];
    expect(archiveDigest(["ta"], a)).toBe(archiveDigest(["ta"], b));
  });

  it("is six hex digits", () => {
    expect(archiveDigest(["ta"], ta("x"))).toMatch(/^[0-9a-f]{6}$/);
  });

  it("leaves an archive-only mod's label exactly as it was", () => {
    // The value this label had before manifests joined it: a mod that has no
    // manifest must not start looking like different data to the lobby.
    expect(archiveDigest(["ta"], ta("x"))).toBe("5f35b2");
  });

  it("differs when only the manifest differs", () => {
    const withManifest = (hash: string) => [
      {
        name: "ta",
        archives: [archive("totala1.hpi", "x")],
        manifest: { size: 1, hash },
      },
    ];
    expect(archiveDigest(["ta"], withManifest("m"))).not.toBe(
      archiveDigest(["ta"], withManifest("n"))
    );
  });

  it("differs between a mod with a manifest and one without", () => {
    const withManifest = [
      {
        name: "ta",
        archives: [archive("totala1.hpi", "x")],
        manifest: { size: 1, hash: "m" },
      },
    ];
    expect(archiveDigest(["ta"], withManifest)).not.toBe(
      archiveDigest(["ta"], ta("x"))
    );
  });
});
