import { startLocalServer } from "./localServer";
import * as http from "http";
import { archiveDigest } from "../common/archives";

function fakeStore(
  initial: any = {
    isRweRunning: false,
    currentScreen: { room: { mapCache: {} } },
  }
) {
  let state = initial;
  const listeners = new Set<() => void>();
  return {
    getState: () => state,
    dispatch: (action: any) => {
      state = { ...state, lastAction: action };
      listeners.forEach(listener => listener());
    },
    subscribe: (listener: () => void) => {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    setState: (value: any) => {
      state = value;
      listeners.forEach(listener => listener());
    },
  };
}

describe("local browser server", () => {
  let server: Awaited<ReturnType<typeof startLocalServer>>;
  const store = fakeStore();
  beforeAll(async () => {
    server = await startLocalServer({
      store,
      staticDir: __dirname,
      operations: { ping: () => "pong", nothing: () => undefined },
    });
  });
  afterAll(async () => server.close());

  it("serves initial state and accepts a validated action", async () => {
    const state = await fetch(new URL("api/state", server.url));
    expect((await state.json()).isRweRunning).toBe(false);
    const action = await fetch(new URL("api/action", server.url), {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        action: { type: "SET_PLAYER_NAME", playerName: "Test" },
      }),
    });
    expect(action.status).toBe(200);
    expect(store.getState().lastAction.type).toBe("SET_PLAYER_NAME");
  });

  it("requires the session path and JSON content type", async () => {
    const withoutToken = await fetch(
      new URL("api/state", server.url).origin + "/api/state"
    );
    expect(withoutToken.status).toBe(404);
    const wrongType = await fetch(new URL("api/action", server.url), {
      method: "POST",
      headers: { "Content-Type": "text/plain" },
      body: "{}",
    });
    expect(wrongType.status).toBe(415);
  });

  it("rejects forged origins and internal actions without killing the service", async () => {
    const forged = await fetch(new URL("api/state", server.url), {
      headers: { Origin: "http://evil.example" },
    });
    expect(forged.status).toBe(403);
    const internal = await fetch(new URL("api/action", server.url), {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ action: { type: "REMOTE_STATE" } }),
    });
    expect(internal.status).toBe(400);
    expect((await fetch(new URL("api/state", server.url))).status).toBe(200);
  });

  it("rejects unregistered RPC names", async () => {
    const response = await fetch(new URL("api/rpc", server.url), {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ method: "constructor" }),
    });
    expect(response.status).toBe(400);
  });

  it("returns JSON null for successful void operations", async () => {
    const response = await fetch(new URL("api/rpc", server.url), {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ method: "nothing" }),
    });
    expect(response.status).toBe(200);
    expect(await response.json()).toBeNull();
  });

  it("computes lobby archive digests with the existing Node implementation", async () => {
    const fingerprints = [
      {
        name: "ta",
        archives: [{ name: "rev31.gp3", size: 1, hash: "012345" }],
      },
    ];
    const digestStore = fakeStore({
      isRweRunning: false,
      currentScreen: { room: { mapCache: {} } },
      currentGame: {
        activeMods: ["ta"],
        players: [
          { state: "filled", player: { id: 1, archives: fingerprints } },
        ],
      },
    });
    const digestServer = await startLocalServer({
      store: digestStore,
      staticDir: __dirname,
      operations: {},
    });
    try {
      const state = await (
        await fetch(new URL("api/state", digestServer.url))
      ).json();
      expect(state.currentGame.players[0].player.archivesDigest).toBe(
        archiveDigest(["ta"], fingerprints)
      );
    } finally {
      await digestServer.close();
    }
  });

  it("rejects forged Host, malformed JSON, oversized bodies and uncached minimap paths", async () => {
    const target = new URL("api/state", server.url);
    const forgedHost = await new Promise<number>((resolve, reject) => {
      const request = http.request(
        {
          hostname: "127.0.0.1",
          port: target.port,
          path: target.pathname,
          headers: { Host: "attacker.example" },
        },
        response => {
          response.resume();
          response.on("end", () => resolve(response.statusCode || 0));
        }
      );
      request.on("error", reject);
      request.end();
    });
    expect(forgedHost).toBe(403);
    const malformed = await fetch(new URL("api/action", server.url), {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: "{",
    });
    expect(malformed.status).toBe(400);
    const oversized = await fetch(new URL("api/action", server.url), {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        action: { type: "SET_PLAYER_NAME", value: "x".repeat(70_000) },
      }),
    });
    expect(oversized.status).toBe(413);
    const minimap = await fetch(
      new URL("api/minimap?path=%2Fetc%2Fpasswd", server.url)
    );
    expect(minimap.status).toBe(404);
  });

  it("sends an SSE snapshot and releases the stream when the client closes", async () => {
    const response = await fetch(new URL("events", server.url));
    expect(response.headers.get("content-type")).toContain("text/event-stream");
    const reader = response.body!.getReader();
    const first = await reader.read();
    expect(new TextDecoder().decode(first.value)).toContain('"isRweRunning"');
    await reader.cancel();
  });

  it("pauses idle shutdown while a game is active and resumes after it ends", async () => {
    const idleStore = fakeStore();
    let closed = false;
    const idleServer = await startLocalServer({
      store: idleStore,
      staticDir: __dirname,
      operations: {},
      idleMs: 40,
      onIdle: () => {
        closed = true;
      },
    });
    idleStore.setState({ isRweRunning: true });
    await new Promise(resolve => setTimeout(resolve, 70));
    expect(closed).toBe(false);
    idleStore.setState({ isRweRunning: false });
    await new Promise(resolve => setTimeout(resolve, 70));
    expect(closed).toBe(true);
    await idleServer.close();
  });
});
