import { ofType, StateObservable } from "redux-observable";
import * as fs from "fs";
import * as os from "os";
import * as path from "path";
import * as rx from "rxjs";
import * as rxop from "rxjs/operators";
import {
  AppAction,
  closeSelectMapDialog,
  gameEnded,
  LeaveGameAction,
} from "../actions";
import { getRoom, State, InstalledModInfo } from "../state";
import { FilledPlayerSlot, CurrentGameState } from "../gameClient/state";
import * as protocol from "../../game-server/protocol";

import { getIpv4Address } from "../../common/ip-lookup";
import {
  execRwe,
  RunningRwe,
  RweArgs,
  RweArgsEmptyPlayerSlot,
  RweArgsFilledPlayerSlot,
  RweArgsPlayerController,
  RweArgsPlayerInfo,
  RweArgsPlayerSlot,
} from "../rwe";
import { assertNever, masterServer, choose } from "../../common/util";
import { EpicDependencies } from "./EpicDependencies";
import { ReceiveCreateGameResponseAction } from "../masterClient/actions";

function rweArgsFromCurrentGameState(
  installedMods: InstalledModInfo[],
  game: CurrentGameState,
  startInfo: protocol.StartGamePayload
): RweArgs {
  const playersArgs: RweArgsPlayerSlot[] = game.players.map((x, i) => {
    switch (x.state) {
      case "empty":
      case "closed": {
        const s: RweArgsEmptyPlayerSlot = { state: "empty" };
        return s;
      }
      case "filled": {
        const controller: RweArgsPlayerController =
          x.player.id === game.localPlayerId
            ? { type: "human" }
            : {
                type: "remote",
                host: startInfo.addresses.find(
                  ([id, _]) => id === x.player.id
                )![1],
                port: 6670 + i,
              };
        const a: RweArgsPlayerInfo = {
          name: x.player.name,
          side: x.player.side,
          color: x.player.color,
          controller,
        };
        const s: RweArgsFilledPlayerSlot = { ...a, state: "filled" };
        return s;
      }
      default:
        return assertNever(x);
    }
  });
  const portOffset = game.players.findIndex(
    x => x.state === "filled" && x.player.id === game.localPlayerId
  );
  if (game.mapName === undefined) {
    throw new Error("map is not set");
  }
  return {
    dataPaths: choose(game.activeMods, name =>
      installedMods.find(x => x.name === name)
    ).map(x => x.path),
    map: game.mapName,
    port: 6670 + portOffset,
    players: playersArgs,
    // Every peer of a network game records, and not for the sake of watching
    // it afterwards: the recording is the only thing that keeps the commands,
    // and handing them over is the whole of letting a dropped player back in.
    // A bare name lands in the Replays folder; the time is in it so that one
    // evening's games do not overwrite each other.
    recordReplay: `multiplayer-${new Date()
      .toISOString()
      .replace(/[:.]/g, "-")}`,
  };
}

/**
 * The engine numbers players by slot and the lobby numbers them by join order,
 * and the rejoin has to cross between the two: the engine says "slot 1 has
 * dropped" and the lobby has to know whose seat that is.
 */
function lobbyIdOfSlot(
  game: CurrentGameState,
  slot: number
): number | undefined {
  const s = game.players[slot];
  return s && s.state === "filled" ? s.player.id : undefined;
}

function slotOfLobbyId(
  game: CurrentGameState,
  playerId: number
): number | undefined {
  const slot = game.players.findIndex(
    x => x.state === "filled" && x.player.id === playerId
  );
  return slot === -1 ? undefined : slot;
}

/** Where a received recording is put before the game is started on it. */
function rejoinBundlePath(): string {
  return path.join(os.tmpdir(), "rwe-rejoin-bundle.rwereplay");
}

