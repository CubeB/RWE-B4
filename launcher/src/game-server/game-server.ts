import * as crypto from "crypto";
import * as rx from "rxjs";
import type { Namespace, Socket } from "socket.io";
import { ModFingerprint, checkArchiveAgreement } from "../common/archives";
import { assertNever, choose, findAndMap, getAddr } from "../common/util";
import * as protocol from "./protocol";
import * as valid from "./validation";

type PlayerSide = "ARM" | "CORE";
type PlayerColor = number;

interface PlayerInfo {
  id: number;
  name: string;
  host: string;
  ipv4Address: string;
  side: PlayerSide;
  color: PlayerColor;
  team?: number;
  ready: boolean;
  installedMods: string[];
  archives?: ModFingerprint[];
}

function toProtocolPlayerInfo(info: PlayerInfo): protocol.PlayerInfo {
  return {
    id: info.id,
    name: info.name,
    side: info.side,
    color: info.color,
    team: info.team,
    ready: info.ready,
    installedMods: info.installedMods,
    archives: info.archives,
  };
}

interface FilledPlayerSlot {
  state: "filled";
  player: PlayerInfo;
}

interface EmptyPlayerSlot {
  state: "empty";
}

interface ClosedPlayerSlot {
  state: "closed";
}

type PlayerSlot = EmptyPlayerSlot | ClosedPlayerSlot | FilledPlayerSlot;

function toProtocolPlayerSlot(slot: PlayerSlot): protocol.PlayerSlot {
  switch (slot.state) {
    case "empty":
    case "closed":
      return slot;
    case "filled":
      return { state: "filled", player: toProtocolPlayerInfo(slot.player) };
  }
  assertNever(slot);
}

export interface AdminUnclaimed {
  state: "unclaimed";
  adminKey: string;
}

export interface AdminClaimed {
  state: "claimed";
  adminPlayerId?: number;
}

export type AdminState = AdminUnclaimed | AdminClaimed;

export interface Room {
  description: string;
  nextPlayerId: number;
  players: PlayerSlot[];
  adminState: AdminState;
  mapName?: string;
  activeMods: string[];

  /**
   * Players whose game has dropped them, in the order they were lost.
   *
   * Kept so that a rejoin can be sent to the one peer entitled to answer it.
   * The engine's rule is the lowest-numbered slot that is neither the player
   * in question nor itself dropped, computed rather than agreed; this is the
   * same rule over the same facts, so the peer asked is the peer that will
   * say yes. See rejoinHostFor.
   */
  droppedPlayerIds: number[];

  /** When the room was made, so one nobody ever joins can be cleared away. */
  createdAt: number;
}

/**
 * Rooms at most, and how long one may stand with nobody in it. A room is
 * deleted when its last player leaves, but one that nobody ever joined had no
 * last player, and creating them was unauthenticated and unlimited: a loop of
 * create-game grew the server without bound. Issue #75.
 */
const MaxRooms = 1000;
const UnjoinedRoomLifetimeMs = 5 * 60 * 1000;

/**
 * The player whose game may let `playerId` back in: the lowest-numbered slot
 * still in the game. Nothing if there is nobody left to ask, which is a game
 * with one peer in it and nothing to rejoin.
 */
function rejoinHostFor(room: Room, playerId: number): number | undefined {
  for (const slot of room.players) {
    if (slot.state !== "filled") {
      continue;
    }
    if (slot.player.id === playerId) {
      continue;
    }
    if (room.droppedPlayerIds.includes(slot.player.id)) {
      continue;
    }
    return slot.player.id;
  }
  return undefined;
}

function generateAdminKey() {
  return crypto.randomBytes(16).toString("hex");
}

function findPlayer(
  players: protocol.PlayerSlot[],
  playerId: number
): protocol.PlayerInfo | undefined {
  return findAndMap(players, x =>
    x.state === "filled" && x.player.id === playerId ? x.player : undefined
  );
}

function isIpv4Client(player: PlayerInfo): boolean {
  return /^(::ffff:)?\d+\.\d+\.\d+\.\d+$/.test(player.host);
}

export interface GameCreatedInfo {
  gameId: number;
  adminKey: string;
}

export class GameServer {
  private readonly ns: Namespace;
  private readonly reverseProxy: boolean;

