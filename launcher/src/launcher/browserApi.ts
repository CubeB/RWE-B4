export async function invoke<T = unknown>(
  method: string,
  args?: unknown
): Promise<T> {
  const response = await fetch("api/rpc", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ method, args }),
  });
  const value = await response.json();
  if (!response.ok)
    throw new Error(value.error || `Request failed (${response.status})`);
  return value as T;
}

export async function openExternal(url: string): Promise<void> {
  await invoke("open-external", { url });
}

export function minimapUrl(filePath: string): string {
  return `api/minimap?path=${encodeURIComponent(filePath)}`;
}
