import fs from "fs";
import path from "path";

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

/** Names fit both the online lobby and the native LAN name field. */
export function normalizePlayerName(value: string): string {
  const cleaned = Array.from(value)
    .filter(character => {
      const code = character.codePointAt(0)!;
      return code >= 32 && code !== 127 && character !== ";";
    })
    .join("")
    .trim();
  let name = "";
  for (const character of cleaned) {
    if (Buffer.byteLength(name + character, "utf8") > 20) {
      break;
    }
    name += character;
  }
  return name.trim();
}

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
