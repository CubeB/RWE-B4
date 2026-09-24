import { spawn } from "child_process";
import * as path from "path";
import * as readline from "readline";
import { Observable, Subject } from "rxjs";
import { assertNever } from "../common/util";

export interface RweArgsPlayerHuman {
  type: "human";
}

export interface RweArgsPlayerComputer {
  type: "computer";
}

export interface RweArgsPlayerRemote {
  type: "remote";
  host: string;
  port: number;
}

export type RweArgsPlayerController =
  RweArgsPlayerHuman | RweArgsPlayerComputer | RweArgsPlayerRemote;

export interface RweArgsPlayerInfo {
  name: string;
  side: "ARM" | "CORE";
  color: number;
  controller: RweArgsPlayerController;
}

export interface RweArgsEmptyPlayerSlot {
  state: "empty";
}

export interface RweArgsFilledPlayerSlot extends RweArgsPlayerInfo {
  state: "filled";
}

export type RweArgsPlayerSlot =
  RweArgsEmptyPlayerSlot | RweArgsFilledPlayerSlot;

export interface RweArgs {
  dataPaths?: string[];
  map?: string;
  interface?: string;
  port?: number;
  players?: RweArgsPlayerSlot[];

  /**
   * Speak the bridge protocol on the game's own stdin and stdout: what it has
   * to say about the game while it runs, and what we have to ask of it.
   * See ControlChannel in the engine.
   */
  bridge?: boolean;

  /**
   * Record every command to a replay file of this name.
   *
   * Not only for watching afterwards: the recording is what a peer hands over
   * when somebody asks to rejoin, nothing else keeping the commands, so a
   * network game that may be rejoined is a network game that records.
   */
  recordReplay?: string;

  /**
   * Record the game as a TA Demo Recorder compatible `.tad` at this path.
   *
   * Not set for lobby games the way recordReplay is: a replay is what makes
   * a game rejoitable, and a demo is state and effects for the conformance
   * tools instead, so it stays off unless a caller asks for it. The path is
   * passed through exactly as given.
   */
  recordDemo?: string;

  /** A recording of the game so far, for joining one already in progress. */
  rejoinFile?: string;

  /** The tick that recording ends at, and this peer resumes at. */
  rejoinTick?: number;
}

/** Something the running game has said. Undefined fields are simply absent. */
export interface RweEvent {
  event: string;
  player?: number;
  tick?: number;
  file?: string;
  reason?: string;
}

/** A game that is running, for as long as it is. */
export interface RunningRwe {
  /** What it has said, as it says it. Completes when the game exits. */
  readonly events: Observable<RweEvent>;

  /** Resolves when the game exits, and rejects if it exits badly. */
  readonly finished: Promise<void>;

  /** Asks the game to let a dropped player back in, by engine slot. */
  requestRejoin(playerSlot: number): void;
}

function serializeHost(host: string): string {
  const ipv4Match = host.match(/^(?:::ffff:)?(\d+)\.(\d+)\.(\d+)\.(\d+)$/);
  if (ipv4Match) {
    return `::ffff:${ipv4Match[1]}.${ipv4Match[2]}.${ipv4Match[3]}.${ipv4Match[4]}`;
  }
  return host;
}

function serializeHostAndPort(host: string, port: number): string {
  return `[${serializeHost(host)}]:${port.toString()}`;
}

function serializeRweController(controller: RweArgsPlayerController): string {
  switch (controller.type) {
    case "human":
      return "Human";
    case "computer":
      return "Computer";
    case "remote":
      return `Network,${serializeHostAndPort(
        controller.host,
        controller.port
      )}`;
    default:
      throw new Error("unknown controller type");
  }
}

function serializeRweArgs(args: RweArgs): string[] {
  const out = [];
  if (args.dataPaths) {
    for (const path of args.dataPaths) {
      out.push("--data-path", path);
    }
  }
  if (args.map !== undefined) {
    out.push("--map", args.map);
  }
  if (args.interface !== undefined) {
    out.push("--interface", args.interface);
  }
  if (args.port !== undefined) {
    out.push("--port", args.port.toString());
  }
  if (args.bridge) {
    out.push("--bridge");
  }
  if (args.recordReplay !== undefined) {
    out.push("--record-replay", args.recordReplay);
  }
  if (args.recordDemo !== undefined) {
    out.push("--record-demo", args.recordDemo);
  }
  if (args.rejoinFile !== undefined && args.rejoinTick !== undefined) {
    out.push("--rejoin", args.rejoinFile);
    out.push("--rejoin-tick", args.rejoinTick.toString());
  }
  if (args.players) {
    for (const p of args.players) {
      switch (p.state) {
        case "filled": {
          const controllerString = serializeRweController(p.controller);
          out.push(
            "--player",
            `${p.name.replace(";", "_")};${controllerString};${p.side};${
              p.color
            }`
          );
          break;
        }
        case "empty": {
          out.push("--player", "empty");
          break;
        }
        default:
          assertNever(p);
      }
    }
  }
  return out;
}

// Naively quotes args, as if you were going to pass them through a shell,
// for display purposes.
// Don't use this for actual shell escaping, it's probably really insecure.
function quoteArg(arg: string) {
  if (arg.match(/[ "'\\]/)) {
    const escapedArg = arg.replace(/(["\\])/g, "\\$1");
    return `"${escapedArg}"`;
  }
  return arg;
}

export function execRwe(args?: RweArgs): RunningRwe {
  const rweHome = process.env["RWE_HOME"];
  const events = new Subject<RweEvent>();
  if (!rweHome) {
    events.complete();
    return {
      events,
      finished: Promise.reject("Cannot launch RWE, RWE_HOME is not defined"),
      requestRejoin: () => undefined,
    };
  }

  const serializedArgs = args ? serializeRweArgs(args) : [];
  console.log(
    "Launching RWE with args: " + serializedArgs.map(quoteArg).join(" ")
  );

  // FIXME: assumes windows
  const proc = spawn(
    path.join(rweHome, "rwe" + (process.platform === "win32" ? ".exe" : "")),
    serializedArgs,
    { cwd: rweHome, stdio: ["pipe", "pipe", "inherit"] }
  );

  if (proc.stdout) {
    // One JSON object a line, and anything else ignored: the game's own log
    // goes to a file, but nothing says a library it links will stay quiet.
    const lines = readline.createInterface({ input: proc.stdout });
    lines.on("line", line => {
      const trimmed = line.trim();
      if (!trimmed.startsWith("{")) {
        return;
      }
      try {
        const parsed = JSON.parse(trimmed);
        if (typeof parsed.event === "string") {
          events.next(parsed as RweEvent);
        }
      } catch {
        console.log("Ignoring a line from RWE that is not JSON: " + trimmed);
      }
    });
  }

  const finished = new Promise<void>((resolve, reject) => {
    proc.on("error", error => {
      events.complete();
      reject(`RWE could not be started: ${error.message}`);
    });
    proc.on("close", code => {
      events.complete();
      if (code !== 0 && code !== null) {
        reject(`RWE exited with exit code ${code}`);
        return;
      }
      resolve();
    });
  });

  return {
    events,
    finished,
    requestRejoin: (playerSlot: number) => {
      if (!proc.stdin || proc.stdin.destroyed) {
        return;
      }
      proc.stdin.write(
        JSON.stringify({ command: "rejoin", player: playerSlot }) + "\n"
      );
    },
  };
}
