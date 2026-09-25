import { serializeRweArgs } from "./rwe";

describe("serializeRweArgs", () => {
  it("joins every value to its key, so a lobby string cannot become an option", () => {
    // Issue #75: the map name and player names are other people's strings,
    // and the engine reads a separate element beginning with -- as an option.
    const args = serializeRweArgs({
      map: "--log=/home/someone/.bashrc",
      players: [
        {
          state: "filled",
          name: "--load=evil;x;y",
          side: "ARM",
          color: 0,
          controller: { type: "human" },
        },
        { state: "empty" },
      ],
      rejoinFile: "/tmp/bundle.rwereplay",
      rejoinTick: 1200,
    });
    expect(args).toEqual([
      "--map=--log=/home/someone/.bashrc",
      "--rejoin=/tmp/bundle.rwereplay",
      "--rejoin-tick=1200",
      "--player=--load=evil_x_y;Human;ARM;0",
      "--player=empty",
    ]);
    for (const a of args) {
      expect(a.startsWith("--")).toBe(true);
      expect(a.indexOf("=")).toBeGreaterThan(2);
    }
  });

  it("keeps a flag with no value on its own", () => {
    expect(serializeRweArgs({ bridge: true, port: 6670 })).toEqual([
      "--port=6670",
      "--bridge",
    ]);
  });
});