  private nextRoomId = 1;
  private readonly rooms = new Map<number, Room>();

  private _gameUpdated = new rx.Subject<[number, Room]>();
  private _gameDeleted = new rx.Subject<number>();

  constructor(ns: Namespace, reverseProxy: boolean) {
    this.ns = ns;
    this.reverseProxy = reverseProxy;
    this.connect();
  }

  get gameUpdated(): rx.Observable<[number, Room]> {
    return this._gameUpdated;
  }
  get gameDeleted(): rx.Observable<number> {
    return this._gameDeleted;
  }

  getAllRooms() {
    return this.rooms.entries();
  }

  /**
   * A room, or nothing if the request is not one the server will honour: a
   * description that is not a short string, a seat count outside 1 to 10,
   * or no room left under MaxRooms once the unjoined ones have been swept.
   */
  createRoom(
    rawDescription: unknown,
    rawMaxPlayers: unknown
  ): GameCreatedInfo | undefined {
    const description = valid.lobbyString(
      rawDescription,
      valid.MaxDescriptionLength
    );
    const maxPlayers = valid.intInRange(rawMaxPlayers, 1, valid.MaxPlayers);
    if (description === undefined || maxPlayers === undefined) {
      this.log("Refused to create a room: bad description or player count");
      return undefined;
    }
    this.sweepUnjoinedRooms();
    if (this.rooms.size >= MaxRooms) {
      this.log("Refused to create a room: the server is full");
      return undefined;
    }
    const id = this.nextRoomId++;
    const adminKey = generateAdminKey();
    const players: PlayerSlot[] = new Array(valid.MaxPlayers);
    for (let i = 0; i < maxPlayers; ++i) {
      players[i] = { state: "empty" };
    }
    for (let i = maxPlayers; i < valid.MaxPlayers; ++i) {
      players[i] = { state: "closed" };
    }
    this.rooms.set(id, {
      description,
      nextPlayerId: 1,
      players,
      adminState: { state: "unclaimed", adminKey },
      activeMods: [],
      droppedPlayerIds: [],
      createdAt: Date.now(),
    });

    return { gameId: id, adminKey };
  }

  /** Deletes rooms that nobody has joined within UnjoinedRoomLifetimeMs. */
  private sweepUnjoinedRooms() {
    const now = Date.now();
    for (const [id, room] of Array.from(this.rooms.entries())) {
      if (
        room.nextPlayerId === 1 &&
        now - room.createdAt > UnjoinedRoomLifetimeMs
      ) {
        this.deleteRoom(id);
      }
    }
  }

  deleteRoom(id: number) {
    if (this.rooms.delete(id)) {
      this._gameDeleted.next(id);
      return true;
    }
    return false;
  }

  getAdminKey(id: number) {
    const room = this.rooms.get(id);
    if (!room) {
      return undefined;
    }
    return room.adminState.state === "unclaimed"
      ? room.adminState.adminKey
      : undefined;
  }

  getRoomInfo(id: number) {
    return this.rooms.get(id);
  }

  getNumberOfPlayers(id: number) {
    const room = this.rooms.get(id);
    if (!room) {
      return undefined;
    }
    return room.players.length;
  }

