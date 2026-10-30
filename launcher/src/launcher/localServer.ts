import * as http from "http";
import * as fs from "fs";
import * as path from "path";
import { randomBytes } from "crypto";
import { archiveDigest } from "../common/archives";

export interface LocalServerOptions {
  store: {
    getState(): any;
    dispatch(action: any): any;
    subscribe(callback: () => void): () => void;
  };
  staticDir: string;
  operations: Record<string, (args?: any) => unknown | Promise<unknown>>;
  onIdle?: () => void;
  idleMs?: number;
}

// DOWNLOAD-001 / ADR-0007: expose the existing launcher through a loopback
// browser session. This distribution boundary is RWE-original behavior.
export async function startLocalServer(options: LocalServerOptions) {
  const token = randomBytes(32).toString("hex");
  const prefix = `/${token}/`;
  const clients = new Set<http.ServerResponse>();
  const snapshot = () => {
    const state = options.store.getState();
    const game = state.currentGame;
    if (!game) return state;
    return {
      ...state,
      currentGame: {
        ...game,
        players: game.players.map((slot: any) =>
          slot.state === "filled"
            ? {
                ...slot,
                player: {
                  ...slot.player,
                  archivesDigest:
                    slot.player.archives === undefined
                      ? undefined
                      : archiveDigest(game.activeMods, slot.player.archives),
                },
              }
            : slot
        ),
      },
    };
  };
  const staticFiles = new Set([
    "index.html",
    "renderer.js",
    "b4-icon.png",
    ...fs
      .readdirSync(options.staticDir)
      .filter(
        name =>
          /\.(?:ttf|woff2?)$/i.test(name) &&
          fs.statSync(path.join(options.staticDir, name)).isFile() &&
          path.basename(name) === name
      ),
  ]);
  let idleTimer: ReturnType<typeof setTimeout> | undefined;
  const stopIdle = () => {
    if (idleTimer) clearTimeout(idleTimer);
    idleTimer = undefined;
  };
  const checkIdle = () => {
    if (clients.size || options.store.getState().isRweRunning) {
      stopIdle();
      return;
    }
    if (!idleTimer && options.onIdle)
      idleTimer = setTimeout(options.onIdle, options.idleMs ?? 60_000);
  };
  const unsubscribeIdle = options.store.subscribe(checkIdle);
  const server = http.createServer(async (req, res) => {
    try {
      const port = (server.address() as any)?.port;
      const authority = `127.0.0.1:${port}`;
      const fail = (code: number, error: string) => {
        res.writeHead(code, {
          "Content-Type": "application/json",
          "Cache-Control": "no-store",
          "Referrer-Policy": "no-referrer",
        });
        res.end(JSON.stringify({ error }));
      };
      res.setHeader("Cache-Control", "no-store");
      res.setHeader("Referrer-Policy", "no-referrer");
      if (req.headers.host !== authority) return fail(403, "Invalid host");
      if (req.headers.origin && req.headers.origin !== `http://${authority}`)
        return fail(403, "Invalid origin");
      let url: URL;
      try {
        url = new URL(req.url || "/", `http://${authority}`);
      } catch {
        return fail(400, "Invalid URL");
      }
      if (!url.pathname.startsWith(prefix)) return fail(404, "Not found");
      const route = url.pathname.slice(prefix.length);
      if (route === "api/state" && req.method === "GET") {
        res.setHeader("Content-Type", "application/json");
        return res.end(JSON.stringify(snapshot()));
      }
      if (route === "events" && req.method === "GET") {
        res.writeHead(200, {
          "Content-Type": "text/event-stream",
          Connection: "keep-alive",
        });
        const send = () => {
          if (!res.destroyed)
            res.write(`data: ${JSON.stringify(snapshot())}\n\n`);
        };
        clients.add(res);
        stopIdle();
        send();
        const unsubscribe = options.store.subscribe(send);
        const heartbeat = setInterval(() => {
          if (!res.destroyed) res.write(": keepalive\n\n");
        }, 15_000);
        req.on("close", () => {
          clearInterval(heartbeat);
          unsubscribe();
          clients.delete(res);
          checkIdle();
        });
        return;
      }
      if (
        (route === "api/action" || route === "api/rpc") &&
        req.method === "POST"
      ) {
        if (
          !req.headers["content-type"]
            ?.toLowerCase()
            .startsWith("application/json")
        )
          return fail(415, "Expected application/json");
        const chunks: Buffer[] = [];
        let size = 0;
        try {
          for await (const chunk of req) {
            size += chunk.length;
            if (size > 65536) return fail(413, "Request too large");
            chunks.push(Buffer.from(chunk));
          }
          const body = JSON.parse(Buffer.concat(chunks).toString("utf8"));
          if (route === "api/action") {
            const action = body?.action;
            if (
              !action ||
              typeof action !== "object" ||
              Array.isArray(action) ||
              typeof action.type !== "string" ||
              action.type.length > 128 ||
              action.type.startsWith("@@") ||
              action.type === "REMOTE_STATE" ||
              /^(RECEIVE_|MASTER_SERVER_|GAME_ENDED|LAUNCH_RWE_END)/.test(
                action.type
              )
            )
              return fail(400, "Invalid action");
            options.store.dispatch(action);
            res.setHeader("Content-Type", "application/json");
            return res.end("{}");
          }
          const operation =
            typeof body?.method === "string" &&
            Object.prototype.hasOwnProperty.call(
              options.operations,
              body.method
            )
              ? options.operations[body.method]
              : undefined;
          if (!operation) return fail(400, "Unknown operation");
          const result = await operation(body.args);
          res.setHeader("Content-Type", "application/json");
          return res.end(JSON.stringify(result === undefined ? null : result));
        } catch (error) {
          return fail(400, String(error));
        }
      }
      if (route === "api/minimap" && req.method === "GET") {
        const requested = url.searchParams.get("path");
        const cache =
          options.store.getState().currentScreen?.room?.mapCache || {};
        if (
          !requested ||
          !Object.values(cache).some(
            (entry: any) => entry.minimap === requested
          )
        )
          return fail(404, "Minimap not found");
        try {
          const data = fs.readFileSync(requested);
          res.setHeader("Content-Type", "image/png");
          return res.end(data);
        } catch {
          return fail(404, "Minimap not found");
        }
      }
      if (req.method === "GET") {
        const filename = route || "index.html";
        if (!staticFiles.has(filename)) return fail(404, "Not found");
        try {
          const body = fs.readFileSync(path.join(options.staticDir, filename));
          res.setHeader(
            "Content-Type",
            filename.endsWith(".html")
              ? "text/html"
              : filename.endsWith(".js")
                ? "text/javascript"
                : filename.endsWith(".png")
                  ? "image/png"
                  : "font/ttf"
          );
          res.setHeader(
            "Content-Security-Policy",
            "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; font-src 'self'; frame-ancestors 'none'"
          );
          return res.end(body);
        } catch {
          return fail(404, "Not found");
        }
      }
      return fail(404, "Not found");
    } catch {
      res.statusCode = 500;
      res.end(JSON.stringify({ error: "Internal server error" }));
      return;
    }
  });
  await new Promise<void>((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", resolve);
  });
  const port = (server.address() as any).port;
  const url = `http://127.0.0.1:${port}${prefix}`;
  checkIdle();
  return {
    url,
    close: () =>
      new Promise<void>(resolve => {
        stopIdle();
        unsubscribeIdle();
        for (const client of clients) client.end();
        clients.clear();
        server.close(() => resolve());
      }),
  };
}
