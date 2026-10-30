export const browserApi = {
  postAction(action: unknown): Promise<void> {
    return fetch("api/action", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ action }),
    }).then(async response => {
      if (!response.ok)
        throw new Error(
          (await response.json()).error || `Action failed (${response.status})`
        );
    });
  },
};