  connect() {
    this.ns.on("connection", (socket: Socket) => {
      const address = getAddr(socket, this.reverseProxy);
      this.log(`Received connection from ${address}`);

      // Every handler runs inside this, so that a message it did not expect
      // costs that message and not the process. socket.io calls listeners
      // from process.nextTick, where a throw is an uncaught exception and
      // takes the master server and every lobby down with it. Issue #75.
      const on = (event: string, handler: (...args: unknown[]) => void) => {
        socket.on(event, (...args: unknown[]) => {
          try {
            handler(...args);
          } catch (e) {
            this.log(`Error handling "${event}" from ${address}: ${e}`);
          }
        });
      };

      on(protocol.Handshake, (rawData: unknown) => {
        const data = valid.payload(rawData);
        const name = valid.playerName(data?.name);
        const roomId = valid.intInRange(
          data?.gameId,
          1,
          Number.MAX_SAFE_INTEGER
        );
        const ipv4Address = valid.ipv4Address(data?.ipv4Address);
        const installedMods = valid.stringList(
          data?.installedMods,
          valid.MaxMods,
          valid.MaxMapNameLength
        );
        if (
          name === undefined ||
          roomId === undefined ||
          ipv4Address === undefined ||
          installedMods === undefined
        ) {
          this.log(`Rejected a malformed handshake from ${address}`);
          socket.disconnect();
          return;
        }
        this.log(`Received handshake from ${address} with name "${name}"`);

        const room = this.rooms.get(roomId);
        if (!room) {
          this.log(
            `Received handshake to connect to room ${roomId}, but room does not exist`
          );
          socket.disconnect();
          return;
        }

        const playerId = room.nextPlayerId++;
        this.log(`Received new connection, assigned ID ${playerId}`);

        const freeSlotIndex = room.players.findIndex(x => x.state === "empty");
        if (freeSlotIndex === -1) {
          this.log(`Room full, rejecting client`);
          socket.disconnect();
          return;
        }

        room.players[freeSlotIndex] = {
          state: "filled",
          player: {
            id: playerId,
            name,
            host: address,
            ipv4Address,
            side: "ARM",
            color: 0,
            team: 0,
            ready: false,
            installedMods,
            archives: undefined,
          },
        };

        if (room.adminState.state === "unclaimed") {
          if (data?.adminKey === room.adminState.adminKey) {
            room.adminState = { state: "claimed", adminPlayerId: playerId };
          }
        }

        this._gameUpdated.next([roomId, room]);

        const handshakeResponse: protocol.HandshakeResponsePayload = {
          playerId: playerId,
          adminPlayerId:
            room.adminState.state === "claimed"
              ? room.adminState.adminPlayerId
              : undefined,
          players: room.players.map(x => toProtocolPlayerSlot(x)),
          mapName: room.mapName,
          activeMods: room.activeMods,
        };
        socket.emit(protocol.HandshakeResponse, handshakeResponse);

        const playerJoined: protocol.PlayerJoinedPayload = {
          playerId: playerId,
          name,
          installedMods,
        };

        this.sendToRoom(roomId, protocol.PlayerJoined, playerJoined);

        socket.join(this.getRoomString(roomId));
        socket.join(this.getPlayerString(roomId, playerId));

        on(protocol.ChatMessage, (message: unknown) => {
          this.onChatMessage(roomId, playerId, message);
        });
        on(protocol.ChangeSide, (d: unknown) => {
          this.onChangeSide(roomId, playerId, d);
        });
        on(protocol.ChangeTeam, (d: unknown) => {
          this.onChangeTeam(roomId, playerId, d);
        });
        on(protocol.ChangeColor, (d: unknown) => {
          this.onChangeColor(roomId, playerId, d);
        });
        on(protocol.Ready, (value: unknown) => {
          this.onPlayerReady(roomId, playerId, value);
        });
        on(protocol.SetArchives, (d: unknown) => {
          this.onSetArchives(roomId, playerId, d);
        });
        on(protocol.OpenSlot, (d: unknown) => {
          this.onOpenSlot(roomId, playerId, d);
        });
        on(protocol.CloseSlot, (d: unknown) => {
          this.onCloseSlot(roomId, playerId, d);
        });
        on(protocol.SetActiveMods, (d: unknown) => {
          this.onSetActiveMods(roomId, playerId, d);
        });
        on(protocol.ChangeMap, (d: unknown) => {
          this.onChangeMap(roomId, playerId, d);
        });
        on(protocol.RequestStartGame, () => {
          this.onPlayerRequestStartGame(roomId, playerId);
        });
        on(protocol.PlayerDroppedFromGame, (d: unknown) => {
          this.onPlayerDroppedFromGame(roomId, playerId, d);
        });
        on(protocol.RequestRejoin, () => {
          this.onRequestRejoin(roomId, playerId);
        });
        on(protocol.RejoinBundle, (d: unknown) => {
          this.onRejoinBundle(roomId, playerId, d);
        });
        on(protocol.RejoinRefused, (d: unknown) => {
          this.onRejoinRefused(roomId, playerId, d);
        });
        on("disconnect", () => {
          this.onDisconnected(roomId, playerId);
        });
      });
    });
  }

  private log(message: string) {
    console.log(`game server: ${message}`);
  }

  private getRoomString(roomId: number) {
    return `room/${roomId}`;
  }