export const gameRoomEpic = (
  action$: rx.Observable<AppAction>,
  state$: StateObservable<State>,
  deps: EpicDependencies
): rx.Observable<AppAction> => {
  const clientService = deps.clientService;
  const masterClientService = deps.masterClentService;

  // The game this launcher started, while it is running. Kept because the
  // rejoin is a conversation with it: the server asks us to let somebody back
  // in, and only the running game can agree a tick and cut the recording.
  let runningGame: RunningRwe | undefined;

  // What the server said when this game started. A returning peer is started
  // with the same players at the same addresses as the game it is rejoining,
  // which is exactly what this carries.
  let startInfo: protocol.StartGamePayload | undefined;

  /**
   * Runs the game and turns what it says into what the lobby has to pass on.
   *
   * Three things come back over the bridge and each goes straight out to the
   * server: who was lost, where the recording for a returning player is, and
   * that a rejoin will not be happening. Nothing here decides anything -- the
   * engine has decided it all already, on a tick every peer agrees about.
   */
  const runGame = (game: CurrentGameState, args: RweArgs) => {
    const running = execRwe(args);
    runningGame = running;
    return rx.merge(
      running.events.pipe(
        rxop.tap(e => {
          const playerId =
            e.player === undefined ? undefined : lobbyIdOfSlot(game, e.player);
          if (playerId === undefined) {
            return;
          }
          switch (e.event) {
            case "player-dropped": {
              clientService.playerDroppedFromGame(playerId, e.tick ?? 0);
              break;
            }
            case "rejoin-bundle": {
              if (e.file === undefined) {
                break;
              }
              // Read here rather than sent as a path: the other player is on
              // another machine, and the lobby connection is the only thing
              // either of us holds that can carry bytes to them.
              try {
                const data = fs.readFileSync(e.file);
                clientService.sendRejoinBundle(
                  playerId,
                  e.tick ?? 0,
                  data.buffer.slice(
                    data.byteOffset,
                    data.byteOffset + data.byteLength
                  ) as ArrayBuffer
                );
              } catch (error) {
                clientService.sendRejoinRefused(
                  playerId,
                  `the recording could not be read: ${error}`
                );
              }
              break;
            }
            case "rejoin-refused": {
              clientService.sendRejoinRefused(
                playerId,
                e.reason ?? "the game refused"
              );
              break;
            }
          }
        }),
        rxop.ignoreElements()
      ),
      rx.from(running.finished).pipe(
        rxop.catchError(() => rx.of(undefined)),
        rxop.tap(() => {
          runningGame = undefined;
        }),
        rxop.map(() => gameEnded())
      )
    );
  };

  return action$.pipe(
    rxop.flatMap(action => {
      switch (action.type) {
        case "HOST_GAME_FORM_CONFIRM": {
          masterClientService.requestCreateGame(
            action.gameDescription,
            action.players
          );
          action$
            .pipe(
              ofType<
                AppAction,
                ReceiveCreateGameResponseAction["type"],
                ReceiveCreateGameResponseAction
              >("RECEIVE_CREATE_GAME_RESPONSE"),
              rxop.first()
            )
            .subscribe(x => {
              getIpv4Address().then(clientAddress => {
                clientService.connectToServer(
                  `${masterServer()}/rooms`,
                  x.payload.game_id,
                  action.playerName,
                  clientAddress,
                  state$.value.installedMods!.map(m => m.name),
                  x.payload.admin_key
                );
              });
            });
          break;
        }
        case "JOIN_SELECTED_GAME_CONFIRM": {
          const state = state$.value;
          if (state.selectedGameId === undefined) {
            break;
          }
          const selectedGameId = state.selectedGameId;
          const connectionString = `${masterServer()}/rooms`;
          console.log(`connecting to ${connectionString}`);
          rx.from(getIpv4Address())
            .pipe(
              rxop.takeUntil(
                action$.pipe(
                  ofType<AppAction, LeaveGameAction["type"], LeaveGameAction>(
                    "LEAVE_GAME"
                  )
                )
              )
            )
            .subscribe(clientAddress => {
              clientService.connectToServer(
                connectionString,
                selectedGameId,
                action.name,
                clientAddress,
                state.installedMods!.map(m => m.name)
              );
            });
          break;
        }
        case "SEND_CHAT_MESSAGE": {
          clientService.sendChatMessage(action.message);
          break;
        }
        case "CHANGE_SIDE": {
          clientService.changeSide(action.side);
          break;
        }
        case "CHANGE_TEAM": {
          clientService.changeTeam(action.team);
          break;
        }
        case "CHANGE_COLOR": {
          clientService.changeColor(action.color);
          break;
        }
        case "OPEN_SLOT": {
          clientService.openSlot(action.slotId);
          break;
        }
        case "CLOSE_SLOT": {
          clientService.closeSlot(action.slotId);
          break;
        }
        case "REQUEST_SET_ACTIVE_MODS": {
          clientService.setActiveMods(action.mods);
          break;
        }
        case "RECEIVE_ACTIVE_MODS_CHANGED": {
          deps.bridgeService.clearDataPaths();
          const installedMods = state$.value.installedMods;
          if (!installedMods) {
            break;
          }
          const resolvedMods = choose(action.payload.mods, x =>
            installedMods.find(y => y.name === x)
          );
          for (const info of resolvedMods) {
            deps.bridgeService.addDataPath(info.path);
          }
          break;
        }
        case "TOGGLE_READY": {
          const state = state$.value;
          const room = state.currentGame;
          if (!room) {
            break;
          }
          if (room.localPlayerId === undefined) {
            break;
          }
          const localPlayerSlot = room.players.find(
            x => x.state === "filled" && x.player.id === room.localPlayerId
          )! as FilledPlayerSlot;
          const currentValue = localPlayerSlot.player.ready;
          clientService.setReadyState(!currentValue);
          break;
        }
        case "LEAVE_GAME": {
          clientService.disconnect();
          break;
        }
        case "DISCONNECT_GAME": {
          deps.bridgeService.clearDataPaths();
          break;
        }
        case "START_GAME": {
          deps.bridgeService.clearDataPaths();
          break;
        }
        case "SEND_START_GAME": {
          clientService.requestStartGame();
          break;
        }
        case "RECEIVE_START_GAME": {
          const state = state$.value;
          const room = state.currentGame;
          if (!room || !state.installedMods) {
            break;
          }
          startInfo = action.payload;
          const args = rweArgsFromCurrentGameState(
            state.installedMods,
            room,
            action.payload
          );
          return runGame(room, { ...args, bridge: true });
        }
        case "SEND_REQUEST_REJOIN": {
          clientService.requestRejoin();
          break;
        }
        case "RECEIVE_REJOIN_REQUESTED": {
          const room = state$.value.currentGame;
          if (!room || !runningGame) {
            // Nothing to ask. The server picked this peer because it is the
            // one entitled to answer, so saying nothing would leave the other
            // player waiting for ever.
            clientService.sendRejoinRefused(
              action.payload.playerId,
              "that player's game is no longer running"
            );
            break;
          }
          const slot = slotOfLobbyId(room, action.payload.playerId);
          if (slot === undefined) {
            clientService.sendRejoinRefused(
              action.payload.playerId,
              "that player has no seat in this game"
            );
            break;
          }
          // Everything after this comes back over the bridge: either the
          // recording, or a refusal with the engine's own reason.
          runningGame.requestRejoin(slot);
          break;
        }
        case "RECEIVE_REJOIN_BUNDLE": {
          const state = state$.value;
          const room = state.currentGame;
          if (!room || !state.installedMods || !startInfo) {
            // Nothing to start the game from. This launcher was not the one
            // that started the game it is being invited back into -- it has
            // been restarted since -- and the addresses of the other peers
            // came with the start message, which is gone with it.
            console.log(
              "Received a rejoin bundle, but this launcher does not know how the game was started"
            );
            break;
          }
          if (action.payload.playerId !== room.localPlayerId) {
            break;
          }
          const bundlePath = rejoinBundlePath();
          try {
            fs.writeFileSync(
              bundlePath,
              Buffer.from(new Uint8Array(action.payload.data))
            );
          } catch (error) {
            console.log(`Could not write the rejoin bundle: ${error}`);
            break;
          }
          const args = rweArgsFromCurrentGameState(
            state.installedMods,
            room,
            startInfo
          );
          return runGame(room, {
            ...args,
            bridge: true,
            rejoinFile: bundlePath,
            rejoinTick: action.payload.tick,
          });
        }
        case "CHANGE_MAP": {
          const state = state$.value;
          const room = getRoom(state);
          if (!room) {
            break;
          }
          if (!room.mapDialog) {
            break;
          }
          if (!room.mapDialog.selectedMap) {
            break;
          }
          clientService.changeMap(room.mapDialog.selectedMap);
          return rx.of<AppAction>(closeSelectMapDialog());
        }
      }
      return rx.empty();
    })
  );
};
