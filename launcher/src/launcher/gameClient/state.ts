import { AppAction } from "../actions";
import {
  ModDifference,
  ModFingerprint,
  checkArchiveAgreement,
} from "../../common/archives";
import { findAndMap, assertNever, choose } from "../../common/util";

export type PlayerSide = "ARM" | "CORE";

export type PlayerColor = number;

export interface PlayerInfo {
  id: number;
  name: string;
  side: PlayerSide;
  color: PlayerColor;
  team?: number;
  ready: boolean;
  installedMods: string[];
  /** The archives in each of their mods, hashed. Undefined until theirs arrive. */
  archives?: ModFingerprint[];
}

export interface ChatMessage {
  senderName?: string;
  message: string;
}

export interface FilledPlayerSlot {
  state: "filled";
  player: PlayerInfo;
}

export interface EmptyPlayerSlot {
  state: "empty";
}

export interface ClosedPlayerSlot {
  state: "closed";
}

export type PlayerSlot = EmptyPlayerSlot | ClosedPlayerSlot | FilledPlayerSlot;

export interface CurrentGameState {
  localPlayerId?: number;
  adminPlayerId?: number;
  players: PlayerSlot[];
  messages: ChatMessage[];
  mapName?: string;
  activeMods: string[];

  /**
   * Whether the game has been started. It stays true for the rest of the
   * room's life: a room whose game has begun is never a lobby again, and a
   * player whose own game has ended while this is true is one who can ask to
   * rejoin.
   */
  started: boolean;

  /**
   * Players their own game has thrown out, as the peers still in it report
   * them. What the rejoin button is offered on.
   */
  droppedPlayerIds: number[];

  /** Why the last rejoin asked for did not happen, for the room to show. */
  rejoinRefusedReason?: string;
}

/**
 * Whether this player may ask for their seat back: they are in a game that has
 * started, their own peers have declared them lost, and they are not playing.
 * Whether they are playing is not this state's business, so it is asked for.
 */
export function canRequestRejoin(
  room: CurrentGameState,
  isGameRunning: boolean
): boolean {
  if (!room.started || isGameRunning) {
    return false;
  }
  if (room.localPlayerId === undefined) {
    return false;
  }
  return room.droppedPlayerIds.includes(room.localPlayerId);
}

export type CanStartGameError =
  | { type: "not-admin" }
  | { type: "no-players" }
  | { type: "no-map" }
  | { type: "no-mods" }
  | { type: "unfilled-slots"; unfilledSlots: number[] }
  | { type: "unready-players"; unreadyPlayerIds: number[] }
  | {
      type: "missing-mods";
      playersMissingMods: { playerId: number; mods: string[] }[];
    }
  | {
      type: "archives-differ";
      playersWithWrongArchives: { playerId: number; mods: ModDifference[] }[];
    }
  | { type: "archives-unchecked"; playerIds: number[] };

export type CanStartGameResult =
  { result: "ok" } | { result: "err"; errors: CanStartGameError[] };

