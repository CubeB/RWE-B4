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
    if (new TextEncoder().encode(name + character).length > 20) break;
    name += character;
  }
  return name.trim();
}
