/**
 * Update checking for the launcher (issue: add an updater).
 *
 * The launcher ships inside the engine's release archive, so the version it
 * knows about is the release tag it was built from. The build injects that tag
 * as `__RWE_LAUNCHER_VERSION__` (webpack's DefinePlugin, from `git describe`),
 * and this module compares it with the newest GitHub release.
 *
 * It deliberately does not replace files in place: releases are a Windows
 * installer, a Windows zip and a Linux AppImage, with no signed update feed,
 * so "Download" hands the matching asset to the system and the user runs it.
 */

export interface UpdateAsset {
  name: string;
  url: string;
}

export interface ReleaseInfo {
  tag: string;
  name: string;
  url: string;
  prerelease: boolean;
  publishedAt: string;
  assets: UpdateAsset[];
}

export interface UpdateCheckResult {
  /** What this build was made from, as injected at build time. */
  currentVersion: string;
  /** The newest release found, if the feed gave one. */
  latest?: ReleaseInfo;
  /** Whether `latest` is newer than `currentVersion`. */
  updateAvailable: boolean;
  /** The asset to offer for this platform, when the release has one. */
  asset?: UpdateAsset;
  /** A comparison could not be made (a development build, or a bad tag). */
  currentIsComparable: boolean;
}

/** The build's own version, or a marker for a build that has none injected. */
export function currentLauncherVersion(): string {
  // `typeof` is safe for an identifier DefinePlugin never replaced (Jest).
  return typeof __RWE_LAUNCHER_VERSION__ === "string"
    ? __RWE_LAUNCHER_VERSION__
    : "development";
}

interface ParsedVersion {
  numbers: number[];
  prerelease: string[];
}

/**
 * Parses `v1.1.0-pre6` and the `1.1.0-pre6-3-gabc123-dirty` that `git
 * describe` gives a few commits past a tag. Returns nothing for a bare hash.
 */
export function parseVersion(text: string): ParsedVersion | undefined {
  let value = text.trim().replace(/^v/i, "");
  // git describe's dirty marker, then its commit distance and hash.
  value = value.replace(/-dirty$/i, "").replace(/-\d+-g[0-9a-f]+$/i, "");
  const match = value.match(/^(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?$/);
  if (match === null) {
    return undefined;
  }
  return {
    numbers: [Number(match[1]), Number(match[2]), Number(match[3])],
    prerelease: match[4] === undefined ? [] : match[4].split("."),
  };
}

function comparePrerelease(a: string[], b: string[]): number {
  // No prerelease outranks any prerelease: 1.1.0 > 1.1.0-pre6.
  if (a.length === 0 && b.length === 0) {
    return 0;
  }
  if (a.length === 0) {
    return 1;
  }
  if (b.length === 0) {
    return -1;
  }
  for (let i = 0; i < Math.min(a.length, b.length); ++i) {
    const left = a[i];
    const right = b[i];
    const leftNumber = /^\d+$/.test(left) ? Number(left) : undefined;
    const rightNumber = /^\d+$/.test(right) ? Number(right) : undefined;
    if (leftNumber !== undefined && rightNumber !== undefined) {
      if (leftNumber !== rightNumber) {
        return leftNumber < rightNumber ? -1 : 1;
      }
    } else if (left !== right) {
      return left < right ? -1 : 1;
    }
  }
  if (a.length === b.length) {
    return 0;
  }
  return a.length < b.length ? -1 : 1;
}

/** Whether `latest` is a newer version than `current`; false if either is unparsable. */
export function isNewerVersion(latest: string, current: string): boolean {
  const a = parseVersion(latest);
  const b = parseVersion(current);
  if (a === undefined || b === undefined) {
    return false;
  }
  for (let i = 0; i < 3; ++i) {
    if (a.numbers[i] !== b.numbers[i]) {
      return a.numbers[i] > b.numbers[i];
    }
  }
  return comparePrerelease(a.prerelease, b.prerelease) > 0;
}

/** The asset for this platform, or nothing when the release has none. */
export function chooseUpdateAsset(
  platform: string,
  assets: UpdateAsset[]
): UpdateAsset | undefined {
  if (platform === "win32") {
    // The installer, not the portable zip, and never a Debug build.
    return assets.find(
      a =>
        /\.exe$/i.test(a.name) &&
        /windows/i.test(a.name) &&
        !/debug/i.test(a.name)
    );
  }
  if (platform === "linux") {
    return assets.find(a => /\.appimage$/i.test(a.name));
  }
  return undefined;
}

/** Picks the newest release from a GitHub releases list and compares it. */
export function evaluateUpdates(
  releases: ReleaseInfo[],
  currentVersion: string,
  platform: string
): UpdateCheckResult {
  const candidates = releases
    .filter(
      release =>
        release !== undefined &&
        release !== null &&
        parseVersion(release.tag) !== undefined
    )
    .slice()
    .sort((a, b) => {
      const pa = parseVersion(a.tag);
      const pb = parseVersion(b.tag);
      if (pa !== undefined && pb !== undefined) {
        for (let i = 0; i < 3; ++i) {
          if (pa.numbers[i] !== pb.numbers[i]) {
            return pb.numbers[i] - pa.numbers[i];
          }
        }
        return -comparePrerelease(pa.prerelease, pb.prerelease);
      }
      return 0;
    });

  const latest = candidates[0];
  const currentIsComparable = parseVersion(currentVersion) !== undefined;
  return {
    currentVersion,
    latest,
    updateAvailable:
      latest !== undefined && isNewerVersion(latest.tag, currentVersion),
    asset:
      latest === undefined
        ? undefined
        : chooseUpdateAsset(platform, latest.assets),
    currentIsComparable,
  };
}