  // Every socket joins a room of its own as well as the game's, so that a
  // message meant for one player -- a rejoin request, the recording that
  // answers it -- can be addressed without keeping a table of sockets.
  private getPlayerString(roomId: number, playerId: number) {
    return `room/${roomId}/player/${playerId}`;
  }

  private sendToPlayer(
    roomId: number,
    playerId: number,
    event: string,
    ...args: any[]
  ) {
    this.ns.to(this.getPlayerString(roomId, playerId)).emit(event, ...args);
  }

  private sendToRoom(roomId: number, event: string, ...args: any[]) {
    this.ns.to(this.getRoomString(roomId)).emit(event, ...args);
  }

  private onPlayerDroppedFromGame(
    roomId: number,
    reporterId: number,
    rawData: unknown
  ) {
    const room = this.rooms.get(roomId);
    if (!room) {
      return;
    }

    // A player this room has actually seated, and not the one reporting:
    // the list decides who may ask to rejoin and who is asked to let them
    // in, so an arbitrary id from any client used to be enough to poison it.
    const raw = valid.payload(rawData);
    const droppedId = valid.intInRange(raw?.playerId, 1, room.nextPlayerId - 1);
    const tick = valid.tick(raw?.tick);
    if (
      droppedId === undefined ||
      tick === undefined ||
      droppedId === reporterId
    ) {
      this.log(`Ignoring a malformed drop report from player ${reporterId}`);
      return;
    }
    const data: protocol.PlayerDroppedFromGamePayload = {
      playerId: droppedId,
      tick,
    };

    // Said by every peer that is still in the game, so it arrives once per
    // peer and is remembered once. The tick they agreed on is the same tick,
    // which is what makes a drop a fact about the game rather than about
    // whoever noticed it first.
    if (!room.droppedPlayerIds.includes(data.playerId)) {
      room.droppedPlayerIds.push(data.playerId);
      this.log(
        `Player ${data.playerId} was dropped from game ${roomId} at tick ${data.tick}`
      );
    }

    const payload: protocol.PlayerDroppedFromGameBroadcastPayload = {
      playerId: data.playerId,
      tick: data.tick,
    };
    this.sendToRoom(roomId, protocol.PlayerDroppedFromGameBroadcast, payload);
  }

  private onRequestRejoin(roomId: number, playerId: number) {
    const room = this.rooms.get(roomId);
    if (!room) {
      return;
    }

    const refuse = (reason: string) => {
      const payload: protocol.RejoinRefusedPayload = { playerId, reason };
      this.sendToPlayer(roomId, playerId, protocol.RejoinRefused, payload);
    };

    if (!room.droppedPlayerIds.includes(playerId)) {
      refuse("your game has not reported you as dropped");
      return;
    }

    const host = rejoinHostFor(room, playerId);
    if (host === undefined) {
      refuse("there is nobody left in the game to let you back in");
      return;
    }

    this.log(
      `Player ${playerId} asked to rejoin game ${roomId}; asking ${host}`
    );
    const payload: protocol.RejoinRequestedPayload = { playerId };
    this.sendToPlayer(roomId, host, protocol.RejoinRequested, payload);
  }

  private onRejoinBundle(roomId: number, playerId: number, rawData: unknown) {
    const room = this.rooms.get(roomId);
    if (!room) {
      return;
    }

    // The tick goes on the returning player's engine command line, so it is
    // held to being a tick; the recording itself is the engine's to judge.
    const raw = valid.payload(rawData);
    const targetId = valid.intInRange(raw?.playerId, 1, room.nextPlayerId - 1);
    const tick = valid.tick(raw?.tick);
    const bytes = raw?.data;
    if (
      targetId === undefined ||
      tick === undefined ||
      !(bytes instanceof ArrayBuffer || ArrayBuffer.isView(bytes))
    ) {
      this.log(`Ignoring a malformed rejoin bundle from player ${playerId}`);
      return;
    }
    const data: protocol.RejoinBundlePayload = {
      playerId: targetId,
      tick,
      data: bytes as ArrayBuffer,
    };

    if (rejoinHostFor(room, data.playerId) !== playerId) {
      this.log(
        `Ignoring a rejoin bundle for player ${data.playerId} from player ${playerId}, who was not asked for one`
      );
      return;
    }

    // Back in the game from the moment the recording exists: what follows is
    // theirs to carry out, and the peers that stayed are already stalled at
    // the tick it ends at.
    room.droppedPlayerIds = room.droppedPlayerIds.filter(
      x => x !== data.playerId
    );

    this.log(
      `Relaying a rejoin bundle for player ${data.playerId} at tick ${data.tick}`
    );
    this.sendToPlayer(roomId, data.playerId, protocol.RejoinBundle, data);
  }

