import * as fs from "fs";
import * as path from "path";
import * as rx from "rxjs";
import * as rxop from "rxjs/operators";
import { ofType, StateObservable } from "redux-observable";
import { AppAction, receiveModFingerprints } from "../actions";
import {
  HashCache,
  ModFingerprint,
  fingerprintMod,
} from "../../common/archives";
import { getRweUserPath } from "../util";
import { InstalledModInfo, State } from "../state";
import { EpicDependencies } from "./EpicDependencies";

/**
 * Hashes the archives in every installed mod, in the background, so the lobby
 * can tell whether two players are about to play with the same data. Issue #43.
 *
 * In the background because it has to be: a community map pack runs to
 * gigabytes, and the first pass over one takes as long as the disk takes. It
 * starts when the installed mods are known and the result arrives whenever it
 * arrives -- the lobby treats a player whose hashes have not come yet as
 * unchecked rather than as wrong, and will not start a game until they do.
 */

function hashCachePath(): string {
  return path.join(getRweUserPath(), "archive-hashes.json");
}

/**
 * The hashes from last time, which is what makes the second run instant. A
 * cache that cannot be read is simply an empty one: it is a cache.
 */
export function loadHashCache(cachePath: string): HashCache {
  try {
    return JSON.parse(fs.readFileSync(cachePath, "utf8")) as HashCache;
  } catch (err) {
    return {};
  }
}

export function saveHashCache(cachePath: string, cache: HashCache): void {
  try {
    fs.mkdirSync(path.dirname(cachePath), { recursive: true });
    fs.writeFileSync(cachePath, JSON.stringify(cache));
  } catch (err) {
    console.error(`Failed to write the archive hash cache: ${err}`);
  }
}

/**
 * Hashes each mod in turn rather than all at once. They are all on the same
 * disk, so running them together would finish no sooner and would compete for
 * it with the game the player may already have started.
 */
export async function fingerprintMods(
  mods: InstalledModInfo[],
  cachePath: string
): Promise<ModFingerprint[]> {
  const cache = loadHashCache(cachePath);
  const fingerprints: ModFingerprint[] = [];

  for (const mod of mods) {
    try {
      fingerprints.push(await fingerprintMod(mod.name, mod.path, cache));
    } catch (err) {
      // A mod that cannot be read is left out rather than allowed to stop the
      // rest: the lobby will call it unchecked and refuse to start, which is
      // the right answer for data nobody can account for.
      console.error(`Failed to hash the archives in ${mod.name}: ${err}`);
    }
  }

  saveHashCache(cachePath, cache);
  return fingerprints;
}

export const archiveFingerprintsEpic = (
  action$: rx.Observable<AppAction>
): rx.Observable<AppAction> => {
  return action$.pipe(
    ofType<
      AppAction,
      { type: "RECEIVE_INSTALLED_MODS"; mods: InstalledModInfo[] }
    >("RECEIVE_INSTALLED_MODS"),
    rxop.switchMap(action =>
      rx.from(fingerprintMods(action.mods, hashCachePath()))
    ),
    rxop.map(receiveModFingerprints)
  );
};

/**
 * Tells the room what this player's archives are, as soon as both facts exist:
 * the hashing has finished and we are in a room. Either can come first -- a
 * player can join while a 2 GB pack is still being read, and can sit in a room
 * long enough for a rescan to land -- so both are watched.
 */
export const sendArchivesEpic = (
  action$: rx.Observable<AppAction>,
  state$: StateObservable<State>,
  { clientService }: EpicDependencies
): rx.Observable<AppAction> => {
  return action$.pipe(
    ofType<AppAction, AppAction>(
      "RECEIVE_HANDSHAKE_RESPONSE",
      "RECEIVE_MOD_FINGERPRINTS"
    ),
    rxop.mergeMap(() => {
      const state = state$.value;
      if (
        state.currentGame === undefined ||
        state.modFingerprints === undefined
      ) {
        return rx.empty();
      }
      clientService.setArchives(state.modFingerprints);
      return rx.empty();
    })
  );
};
