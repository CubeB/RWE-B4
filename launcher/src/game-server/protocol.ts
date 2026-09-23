import { ModFingerprint } from "../common/archives";

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
  /**
   * The archives inside each installed mod, hashed. Undefined until that
   * player has finished hashing, which is a while after they joined when the
   * mod runs to gigabytes -- see SetArchives.
   */
  archives?: ModFingerprint[];
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

// Emitted by the client upon connection
export const Handshake = "handshake";
export interface HandshakePayload {
  gameId: number;
  name: string;
  ipv4Address: string;
  adminKey?: string;
  installedMods: string[];
}

// Emitted by the server to a client in response to a handshake
export const HandshakeResponse = "handshake-response";
export interface HandshakeResponsePayload {
  playerId: number;
  adminPlayerId?: number;
  players: PlayerSlot[];
  mapName?: string;
  activeMods: string[];
}

// Emitted by the client when the user sends a chat message
export const ChatMessage = "chat-message";
export type ChatMessagePayload = string;

// Broadcast by the server to all clients when a chat message is sent
export const PlayerChatMessage = "player-chat-message";
export interface PlayerChatMessagePayload {
  playerId: number;
  message: string;
}

// Broadcast by the server to all clients when a player joins
export const PlayerJoined = "player-joined";
export interface PlayerJoinedPayload {
  playerId: number;
  name: string;
  installedMods: string[];
}

// Broadcast by the server to all clients when a player leaves
export const PlayerLeft = "player-left";
export interface PlayerLeftPayload {
  playerId: number;
  newAdminPlayerId?: number;
}

// Emitted by the client when the player changes ready state
export const Ready = "ready";
export type ReadyPayload = boolean;

// Broadcast by the server to all clients when a player changes ready state
export const PlayerReady = "player-ready";
export interface PlayerReadyPayload {
  playerId: number;
  value: boolean;
}

// Emitted by the client when the player wants to start the game
export const RequestStartGame = "request-start-game";

// Broadcast by the server to all clients to announce the start of the game
export const StartGame = "start-game";
export interface StartGamePayload {
  addresses: [number, string][];
}

export const ChangeSide = "change-side";
export interface ChangeSidePayload {
  side: PlayerSide;
}

export const PlayerChangedSide = "player-changed-side";
export interface PlayerChangedSidePayload {
  playerId: number;
  side: PlayerSide;
}

export const ChangeTeam = "change-team";
export interface ChangeTeamPayload {
  team?: number;
}

export const PlayerChangedTeam = "player-changed-team";
export interface PlayerChangedTeamPayload {
  playerId: number;
  team?: number;
}

export const ChangeColor = "change-color";
export interface ChangeColorPayload {
  color: number;
}

export const PlayerChangedColor = "player-changed-color";
export interface PlayerChangedColorPayload {
  playerId: number;
  color: number;
}

export const OpenSlot = "open-slot";
export interface OpenSlotPayload {
  slotId: number;
}

export const CloseSlot = "close-slot";
export interface CloseSlotPayload {
  slotId: number;
}

// Emitted by the client once it has hashed its installed mods, which is a
// separate message from the handshake rather than a field on it because the
// hashing takes as long as it takes and joining should not wait for it.
export const SetArchives = "set-archives";
export interface SetArchivesPayload {
  mods: ModFingerprint[];
}

// Broadcast by the server to all clients when a player's archives arrive
export const PlayerArchivesChanged = "player-archives-changed";
export interface PlayerArchivesChangedPayload {
  playerId: number;
  mods: ModFingerprint[];
}

export const SetActiveMods = "set-active-mods";
export interface SetActiveModsPayload {
  mods: string[];
}

export const ActiveModsChanged = "active-mods-changed";
export interface ActiveModsChangedPayload {
  mods: string[];
}

export const SlotOpened = "slot-opened";
export interface SlotOpenedPayload {
  slotId: number;
}

export const SlotClosed = "slot-closed";
export interface SlotClosedPayload {
  slotId: number;
}

export const ChangeMap = "change-map";
export interface ChangeMapPayload {
  mapName: string;
}

export const MapChanged = "map-changed";
export interface MapChangedPayload {
  mapName: string;
}

// Letting a dropped player back in (#188).
//
// The engine can do all of it but move the bytes: the peer entitled to speak
// for a dropped player can agree a tick and cut a recording of everything the
// returning peer missed, but the only reliable connection anyone here holds is
// the one to this server, for the lobby. So the lobby carries it. The engine
// speaks to its own launcher over the bridge (--bridge, one JSON object a
// line); these five messages are the rest of the path.

// Emitted by a client whose game has declared a peer lost.
export const PlayerDroppedFromGame = "player-dropped-from-game";
export interface PlayerDroppedFromGamePayload {
  /** The lobby's id, not the engine's slot: the launcher translates. */
  playerId: number;
  tick: number;
}

// Broadcast by the server so every launcher knows who is out of the game.
export const PlayerDroppedFromGameBroadcast = "game-player-dropped";
export interface PlayerDroppedFromGameBroadcastPayload {
  playerId: number;
  tick: number;
}

// Emitted by a client that has been dropped and wants back in. The server
// passes it to the one peer entitled to answer it -- the lowest-numbered slot
// still in the game, which is the same rule the engine uses to decide who may
// declare a drop, computed here from the same facts.
export const RequestRejoin = "request-rejoin";

// Sent by the server to that peer.
export const RejoinRequested = "rejoin-requested";
export interface RejoinRequestedPayload {
  playerId: number;
}

// Emitted by that peer's client once its game has cut the recording, and sent
// on by the server to the player it is for. The recording is the whole game so
// far as commands, which is kilobytes rather than megabytes -- what makes it
// small is that a lockstep game's commands are all there is.
export const RejoinBundle = "rejoin-bundle";
export interface RejoinBundlePayload {
  playerId: number;
  /** The tick the returning peer resumes at, which its engine is told. */
  tick: number;
  data: ArrayBuffer;
}

// Either direction: it is not going to happen, and why. Sent by the peer that
// was asked, and by the server when there is nobody to ask.
export const RejoinRefused = "rejoin-refused";
export interface RejoinRefusedPayload {
  playerId: number;
  reason: string;
}