  private onRejoinRefused(roomId: number, playerId: number, rawData: unknown) {
    const room = this.rooms.get(roomId);
    if (!room) {
      return;
    }

    const raw = valid.payload(rawData);
    const targetId = valid.intInRange(raw?.playerId, 1, room.nextPlayerId - 1);
    const reason = valid.lobbyString(raw?.reason, valid.MaxReasonLength);
    if (targetId === undefined || reason === undefined) {
      return;
    }
    const data: protocol.RejoinRefusedPayload = { playerId: targetId, reason };

    if (rejoinHostFor(room, data.playerId) !== playerId) {
      return;
    }

    this.sendToPlayer(roomId, data.playerId, protocol.RejoinRefused, data);
  }

  private onChatMessage(roomId: number, playerId: number, rawMessage: unknown) {
    const message = valid.lobbyString(rawMessage, valid.MaxChatLength);
    if (message === undefined) {
      return;
    }
    const payload: protocol.PlayerChatMessagePayload = { playerId, message };
    this.sendToRoom(roomId, protocol.PlayerChatMessage, payload);
  }

  private onChangeSide(roomId: number, playerId: number, rawData: unknown) {
    const room = this.rooms.get(roomId);
    if (!room) {
      throw new Error("onChangeSide triggered for non-existent room");
    }
    const player = findPlayer(room.players, playerId);
    if (!player) {
      throw new Error(`Failed to find player ${playerId}`);
    }
    const side = valid.side(valid.payload(rawData)?.side);
    if (side === undefined) {
      return;
    }
    player.side = side;
    const payload: protocol.PlayerChangedSidePayload = {
      playerId,
      side,
    };
    this.sendToRoom(roomId, protocol.PlayerChangedSide, payload);
  }

  private onChangeTeam(roomId: number, playerId: number, rawData: unknown) {
    const room = this.rooms.get(roomId);
    if (!room) {
      throw new Error("onChangeTeam triggered for non-existent room");
    }
    const player = findPlayer(room.players, playerId);
    if (!player) {
      throw new Error(`Failed to find player ${playerId}`);
    }
    // No team is a choice as well: the menu's first entry is blank.
    const rawTeam = valid.payload(rawData)?.team;
    const team =
      rawTeam === undefined
        ? undefined
        : valid.intInRange(rawTeam, 0, valid.MaxPlayers);
    if (rawTeam !== undefined && team === undefined) {
      return;
    }
    player.team = team;
    const payload: protocol.PlayerChangedTeamPayload = {
      playerId,
      team,
    };
    this.sendToRoom(roomId, protocol.PlayerChangedTeam, payload);
  }

  private onChangeColor(roomId: number, playerId: number, rawData: unknown) {
    const room = this.rooms.get(roomId);
    if (!room) {
      throw new Error("onChangeColor triggered for non-existent room");
    }
    const player = findPlayer(room.players, playerId);
    if (!player) {
      throw new Error(`Failed to find player ${playerId}`);
    }
    const color = valid.color(valid.payload(rawData)?.color);
    if (color === undefined) {
      return;
    }
    player.color = color;
    const payload: protocol.PlayerChangedColorPayload = {
      playerId,
      color,
    };
    this.sendToRoom(roomId, protocol.PlayerChangedColor, payload);
  }