export function canStartGame(room: CurrentGameState): CanStartGameResult {
  const errors: CanStartGameError[] = [];

  if (room.adminPlayerId !== room.localPlayerId) {
    errors.push({ type: "not-admin" });
  }
  if (room.players.filter(x => x.state === "filled").length < 2) {
    errors.push({ type: "no-players" });
  }

  if (room.activeMods.length === 0) {
    errors.push({ type: "no-mods" });
  }

  if (room.mapName === undefined) {
    errors.push({ type: "no-map" });
  }

  const unfilledSlots = choose(room.players, (x, i) =>
    x.state === "empty" ? i : undefined
  );
  if (unfilledSlots.length > 0) {
    errors.push({ type: "unfilled-slots", unfilledSlots });
  }

  const unreadyPlayerIds = choose(room.players, x => {
    switch (x.state) {
      case "filled": {
        if (!x.player.ready) {
          return x.player.id;
        }
        return undefined;
      }
      case "closed":
        return undefined;
      case "empty":
        return undefined;
      default:
        assertNever(x);
    }
  });

  if (unreadyPlayerIds.length !== 0) {
    errors.push({ type: "unready-players", unreadyPlayerIds });
  }

  const playersMissingMods = choose(room.players, x => {
    switch (x.state) {
      case "filled": {
        const missingMods = room.activeMods.filter(
          m => !x.player.installedMods.includes(m)
        );
        if (missingMods.length !== 0) {
          return { playerId: x.player.id, mods: missingMods };
        }
        return undefined;
      }
      case "closed":
        return undefined;
      case "empty":
        return undefined;
      default:
        assertNever(x);
    }
  });

  if (playersMissingMods.length !== 0) {
    errors.push({ type: "missing-mods", playersMissingMods });
  }

  // Having the same mods by name is not the same as having the same data in
  // them, and the difference is a desync. Issue #43.
  const agreement = checkArchiveAgreement(
    room.activeMods,
    choose(room.players, x =>
      x.state === "filled"
        ? { playerId: x.player.id, mods: x.player.archives }
        : undefined
    ),
    room.adminPlayerId
  );
  if (agreement.mismatches.length !== 0) {
    errors.push({
      type: "archives-differ",
      playersWithWrongArchives: agreement.mismatches,
    });
  }
  if (agreement.uncheckedPlayerIds.length !== 0) {
    errors.push({
      type: "archives-unchecked",
      playerIds: agreement.uncheckedPlayerIds,
    });
  }

  return errors.length === 0 ? { result: "ok" } : { result: "err", errors };
}

function findPlayer(
  players: PlayerSlot[],
  playerId: number
): PlayerInfo | undefined {
  return findAndMap(players, x =>
    x.state === "filled" && x.player.id === playerId ? x.player : undefined
  );
}

