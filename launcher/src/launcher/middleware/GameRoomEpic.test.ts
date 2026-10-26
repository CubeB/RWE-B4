import { CurrentGameState } from "../gameClient/state";
import { InstalledModInfo } from "../state";
import { rweArgsFromCurrentGameState } from "./GameRoomEpic";
import { StartGamePayload } from "../../game-server/protocol";

const game = (activeMods: string[]): CurrentGameState => ({
  players: [],
  messages: [],
  mapName: "two-player-map",
  activeMods,
  started: false,
  droppedPlayerIds: [],
});

const startInfo = { addresses: [] } as StartGamePayload;

describe("multiplayer launcher resource roots", () => {
  it("passes builder GUI roots with multiplayer game data paths", () => {
    const mods: InstalledModInfo[] = [
      { name: "ta", path: "C:/data/ta" },
      {
        name: "TAESC",
        path: "C:/mods/TAESC",
        resourceDirectories: { builderGuis: "guiE" },
      },
    ];
    expect(
      rweArgsFromCurrentGameState(mods, game(["TAESC", "ta"]), startInfo)
    ).toMatchObject({
      dataPaths: ["C:/mods/TAESC", "C:/data/ta"],
      resourceDirectories: { builderGuis: "guiE" },
    });
  });

  it("refuses to build multiplayer args when a selected mod inspection failed", () => {
    const mods: InstalledModInfo[] = [
      {
        name: "TAESC",
        path: "C:/mods/TAESC",
        resourceDirectoryError: "setup missing",
      },
    ];
    expect(() =>
      rweArgsFromCurrentGameState(mods, game(["TAESC"]), startInfo)
    ).toThrow("Could not inspect selected mod TAESC: setup missing");
  });
});