  private onOpenSlot(roomId: number, playerId: number, rawData: unknown) {
    const room = this.rooms.get(roomId);
    if (!room) {
      throw new Error("onOpenSlot triggered for non-existent room");
    }
    if (
      room.adminState.state !== "claimed" ||
      room.adminState.adminPlayerId !== playerId
    ) {
      this.log(
        `Received open-slot from player ${playerId}, but that player is not admin!`
      );
      return;
    }
    const slotId = valid.slotId(valid.payload(rawData)?.slotId);
    if (slotId === undefined) {
      return;
    }
    const data = { slotId };
    const state = room.players[slotId].state;
    switch (state) {
      case "filled":
        this.log(
          `Player ${playerId} tried to open filled player slot ${data.slotId}`
        );
        return;
      case "closed":
      case "empty": {
        room.players[data.slotId] = { state: "empty" };
        const payload: protocol.SlotOpenedPayload = { slotId: data.slotId };
        this.sendToRoom(roomId, protocol.SlotOpened, payload);
        this._gameUpdated.next([roomId, room]);
        return;
      }
      default:
        return assertNever(state);
    }
  }

  private onCloseSlot(roomId: number, playerId: number, rawData: unknown) {
    const room = this.rooms.get(roomId);
    if (!room) {
      throw new Error("onCloseSlot triggered for non-existent room");
    }
    if (
      room.adminState.state !== "claimed" ||
      room.adminState.adminPlayerId !== playerId
    ) {
      this.log(
        `Received close-slot from player ${playerId}, but that player is not admin!`
      );
      return;
    }
    const slotId = valid.slotId(valid.payload(rawData)?.slotId);
    if (slotId === undefined) {
      return;
    }
    const data = { slotId };
    const state = room.players[slotId].state;
    switch (state) {
      case "filled":
        this.log(
          `Player ${playerId} tried to close filled player slot ${data.slotId}`
        );
        return;
      case "closed":
      case "empty": {
        room.players[data.slotId] = { state: "closed" };
        const payload: protocol.SlotClosedPayload = { slotId: data.slotId };
        this.sendToRoom(roomId, protocol.SlotClosed, payload);
        this._gameUpdated.next([roomId, room]);
        return;
      }
      default:
        return assertNever(state);
    }
  }

  onSetActiveMods(roomId: number, playerId: number, rawData: unknown) {
    const room = this.rooms.get(roomId);
    if (!room) {
      throw new Error("onSetActiveMods triggered for non-existent room");
    }
    if (
      room.adminState.state !== "claimed" ||
      room.adminState.adminPlayerId !== playerId
    ) {
      this.log(
        `Received set-active-mods from player ${playerId}, but that player is not admin!`
      );
      return;
    }
    const mods = valid.stringList(
      valid.payload(rawData)?.mods,
      valid.MaxMods,
      valid.MaxMapNameLength
    );
    if (mods === undefined) {
      return;
    }
    const payload: protocol.ActiveModsChangedPayload = { mods };
    this.sendToRoom(roomId, protocol.ActiveModsChanged, payload);
    room.activeMods = mods;
  }

  private onChangeMap(roomId: number, playerId: number, rawData: unknown) {
    const room = this.rooms.get(roomId);
    if (!room) {
      throw new Error("onChangeMap triggered for non-existent room");
    }
    if (
      room.adminState.state !== "claimed" ||
      room.adminState.adminPlayerId !== playerId
    ) {
      this.log(
        `Received change-map from player ${playerId}, but that player is not admin!`
      );
      return;
    }
    // Every player's engine is launched with this map name, so it is held
    // to being a short line of text.
    const mapName = valid.lobbyString(
      valid.payload(rawData)?.mapName,
      valid.MaxMapNameLength
    );
    if (mapName === undefined) {
      return;
    }
    room.mapName = mapName;
    const payload: protocol.MapChangedPayload = { mapName };
    this.sendToRoom(roomId, protocol.MapChanged, payload);
  }

  private onPlayerReady(roomId: number, playerId: number, rawValue: unknown) {
    if (typeof rawValue !== "boolean") {
      return;
    }
    const value = rawValue;
    const room = this.rooms.get(roomId);
    if (!room) {
      throw new Error("onPlayerReady triggered for non-existent room");
    }
    const player = findPlayer(room.players, playerId);
    if (!player) {
      throw new Error(`Failed to find player ${playerId}`);
    }
    player.ready = value;
    const payload: protocol.PlayerReadyPayload = { playerId, value };
    this.sendToRoom(roomId, protocol.PlayerReady, payload);
  }

