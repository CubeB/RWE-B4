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
  compareModSets,
  fingerprintMod,
  hashFile,
  listArchives,
  sortArchivesInLoadOrder,
} from "./archives";

function archive(name: string, hash: string, size = 1): ArchiveInfo {
  return { name, size, hash };
}

function makeTempDir(): string {
  return fs.mkdtempSync(path.join(os.tmpdir(), "rwe-archives-test-"));
}

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
        hash:
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
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
});
