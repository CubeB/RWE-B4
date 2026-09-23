import { ModFingerprint } from "../../common/archives";
import {
  canRequestRejoin,
  canStartGame,
  CanStartGameError,
  currentGameWrapperReducer,
  CurrentGameState,
  PlayerInfo,
  PlayerSlot,
} from "./state";

function player(
  id: number,
  archives: ModFingerprint[] | undefined
): PlayerSlot {
  const info: PlayerInfo = {
    id,
    name: `player ${id}`,
    side: "ARM",
    color: id,
    team: undefined,
    ready: true,
    installedMods: ["ta"],
    archives,
  };
  return { state: "filled", player: info };
}

function room(players: PlayerSlot[]): CurrentGameState {
  return {
    localPlayerId: 1,
    adminPlayerId: 1,
    players,
    messages: [],
    mapName: "Coast To Coast",
    activeMods: ["ta"],
    started: false,
    droppedPlayerIds: [],
  };
}

function ta(hash: string): ModFingerprint[] {
  return [{ name: "ta", archives: [{ name: "totala1.hpi", size: 1, hash }] }];
}

function errorsOf(state: CurrentGameState): CanStartGameError[] {
  const result = canStartGame(state);
  return result.result === "err" ? result.errors : [];
}

describe("canStartGame, on the archives everyone is about to play with", () => {
  it("lets a game start when the players have the same data", () => {
    expect(
      canStartGame(room([player(1, ta("x")), player(2, ta("x"))]))
    ).toEqual({ result: "ok" });
  });

  it("refuses to start when a player's data differs, and says which file", () => {
    const errors = errorsOf(room([player(1, ta("x")), player(2, ta("y"))]));
    expect(errors).toContainEqual({
      type: "archives-differ",
      playersWithWrongArchives: [
        {
          playerId: 2,
          mods: [
            {
              modName: "ta",
              differences: [{ archiveName: "totala1.hpi", kind: "different" }],
            },
          ],
        },
      ],
    });
  });

  it("refuses to start while a player's hashes have not arrived", () => {
    // The window this exists to close: joining and starting fast enough that
    // nobody's data was ever compared.
    const errors = errorsOf(room([player(1, ta("x")), player(2, undefined)]));
    expect(errors).toContainEqual({
      type: "archives-unchecked",
      playerIds: [2],
    });
  });

  it("does not complain about archives when everyone agrees", () => {
    const errors = errorsOf(room([player(1, ta("x")), player(2, ta("x"))]));
    expect(errors.map(e => e.type)).not.toContain("archives-differ");
    expect(errors.map(e => e.type)).not.toContain("archives-unchecked");
  });
});

describe("canRequestRejoin, on who is offered their seat back", () => {
  const inGame = (players: PlayerSlot[]): CurrentGameState => ({
    ...room(players),
    started: true,
  });

  it("offers it to a player whose own game dropped them", () => {
    const state = {
      ...inGame([player(1, ta("x")), player(2, ta("x"))]),
      droppedPlayerIds: [1],
    };
    expect(canRequestRejoin(state, false)).toBe(true);
  });

  it("does not offer it while that player is still playing", () => {
    // The game they are playing may be a different one, or this one about to
    // end; either way there is nothing to rejoin while it is running.
    const state = {
      ...inGame([player(1, ta("x")), player(2, ta("x"))]),
      droppedPlayerIds: [1],
    };
    expect(canRequestRejoin(state, true)).toBe(false);
  });

  it("does not offer it to somebody who was never dropped", () => {
    const state = {
      ...inGame([player(1, ta("x")), player(2, ta("x"))]),
      droppedPlayerIds: [2],
    };
    expect(canRequestRejoin(state, false)).toBe(false);
  });

  it("does not offer it in a lobby whose game has not started", () => {
    const state = { ...room([player(1, ta("x"))]), droppedPlayerIds: [1] };
    expect(canRequestRejoin(state, false)).toBe(false);
  });
});

describe("the room, as the rejoin goes through it", () => {
  const started = (): CurrentGameState => ({
    ...room([player(1, ta("x")), player(2, ta("x"))]),
    started: true,
  });

  it("remembers a dropped player once, however many peers report them", () => {
    let state = currentGameWrapperReducer(started(), {
      type: "RECEIVE_PLAYER_DROPPED_FROM_GAME",
      payload: { playerId: 2, tick: 500 },
    })!;
    state = currentGameWrapperReducer(state, {
      type: "RECEIVE_PLAYER_DROPPED_FROM_GAME",
      payload: { playerId: 2, tick: 500 },
    })!;
    expect(state.droppedPlayerIds).toEqual([2]);
  });

  it("stops calling them dropped once their recording is on its way", () => {
    let state = currentGameWrapperReducer(started(), {
      type: "RECEIVE_PLAYER_DROPPED_FROM_GAME",
      payload: { playerId: 1, tick: 500 },
    })!;
    state = currentGameWrapperReducer(state, {
      type: "RECEIVE_REJOIN_BUNDLE",
      payload: { playerId: 1, tick: 620, data: new ArrayBuffer(8) },
    })!;
    expect(state.droppedPlayerIds).toEqual([]);
  });

  it("keeps a refusal meant for this player, and ignores one that is not", () => {
    let state = currentGameWrapperReducer(started(), {
      type: "RECEIVE_REJOIN_REFUSED",
      payload: { playerId: 2, reason: "not for you" },
    })!;
    expect(state.rejoinRefusedReason).toBeUndefined();

    state = currentGameWrapperReducer(state, {
      type: "RECEIVE_REJOIN_REFUSED",
      payload: { playerId: 1, reason: "nobody left to ask" },
    })!;
    expect(state.rejoinRefusedReason).toBe("nobody left to ask");

    // And asking again clears it, so the last answer is the one shown.
    state = currentGameWrapperReducer(state, { type: "SEND_REQUEST_REJOIN" })!;
    expect(state.rejoinRefusedReason).toBeUndefined();
  });
});
