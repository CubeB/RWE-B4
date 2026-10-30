import fs from "fs";
import path from "path";
import { normalizePlayerName } from "./playerName";
export { normalizePlayerName } from "./playerName";

export interface LauncherPreferences {
  playerName: string;
  activeMods: string[];
  communityPatch: boolean;
}

export const defaultPreferences: LauncherPreferences = {
  playerName: "Player",
  activeMods: ["ta"],
  communityPatch: false,
};

export function normalizePreferences(value: unknown): LauncherPreferences {
  const data =
    value && typeof value === "object"
      ? (value as Record<string, unknown>)
      : {};
  return {
    playerName:
      typeof data.playerName === "string"
        ? normalizePlayerName(data.playerName) || "Player"
        : "Player",
    activeMods: Array.isArray(data.activeMods)
      ? [
          ...new Set(
            data.activeMods.filter(
              (entry): entry is string =>
                typeof entry === "string" && entry.length > 0
            )
          ),
        ]
      : [...defaultPreferences.activeMods],
    communityPatch: data.communityPatch === true,
  };
}

export function loadPreferences(filename: string): LauncherPreferences {
  try {
    return normalizePreferences(JSON.parse(fs.readFileSync(filename, "utf8")));
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code === "ENOENT") {
      return normalizePreferences(undefined);
    }
    throw error;
  }
}

export function savePreferences(
  filename: string,
  preferences: LauncherPreferences
): void {
  fs.mkdirSync(path.dirname(filename), { recursive: true });
  const temporary = filename + ".tmp";
  fs.writeFileSync(
    temporary,
    JSON.stringify(normalizePreferences(preferences), null, 2) + "\n"
  );
  fs.renameSync(temporary, filename);
}
