import { parseRweEvent, serializeRweArgs } from "./rwe";

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

describe("parseRweEvent", () => {
  it("reads a game-ended event with a winner and its artifacts", () => {
    const line = JSON.stringify({
      event: "game-ended",
      outcome: "decided",
      winner: 1,
      tick: 4281,
      gameTime: 142,
      engineBuild: "Robot War Engine v1.1.0-pre2-Debug",
      replay: "/tmp/game.rwereplay",
      hashLog: "/tmp/h1.log",
      desyncDumps: [],
      log: "/tmp/rwe.log",
    });
    const event = parseRweEvent(line);
    expect(event?.event).toBe("game-ended");
    expect(event?.outcome).toBe("decided");
    expect(event?.winner).toBe(1);
    expect(event?.tick).toBe(4281);
    expect(event?.gameTime).toBe(142);
    expect(event?.engineBuild).toBe("Robot War Engine v1.1.0-pre2-Debug");
    expect(event?.replay).toBe("/tmp/game.rwereplay");
    expect(event?.hashLog).toBe("/tmp/h1.log");
    expect(event?.log).toBe("/tmp/rwe.log");
  });

  it("reads a draw, which carries no winner", () => {
    const event = parseRweEvent(
      JSON.stringify({
        event: "game-ended",
        outcome: "draw",
        winners: [],
        tick: 300,
      })
    );
    expect(event?.outcome).toBe("draw");
    expect(event?.winner).toBeUndefined();
    expect(event?.winners).toEqual([]);
  });

  it("reads an abandoned game's desync tick and null artifacts", () => {
    const event = parseRweEvent(
      JSON.stringify({
        event: "game-ended",
        outcome: "abandoned",
        tick: 4305,
        desyncTick: 4281,
        replay: null,
        hashLog: null,
        log: null,
      })
    );
    expect(event?.outcome).toBe("abandoned");
    expect(event?.desyncTick).toBe(4281);
    expect(event?.replay).toBeNull();
    expect(event?.hashLog).toBeNull();
  });

  it("ignores a line that is not an event", () => {
    expect(parseRweEvent("not json")).toBeUndefined();
    expect(parseRweEvent("{")).toBeUndefined();
    expect(parseRweEvent("[1, 2, 3]")).toBeUndefined();
    expect(parseRweEvent('{"missing":"event"}')).toBeUndefined();
    expect(parseRweEvent('{"event":5}')).toBeUndefined();
  });
});
