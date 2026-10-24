import * as React from "react";
import { renderToStaticMarkup } from "react-dom/server";
import { LauncherHome, LauncherHomeProps } from "./LauncherHome";

jest.mock("electron", () => ({
  ipcRenderer: { invoke: jest.fn() },
  shell: { openPath: jest.fn() },
}));

function home(overrides: Partial<LauncherHomeProps> = {}) {
  return renderToStaticMarkup(
    <LauncherHome
      playerName="Oscar"
      activeMods={["ta"]}
      installedMods={["ta"]}
      launchEnabled
      running={false}
      onLaunch={jest.fn()}
      onSaveName={jest.fn()}
      onManageMods={jest.fn()}
      onSettings={jest.fn()}
      onRefreshMods={jest.fn()}
      {...overrides}
    />
  );
}

it("allows offline play with imported data and the saved profile", () => {
  const markup = home();
  const play = markup
    .match(/<button\b[^>]*>[\s\S]*?<\/button>/g)!
    .find(button => button.includes("Play RWE"))!;
  expect(play).toBeDefined();
  expect(play).not.toContain("disabled");
  expect(markup).toContain('value="Oscar"');
  expect(markup).toContain("Total Annihilation");
});

it("prevents launching a second game and retains the import controls", () => {
  const markup = home({ running: true, launchEnabled: false });
  const play = markup
    .match(/<button\b[^>]*>[\s\S]*?<\/button>/g)!
    .find(button => button.includes("Game running"))!;
  expect(play).toContain("disabled");
  expect(markup).toContain("Import game data");
});
