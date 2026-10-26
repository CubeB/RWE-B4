import type { Namespace, Socket } from "socket.io";
import { GameServer } from "./game-server";
import * as protocol from "./protocol";

/**
 * A namespace and sockets that record what the server registers and emits,
 * so the real handlers can be driven without a network. Issue #75.
 */
class FakeSocket {
  handlers = new Map<string, (...args: unknown[]) => void>();
  emitted: [string, unknown][] = [];
  disconnected = false;
  handshake = { address: "203.0.113.9", headers: {} };
  on(event: string, handler: (...args: unknown[]) => void) {
    this.handlers.set(event, handler);
  }
  emit(event: string, payload: unknown) {
    this.emitted.push([event, payload]);
  }
  join() {}
  disconnect() {
    this.disconnected = true;
  }
  fire(event: string, ...args: unknown[]) {
    const h = this.handlers.get(event);
    if (!h) {
      throw new Error(`no handler for ${event}`);
    }
    h(...args);
  }
}

function makeServer() {
  let onConnection: ((s: Socket) => void) | undefined;
  const emitted: { room: string; event: string; payload: unknown }[] = [];
  const ns = {
    on: (_: string, f: (s: Socket) => void) => {
      onConnection = f;
    },
    to: (room: string) => ({
      emit: (event: string, payload: unknown) => {
        emitted.push({ room, event, payload });
      },
    }),
  } as unknown as Namespace;
  const server = new GameServer(ns, false);
  const connect = () => {
    const s = new FakeSocket();
    onConnection!(s as unknown as Socket);
    return s;
  };
  return { server, connect, emitted };
}

function handshake(
  gameId: number,
  adminKey?: string,
  name = "someone",
  installedMods: string[] = []
) {
  return {
    gameId,
    name,
    ipv4Address: "203.0.113.9",
    installedMods,
    adminKey,
  };
}

function archives(hash: string) {
  return [
    {
      name: "ta",
      archives: [{ name: "totala1.hpi", size: 1, hash }],
    },
  ];
}

describe("GameServer against payloads a client should never send", () => {
  it("refuses a room with a billion seats rather than allocating them", () => {
    const { server } = makeServer();
    expect(server.createRoom("x", 1e9)).toBeUndefined();
    expect(server.createRoom("x", 0)).toBeUndefined();
    expect(server.createRoom(null, 4)).toBeUndefined();
    const room = server.createRoom("x", 4)!;
    const info = server.getRoomInfo(room.gameId)!;
    expect(info.players.map(p => p.state)).toEqual([
      "empty",
      "empty",
      "empty",
      "empty",
      "closed",
      "closed",
      "closed",
      "closed",
      "closed",
      "closed",
    ]);
  });

  it("survives a null handshake and disconnects it", () => {
    const { connect } = makeServer();
    const s = connect();
    expect(() => s.fire(protocol.Handshake, null)).not.toThrow();
    expect(s.disconnected).toBe(true);
  });

  it("survives an admin closing a slot that does not exist", () => {
    const { server, connect } = makeServer();
    const room = server.createRoom("x", 4)!;
    const s = connect();
    s.fire(protocol.Handshake, handshake(room.gameId, room.adminKey));
    expect(s.disconnected).toBe(false);
    expect(() => s.fire(protocol.CloseSlot, { slotId: 99 })).not.toThrow();
    expect(() => s.fire(protocol.OpenSlot, { slotId: -1 })).not.toThrow();
    expect(() => s.fire(protocol.CloseSlot, null)).not.toThrow();
    expect(server.getRoomInfo(room.gameId)!.players).toHaveLength(10);
  });

  it("keeps what it stores to the shapes the engine will be handed", () => {
    const { server, connect } = makeServer();
    const room = server.createRoom("x", 4)!;
    const s = connect();
    s.fire(protocol.Handshake, handshake(room.gameId, room.adminKey));
    s.fire(protocol.ChangeSide, { side: "ARM;Human;CORE;0" });
    s.fire(protocol.ChangeColor, { color: 1e6 });
    s.fire(protocol.ChangeMap, { mapName: "Coast To Coast" });
    s.fire(protocol.ChangeMap, { mapName: 7 });
    s.fire(protocol.SetActiveMods, { mods: "all of them" });
    const info = server.getRoomInfo(room.gameId)!;
    const player = info.players.find(p => p.state === "filled");
    expect(player?.state === "filled" && player.player.side).toBe("ARM");
    expect(player?.state === "filled" && player.player.color).toBe(0);
    expect(info.mapName).toBe("Coast To Coast");
    expect(info.activeMods).toEqual([]);
  });

  it("tells the room which player and archive differ when a start is refused", () => {
    const { server, connect, emitted } = makeServer();
    const room = server.createRoom("x", 2)!;
    const host = connect();
    const guest = connect();
    host.fire(protocol.Handshake, handshake(room.gameId, room.adminKey, "host", ["ta"]));
    guest.fire(protocol.Handshake, handshake(room.gameId, undefined, "guest", ["ta"]));

    host.fire(protocol.ChangeMap, { mapName: "Coast To Coast" });
    host.fire(protocol.SetActiveMods, { mods: ["ta"] });
    host.fire(protocol.SetArchives, { mods: archives("a".repeat(64)) });
    guest.fire(protocol.SetArchives, { mods: archives("b".repeat(64)) });
    host.fire(protocol.Ready, true);
    guest.fire(protocol.Ready, true);
    host.fire(protocol.RequestStartGame);

    const refusal = emitted.find(x => x.event === protocol.StartRefused);
    expect(refusal).toBeDefined();
    expect(refusal!.payload).toEqual({
      message:
        "Cannot start: the players do not have the same game data (guest has ta/totala1.hpi). Both players must use the same mods, maps and archives.",
    });
  });

  it("will not record a drop for a player it never seated, or the reporter itself", () => {
    const { server, connect } = makeServer();
    const room = server.createRoom("x", 4)!;
    const s = connect();
    s.fire(protocol.Handshake, handshake(room.gameId, room.adminKey));
    s.fire(protocol.PlayerDroppedFromGame, { playerId: 9999, tick: 10 });
    s.fire(protocol.PlayerDroppedFromGame, { playerId: 1, tick: 10 });
    s.fire(protocol.PlayerDroppedFromGame, { playerId: "1", tick: 10 });
    expect(server.getRoomInfo(room.gameId)!.droppedPlayerIds).toEqual([]);
  });
});