  private onSetArchives(roomId: number, playerId: number, rawData: unknown) {
    const room = this.rooms.get(roomId);
    if (!room) {
      throw new Error("onSetArchives triggered for non-existent room");
    }
    const player = findPlayer(room.players, playerId);
    if (!player) {
      throw new Error(`Failed to find player ${playerId}`);
    }
    const mods = valid.modFingerprints(valid.payload(rawData)?.mods);
    if (mods === undefined) {
      return;
    }
    player.archives = mods;
    const payload: protocol.PlayerArchivesChangedPayload = {
      playerId,
      mods,
    };
    this.sendToRoom(roomId, protocol.PlayerArchivesChanged, payload);
  }

  private onPlayerRequestStartGame(roomId: number, playerId: number) {
    const room = this.rooms.get(roomId);
    if (!room) {
      throw new Error(
        "onPlayerRequestStartGame triggered for non-existent room"
      );
    }
    if (
      room.adminState.state !== "claimed" ||
      room.adminState.adminPlayerId !== playerId
    ) {
      this.log(
        `Received start-game from ${playerId}, but that player is not admin!`
      );
      return;
    }
    if (room.mapName === undefined) {
      this.log(`Received start-game from ${playerId}, but the map is not set`);
      return;
    }
    if (
      !room.players.every(x => {
        switch (x.state) {
          case "filled":
            return (
              x.player.ready &&
              room.activeMods.every(m => x.player.installedMods.includes(m))
            );
          case "closed":
            return true;
          case "empty":
            return false;
        }
      })
    ) {
      this.log(
        `Received start-game from ${playerId}, but not all open slots are filled, ready and have the required mods`
      );
      return;
    }

    // The same rule the lobby greys the button with, applied again here
    // because this is the one that decides: a client that skipped the check,
    // or was built before it existed, must not be able to start a game where
    // the players are running different data. Issue #43.
    const agreement = checkArchiveAgreement(
      room.activeMods,
      choose(room.players, x =>
        x.state === "filled"
          ? { playerId: x.player.id, mods: x.player.archives }
          : undefined
      ),
      room.adminState.adminPlayerId
    );
    if (agreement.mismatches.length !== 0) {
      const names = agreement.mismatches
        .map(m => `${m.playerId}: ${m.mods.map(x => x.modName).join(", ")}`)
        .join("; ");
      this.log(
        `Received start-game from ${playerId}, but these players do not have the same archives -- ${names}`
      );
      return;
    }
    if (agreement.uncheckedPlayerIds.length !== 0) {
      this.log(
        `Received start-game from ${playerId}, but these players have not reported their archives yet: ${agreement.uncheckedPlayerIds.join(
          ", "
        )}`
      );
      return;
    }
    const isIpv4Game = choose(room.players, x =>
      x.state === "filled" ? x : undefined
    ).some(x => isIpv4Client(x.player));
    const payload: protocol.StartGamePayload = {
      addresses: choose(room.players, x =>
        x.state === "filled" ? x : undefined
      ).map(x => [
        x.player.id,
        isIpv4Game ? x.player.ipv4Address : x.player.host,
      ]),
    };
    this.sendToRoom(roomId, protocol.StartGame, payload);
  }

  private onDisconnected(roomId: number, playerId: number) {
    const room = this.rooms.get(roomId);
    if (!room) {
      throw new Error("onDisconnected triggered for non-existent room");
    }
    room.players = room.players.map(x => {
      if (x.state === "filled" && x.player.id === playerId) {
        const e: EmptyPlayerSlot = { state: "empty" };
        return e;
      }
      return x;
    });
    const playerLeft: protocol.PlayerLeftPayload = {
      playerId,
    };

    const firstPlayerIndex = room.players.findIndex(x => x.state === "filled");
    if (firstPlayerIndex === -1) {
      this.deleteRoom(roomId);
      return;
    }

    const firstPlayer = room.players[
      firstPlayerIndex
    ] as protocol.FilledPlayerSlot;

    if (
      room.adminState.state === "claimed" &&
      room.adminState.adminPlayerId === playerId
    ) {
      room.adminState.adminPlayerId = firstPlayer.player.id;
      playerLeft.newAdminPlayerId = room.adminState.adminPlayerId;
    }
    this.sendToRoom(roomId, protocol.PlayerLeft, playerLeft);
    this._gameUpdated.next([roomId, room]);
  }
}
