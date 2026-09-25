import * as valid from "./validation";

// Issue #75: every payload a lobby client sends is untrusted, and each of
// these was once enough to crash the server or to reach another player's
// engine command line unchecked.
describe("lobby payload validation", () => {
  it("accepts a player name and cleans it", () => {
    expect(valid.playerName("  Hector  ")).toBe("Hector");
    expect(valid.playerName("a;b;c")).toBe("a_b_c");
    expect(valid.playerName("tab\there")).toBe("tabhere");
  });

  it("refuses names that are not short strings", () => {
    expect(valid.playerName(undefined)).toBeUndefined();
    expect(valid.playerName(null)).toBeUndefined();
    expect(valid.playerName(42)).toBeUndefined();
    expect(valid.playerName("")).toBeUndefined();
    expect(valid.playerName("\u0000\u0001")).toBeUndefined();
    expect(
      valid.playerName("x".repeat(valid.MaxNameLength + 1))
    ).toBeUndefined();
  });

  it("holds numbers to integers in range", () => {
    expect(valid.intInRange(3, 1, 10)).toBe(3);
    expect(valid.intInRange(1e9, 1, 10)).toBeUndefined();
    expect(valid.intInRange(0, 1, 10)).toBeUndefined();
    expect(valid.intInRange(2.5, 1, 10)).toBeUndefined();
    expect(valid.intInRange("3", 1, 10)).toBeUndefined();
    expect(valid.intInRange(NaN, 1, 10)).toBeUndefined();
    expect(valid.slotId(9)).toBe(9);
    expect(valid.slotId(99)).toBeUndefined();
    expect(valid.slotId(-1)).toBeUndefined();
    expect(valid.tick(0xffffffff)).toBe(0xffffffff);
    expect(valid.tick(-1)).toBeUndefined();
  });

  it("knows the two sides and the ten colours", () => {
    expect(valid.side("ARM")).toBe("ARM");
    expect(valid.side("CORE")).toBe("CORE");
    expect(valid.side("ARM;Human")).toBeUndefined();
    expect(valid.color(9)).toBe(9);
    expect(valid.color(10)).toBeUndefined();
  });

  it("takes a dotted IPv4 address and nothing else", () => {
    expect(valid.ipv4Address("203.0.113.7")).toBe("203.0.113.7");
    expect(valid.ipv4Address("256.0.0.1")).toBeUndefined();
    expect(valid.ipv4Address("1.2.3.4;Human")).toBeUndefined();
    expect(valid.ipv4Address("::1")).toBeUndefined();
  });

  it("checks archive fingerprints all the way down", () => {
    const hash = "a".repeat(64);
    const good = [
      { name: "mod", archives: [{ name: "x.hpi", size: 10, hash }] },
    ];
    expect(valid.modFingerprints(good)).toEqual(good);
    expect(valid.modFingerprints("mods")).toBeUndefined();
    expect(
      valid.modFingerprints([{ name: "mod", archives: "x" }])
    ).toBeUndefined();
    expect(
      valid.modFingerprints([
        { name: "mod", archives: [{ name: "x.hpi", size: -1, hash }] },
      ])
    ).toBeUndefined();
    expect(
      valid.modFingerprints([
        { name: "mod", archives: [{ name: "x.hpi", size: 1, hash: "zz" }] },
      ])
    ).toBeUndefined();
  });

  it("only reads fields off a real object", () => {
    expect(valid.payload(null)).toBeUndefined();
    expect(valid.payload([1])).toBeUndefined();
    expect(valid.payload("x")).toBeUndefined();
    expect(valid.payload({ a: 1 })).toEqual({ a: 1 });
  });
});