function currentGameReducer(
  room: CurrentGameState,
  action: AppAction
): CurrentGameState {
  switch (action.type) {
    case "RECEIVE_START_GAME": {
      return { ...room, started: true };
    }
    case "RECEIVE_PLAYER_DROPPED_FROM_GAME": {
      // Said once by every peer still in the game, so it arrives as many
      // times as there are peers and is remembered once.
      if (room.droppedPlayerIds.includes(action.payload.playerId)) {
        return room;
      }
      return {
        ...room,
        droppedPlayerIds: [...room.droppedPlayerIds, action.payload.playerId],
      };
    }
    case "RECEIVE_REJOIN_BUNDLE": {
      // On their way back in, so no longer out. The peers that stayed say so
      // too, in their own time, when their games agree the rejoin tick.
      return {
        ...room,
        droppedPlayerIds: room.droppedPlayerIds.filter(
          x => x !== action.payload.playerId
        ),
        rejoinRefusedReason: undefined,
      };
    }
    case "RECEIVE_REJOIN_REFUSED": {
      if (action.payload.playerId !== room.localPlayerId) {
        return room;
      }
      return { ...room, rejoinRefusedReason: action.payload.reason };
    }
    case "SEND_REQUEST_REJOIN": {
      return { ...room, rejoinRefusedReason: undefined };
    }
    case "RECEIVE_PLAYER_JOINED": {
      const newPlayer: PlayerSlot = {
        state: "filled",
        player: {
          id: action.payload.playerId,
          name: action.payload.name,
          side: "ARM",
          color: 0,
          team: 0,
          ready: false,
          installedMods: action.payload.installedMods,
        },
      };
      const newPlayerIndex = room.players.findIndex(x => x.state === "empty");
      if (newPlayerIndex === -1) {
        throw new Error("Player joined game, but already full!");
      }
      const newPlayers = room.players.map((x, i) =>
        i === newPlayerIndex ? newPlayer : x
      );
      return { ...room, players: newPlayers };
    }
    case "RECEIVE_PLAYER_LEFT": {
      const newPlayers = room.players.map(x => {
        if (x.state === "filled" && x.player.id === action.payload.playerId) {
          const e: EmptyPlayerSlot = { state: "empty" };
          return e;
        }
        return x;
      });
      const newAdminId =
        action.payload.newAdminPlayerId !== undefined
          ? action.payload.newAdminPlayerId
          : action.payload.playerId === room.adminPlayerId
            ? undefined
            : room.adminPlayerId;

      return { ...room, players: newPlayers, adminPlayerId: newAdminId };
    }
    case "RECEIVE_CHAT_MESSAGE": {
      const newMessages = room.messages.slice();
      const sender = findPlayer(room.players, action.payload.playerId);
      const senderName = sender ? sender.name : undefined;
      const newMessage: ChatMessage = {
        senderName: senderName,
        message: action.payload.message,
      };
      newMessages.push(newMessage);
      return { ...room, messages: newMessages };
    }
    case "RECEIVE_PLAYER_CHANGED_SIDE": {
      const newPlayers = room.players.map(x => {
        if (x.state !== "filled" || x.player.id !== action.payload.playerId) {
          return x;
        }
        const p = { ...x.player, side: action.payload.side };
        return { ...x, player: p };
      });
      return { ...room, players: newPlayers };
    }
    case "RECEIVE_PLAYER_CHANGED_TEAM": {
      const newPlayers = room.players.map(x => {
        if (x.state !== "filled" || x.player.id !== action.payload.playerId) {
          return x;
        }
        const p = { ...x.player, team: action.payload.team };
        return { ...x, player: p };
      });
      return { ...room, players: newPlayers };
    }
    case "RECEIVE_PLAYER_CHANGED_COLOR": {
      const newPlayers = room.players.map(x => {
        if (x.state !== "filled" || x.player.id !== action.payload.playerId) {
          return x;
        }
        const p = { ...x.player, color: action.payload.color };
        return { ...x, player: p };
      });
      return { ...room, players: newPlayers };
    }
    case "RECEIVE_PLAYER_READY": {
      const newPlayers = room.players.map(x => {
        if (x.state !== "filled" || x.player.id !== action.payload.playerId) {
          return x;
        }
        const p = { ...x.player, ready: action.payload.value };
        return { ...x, player: p };
      });
      return { ...room, players: newPlayers };
    }
    case "RECEIVE_PLAYER_ARCHIVES_CHANGED": {
      const newPlayers = room.players.map(x => {
        if (x.state !== "filled" || x.player.id !== action.payload.playerId) {
          return x;
        }
        const p = { ...x.player, archives: action.payload.mods };
        return { ...x, player: p };
      });
      return { ...room, players: newPlayers };
    }
    case "RECEIVE_SLOT_OPENED": {
      const newPlayers = room.players.map((x, i) => {
        if (i !== action.payload.slotId) {
          return x;
        }
        const e: EmptyPlayerSlot = { state: "empty" };
        return e;
      });
      return { ...room, players: newPlayers };
    }
    case "RECEIVE_SLOT_CLOSED": {
      const newPlayers = room.players.map((x, i) => {
        if (i !== action.payload.slotId) {
          return x;
        }
        const e: ClosedPlayerSlot = { state: "closed" };
        return e;
      });
      return { ...room, players: newPlayers };
    }
    case "RECEIVE_ACTIVE_MODS_CHANGED": {
      return { ...room, activeMods: action.payload.mods };
    }
    case "RECEIVE_MAP_CHANGED": {
      return { ...room, mapName: action.data.mapName };
    }
    default: {
      return room;
    }
  }
}

export function currentGameWrapperReducer(
  state: CurrentGameState | undefined,
  action: AppAction
): CurrentGameState | undefined {
  switch (action.type) {
    case "RECEIVE_HANDSHAKE_RESPONSE": {
      const game: CurrentGameState = {
        players: action.payload.players,
        localPlayerId: action.payload.playerId,
        adminPlayerId: action.payload.adminPlayerId,
        mapName: action.payload.mapName,
        messages: [],
        activeMods: action.payload.activeMods,
        started: false,
        droppedPlayerIds: [],
      };
      return game;
    }
    case "DISCONNECT_GAME": {
      return undefined;
    }
    default: {
      if (!state) {
        return state;
      }
      return currentGameReducer(state, action);
    }
  }
}
