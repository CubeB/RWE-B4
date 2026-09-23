import * as crypto from "crypto";
import * as rx from "rxjs";
import type { Namespace, Socket } from "socket.io";
import { ModFingerprint, checkArchiveAgreement } from "../common/archives";
import { assertNever, choose, findAndMap, getAddr } from "../common/util";
import * as protocol from "./protocol";

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
}

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

  createRoom(description: string, maxPlayers: number): GameCreatedInfo {
    const id = this.nextRoomId++;
    const adminKey = generateAdminKey();
    const players: PlayerSlot[] = new Array(10);
    for (let i = 0; i < maxPlayers; ++i) {
      players[i] = { state: "empty" };
    }
    for (let i = maxPlayers; i < 10; ++i) {
      players[i] = { state: "closed" };
    }
    this.rooms.set(id, {
      description,
      nextPlayerId: 1,
      players,
      adminState: { state: "unclaimed", adminKey },
      activeMods: [],
      droppedPlayerIds: [],
    });

    return { gameId: id, adminKey };
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
      socket.on(protocol.Handshake, (data: protocol.HandshakePayload) => {
        this.log(`Received handshake from ${address} with name "${data.name}"`);

        const roomId = data.gameId;
        const room = this.rooms.get(roomId);
        if (!room) {
          this.log(
            `Received handshake to connect to room ${data.gameId}, but room does not exist`
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
            name: data.name,
            host: address,
            ipv4Address: data.ipv4Address,
            side: "ARM",
            color: 0,
            team: 0,
            ready: false,
            installedMods: data.installedMods,
            archives: undefined,
          },
        };

        if (room.adminState.state === "unclaimed") {
          if (data.adminKey === room.adminState.adminKey) {
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
          name: data.name,
          installedMods: data.installedMods,
        };

        this.sendToRoom(roomId, protocol.PlayerJoined, playerJoined);

        socket.join(this.getRoomString(roomId));
        socket.join(this.getPlayerString(roomId, playerId));

        socket.on(protocol.ChatMessage, (data: protocol.ChatMessagePayload) => {
          this.onChatMessage(roomId, playerId, data);
        });
        socket.on(protocol.ChangeSide, (data: protocol.ChangeSidePayload) => {
          this.onChangeSide(roomId, playerId, data);
        });
        socket.on(protocol.ChangeTeam, (data: protocol.ChangeTeamPayload) => {
          this.onChangeTeam(roomId, playerId, data);
        });
        socket.on(protocol.ChangeColor, (data: protocol.ChangeColorPayload) => {
          this.onChangeColor(roomId, playerId, data);
        });
        socket.on(protocol.Ready, (data: protocol.ReadyPayload) => {
          this.onPlayerReady(roomId, playerId, data);
        });
        socket.on(protocol.SetArchives, (data: protocol.SetArchivesPayload) => {
          this.onSetArchives(roomId, playerId, data);
        });
        socket.on(protocol.OpenSlot, (data: protocol.OpenSlotPayload) => {
          this.onOpenSlot(roomId, playerId, data);
        });
        socket.on(protocol.CloseSlot, (data: protocol.CloseSlotPayload) => {
          this.onCloseSlot(roomId, playerId, data);
        });
        socket.on(
          protocol.SetActiveMods,
          (data: protocol.SetActiveModsPayload) => {
            this.onSetActiveMods(roomId, playerId, data);
          }
        );
        socket.on(protocol.ChangeMap, (data: protocol.ChangeMapPayload) => {
          this.onChangeMap(roomId, playerId, data);
        });
        socket.on(protocol.RequestStartGame, () => {
          this.onPlayerRequestStartGame(roomId, playerId);
        });
        socket.on(
          protocol.PlayerDroppedFromGame,
          (data: protocol.PlayerDroppedFromGamePayload) => {
            this.onPlayerDroppedFromGame(roomId, data);
          }
        );
        socket.on(protocol.RequestRejoin, () => {
          this.onRequestRejoin(roomId, playerId);
        });
        socket.on(
          protocol.RejoinBundle,
          (data: protocol.RejoinBundlePayload) => {
            this.onRejoinBundle(roomId, playerId, data);
          }
        );
        socket.on(
          protocol.RejoinRefused,
          (data: protocol.RejoinRefusedPayload) => {
            this.onRejoinRefused(roomId, playerId, data);
          }
        );
        socket.on("disconnect", () => {
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
    data: protocol.PlayerDroppedFromGamePayload
  ) {
    const room = this.rooms.get(roomId);
    if (!room) {
      return;
    }

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

  private onRejoinBundle(
    roomId: number,
    playerId: number,
    data: protocol.RejoinBundlePayload
  ) {
    const room = this.rooms.get(roomId);
    if (!room) {
      return;
    }

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

  private onRejoinRefused(
    roomId: number,
    playerId: number,
    data: protocol.RejoinRefusedPayload
  ) {
    const room = this.rooms.get(roomId);
    if (!room) {
      return;
    }

    if (rejoinHostFor(room, data.playerId) !== playerId) {
      return;
    }

    this.sendToPlayer(roomId, data.playerId, protocol.RejoinRefused, data);
  }

  private onChatMessage(roomId: number, playerId: number, message: string) {
    const payload: protocol.PlayerChatMessagePayload = { playerId, message };
    this.sendToRoom(roomId, protocol.PlayerChatMessage, payload);
  }

  private onChangeSide(
    roomId: number,
    playerId: number,
    data: protocol.ChangeSidePayload
  ) {
    const room = this.rooms.get(roomId);
    if (!room) {
      throw new Error("onChangeSide triggered for non-existent room");
    }
    const player = findPlayer(room.players, playerId);
    if (!player) {
      throw new Error(`Failed to find player ${playerId}`);
    }
    player.side = data.side;
    const payload: protocol.PlayerChangedSidePayload = {
      playerId,
      side: data.side,
    };
    this.sendToRoom(roomId, protocol.PlayerChangedSide, payload);
  }

  private onChangeTeam(
    roomId: number,
    playerId: number,
    data: protocol.ChangeTeamPayload
  ) {
    const room = this.rooms.get(roomId);
    if (!room) {
      throw new Error("onChangeTeam triggered for non-existent room");
    }
    const player = findPlayer(room.players, playerId);
    if (!player) {
      throw new Error(`Failed to find player ${playerId}`);
    }
    player.team = data.team;
    const payload: protocol.PlayerChangedTeamPayload = {
      playerId,
      team: data.team,
    };
    this.sendToRoom(roomId, protocol.PlayerChangedTeam, payload);
  }

  private onChangeColor(
    roomId: number,
    playerId: number,
    data: protocol.ChangeColorPayload
  ) {
    const room = this.rooms.get(roomId);
    if (!room) {
      throw new Error("onChangeSide triggered for non-existent room");
    }
    const player = findPlayer(room.players, playerId);
    if (!player) {
      throw new Error(`Failed to find player ${playerId}`);
    }
    player.color = data.color;
    const payload: protocol.PlayerChangedColorPayload = {
      playerId,
      color: data.color,
    };
    this.sendToRoom(roomId, protocol.PlayerChangedColor, payload);
  }

  private onOpenSlot(
    roomId: number,
    playerId: number,
    data: protocol.OpenSlotPayload
  ) {
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
    const state = room.players[data.slotId].state;
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

  private onCloseSlot(
    roomId: number,
    playerId: number,
    data: protocol.CloseSlotPayload
  ) {
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
    const state = room.players[data.slotId].state;
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

  onSetActiveMods(
    roomId: number,
    playerId: number,
    data: protocol.SetActiveModsPayload
  ) {
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
    const payload: protocol.ActiveModsChangedPayload = { mods: data.mods };
    this.sendToRoom(roomId, protocol.ActiveModsChanged, payload);
    room.activeMods = data.mods;
  }

  private onChangeMap(
    roomId: number,
    playerId: number,
    data: protocol.ChangeMapPayload
  ) {
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
    room.mapName = data.mapName;
    const payload: protocol.MapChangedPayload = { mapName: data.mapName };
    this.sendToRoom(roomId, protocol.MapChanged, payload);
  }

  private onPlayerReady(roomId: number, playerId: number, value: boolean) {
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

  private onSetArchives(
    roomId: number,
    playerId: number,
    data: protocol.SetArchivesPayload
  ) {
    const room = this.rooms.get(roomId);
    if (!room) {
      throw new Error("onSetArchives triggered for non-existent room");
    }
    const player = findPlayer(room.players, playerId);
    if (!player) {
      throw new Error(`Failed to find player ${playerId}`);
    }
    player.archives = data.mods;
    const payload: protocol.PlayerArchivesChangedPayload = {
      playerId,
      mods: data.mods,
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
