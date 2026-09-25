import type { ArchiveInfo, ModFingerprint } from "../common/archives";

/**
 * What a lobby client sends, checked before the server keeps it or passes it
 * on. Issue #75.
 *
 * The server is on the internet and a client is whatever connects to it, so
 * nothing in a payload has the type its TypeScript interface claims. Before
 * these checks, one handshake with a null payload, one `create-game` asking
 * for a billion players, or one `close-slot` naming slot 99 took the whole
 * process down, and with it every lobby and the games list. Values also go
 * further than the server: a name, a side, a colour and an address end up on
 * every other player's engine command line, so they are held to the shapes
 * the engine expects.
 *
 * Every function here returns undefined for anything it will not accept, and
 * the caller ignores the message.
 */

/** Seats in a room; the engine's player table has ten. */
export const MaxPlayers = 10;

export const MaxNameLength = 32;
export const MaxDescriptionLength = 100;
export const MaxMapNameLength = 128;
export const MaxChatLength = 500;
export const MaxReasonLength = 200;
export const MaxMods = 64;
export const MaxArchivesPerMod = 512;
export const MaxArchiveNameLength = 260;

/** C0 and C1 control characters and DEL: nothing a name or a line of chat needs. */
// eslint-disable-next-line no-control-regex
const controlCharacters = /[\u0000-\u001f\u007f-\u009f]/g;

/**
 * A trimmed string with its control characters removed, if it is a string at
 * all and is between 1 and `maxLength` characters once cleaned.
 */
export function lobbyString(x: unknown, maxLength: number): string | undefined {
  if (typeof x !== "string") {
    return undefined;
  }
  const s = x.replace(controlCharacters, "").trim();
  if (s.length === 0 || s.length > maxLength) {
    return undefined;
  }
  return s;
}

/**
 * A player's name. A semicolon is the engine's field separator in `--player`,
 * so it is replaced rather than passed on to split someone else's arguments.
 */
export function playerName(x: unknown): string | undefined {
  const s = lobbyString(x, MaxNameLength);
  return s === undefined ? undefined : s.replace(/;/g, "_");
}

export function intInRange(
  x: unknown,
  min: number,
  max: number
): number | undefined {
  if (typeof x !== "number" || !Number.isInteger(x) || x < min || x > max) {
    return undefined;
  }
  return x;
}

export function side(x: unknown): "ARM" | "CORE" | undefined {
  return x === "ARM" || x === "CORE" ? x : undefined;
}

/** The engine has ten player colours. */
export function color(x: unknown): number | undefined {
  return intInRange(x, 0, MaxPlayers - 1);
}

export function slotId(x: unknown): number | undefined {
  return intInRange(x, 0, MaxPlayers - 1);
}

/** A tick in a game: the engine counts them in a 32-bit unsigned integer. */
export function tick(x: unknown): number | undefined {
  return intInRange(x, 0, 0xffffffff);
}

/**
 * A dotted IPv4 address, which is all the handshake's `ipv4Address` is ever
 * meant to be. It becomes part of other players' `--player` arguments, so
 * anything else is refused rather than passed through.
 */
export function ipv4Address(x: unknown): string | undefined {
  if (typeof x !== "string") {
    return undefined;
  }
  const m = /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.exec(x);
  if (!m) {
    return undefined;
  }
  for (let i = 1; i <= 4; ++i) {
    if (Number(m[i]) > 255) {
      return undefined;
    }
  }
  return x;
}

export function stringList(
  x: unknown,
  maxItems: number,
  maxLength: number
): string[] | undefined {
  if (!Array.isArray(x) || x.length > maxItems) {
    return undefined;
  }
  const out: string[] = [];
  for (const item of x) {
    const s = lobbyString(item, maxLength);
    if (s === undefined) {
      return undefined;
    }
    out.push(s);
  }
  return out;
}

function archiveInfo(x: unknown): ArchiveInfo | undefined {
  if (typeof x !== "object" || x === null) {
    return undefined;
  }
  const o = x as Record<string, unknown>;
  const name = lobbyString(o.name, MaxArchiveNameLength);
  const size = intInRange(o.size, 0, Number.MAX_SAFE_INTEGER);
  const hash =
    typeof o.hash === "string" && /^[0-9a-f]{64}$/.test(o.hash)
      ? o.hash
      : undefined;
  if (name === undefined || size === undefined || hash === undefined) {
    return undefined;
  }
  return { name, size, hash };
}

/** The archive fingerprints a client reports (common/archives.ts). */
export function modFingerprints(x: unknown): ModFingerprint[] | undefined {
  if (!Array.isArray(x) || x.length > MaxMods) {
    return undefined;
  }
  const out: ModFingerprint[] = [];
  for (const item of x) {
    if (typeof item !== "object" || item === null) {
      return undefined;
    }
    const o = item as Record<string, unknown>;
    const name = lobbyString(o.name, MaxMapNameLength);
    if (
      name === undefined ||
      !Array.isArray(o.archives) ||
      o.archives.length > MaxArchivesPerMod
    ) {
      return undefined;
    }
    const archives: ArchiveInfo[] = [];
    for (const a of o.archives) {
      const info = archiveInfo(a);
      if (info === undefined) {
        return undefined;
      }
      archives.push(info);
    }
    out.push({ name, archives });
  }
  return out;
}

/** An object payload, so that reading a field off it cannot throw. */
export function payload(x: unknown): Record<string, unknown> | undefined {
  return typeof x === "object" && x !== null && !Array.isArray(x)
    ? (x as Record<string, unknown>)
    : undefined;
}
