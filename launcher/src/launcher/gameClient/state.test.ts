import { ModFingerprint } from "../../common/archives";
import {
  canStartGame,
  CanStartGameError,
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
