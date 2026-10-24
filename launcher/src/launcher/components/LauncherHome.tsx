import * as React from "react";
import { Alert, Button, Chip, TextField } from "@mui/material";
import PlayArrowRounded from "@mui/icons-material/PlayArrowRounded";
import FolderOpenRounded from "@mui/icons-material/FolderOpenRounded";
import ExtensionRounded from "@mui/icons-material/ExtensionRounded";
import { ipcRenderer, shell } from "electron";
import fs from "fs";
import { getRweModsPath } from "../util";
import { normalizePlayerName } from "../preferences";

export interface LauncherHomeProps {
  playerName: string;
  activeMods: string[];
  installedMods: string[];
  launchEnabled: boolean;
  running: boolean;
  onLaunch: () => void;
  onSaveName: (name: string) => void;
  onManageMods: () => void;
  onSettings: () => void;
  onRefreshMods: () => void;
}

export function LauncherHome(props: LauncherHomeProps) {
  const [name, setName] = React.useState(props.playerName);
  const [importing, setImporting] = React.useState(false);
  const [notice, setNotice] = React.useState<string>();
  const [error, setError] = React.useState<string>();
  React.useEffect(() => setName(props.playerName), [props.playerName]);
  const validName = normalizePlayerName(name);
  const importData = async () => {
    setImporting(true);
    setError(undefined);
    try {
      if (await ipcRenderer.invoke("rwe:import-data")) {
        props.onRefreshMods();
        setNotice("Game data imported. Ready to play.");
      }
    } catch (failure) {
      setError(String(failure));
    } finally {
      setImporting(false);
    }
  };
  const openMods = async () => {
    try {
      const directory = getRweModsPath();
      fs.mkdirSync(directory, { recursive: true });
      const failure = await shell.openPath(directory);
      if (failure) {
        setError(failure);
      }
    } catch (failure) {
      setError(String(failure));
    }
  };
  return (
    <main className="launcher-home">
      {error && (
        <Alert severity="error" onClose={() => setError(undefined)}>
          {error}
        </Alert>
      )}
      {notice && (
        <Alert severity="success" onClose={() => setNotice(undefined)}>
          {notice}
        </Alert>
      )}
      <section className="launch-hero">
        <div>
          <span className="launcher-eyebrow">ROBOT WAR ENGINE</span>
          <h1>
            Your next battle
            <br />
            starts here.
          </h1>
          <p>Choose your mods, then head into a campaign or skirmish.</p>
          <div className="launcher-play-actions">
            <Button
              variant="contained"
              size="large"
              startIcon={<PlayArrowRounded />}
              disabled={!props.launchEnabled || importing || !validName}
              onClick={() => {
                if (validName !== props.playerName) {
                  props.onSaveName(validName);
                }
                props.onLaunch();
              }}
            >
              {props.running ? "Game running" : "Play RWE"}
            </Button>
            <Button onClick={props.onSettings}>Game settings</Button>
          </div>
          <span className="launcher-play-note">
            Single-player works without an internet connection.
          </span>
        </div>
        <div className="launcher-emblem" aria-hidden="true">
          <img src="b4-icon.png" alt="" />
        </div>
      </section>
      <div className="launcher-cards">
        <section className="launcher-card">
          <span className="launcher-eyebrow">PLAYER PROFILE</span>
          <h2>Make a name for yourself.</h2>
          <p>Your name in skirmishes and multiplayer games.</p>
          <form
            onSubmit={event => {
              event.preventDefault();
              if (validName) {
                props.onSaveName(validName);
                setName(validName);
              }
            }}
          >
            <TextField
              label="Player name"
              value={name}
              fullWidth
              size="small"
              onChange={event => setName(event.target.value)}
              error={!validName}
              helperText={
                !validName
                  ? "Enter a player name."
                  : validName !== name.trim()
                    ? `Will save as ${validName}`
                    : "Saved on this computer."
              }
            />
            <Button
              type="submit"
              variant="outlined"
              disabled={!validName || validName === props.playerName}
            >
              Save name
            </Button>
          </form>
        </section>
        <section className="launcher-card">
          <span className="launcher-eyebrow">YOUR LOADOUT</span>
          <h2>
            <ExtensionRounded /> Mods
          </h2>
          <p>
            {props.activeMods.length
              ? "Active in the order shown. Higher entries take priority."
              : "Using the default game data."}
          </p>
          <div className="launcher-mods">
            {props.activeMods.map(mod => (
              <Chip
                key={mod}
                label={mod === "ta" ? "Total Annihilation" : mod}
                variant="outlined"
              />
            ))}
            {!props.installedMods.length && (
              <span>No mods found yet. Import game data to get started.</span>
            )}
          </div>
          <div className="launcher-card-actions">
            <Button variant="outlined" onClick={props.onManageMods}>
              Manage mods
            </Button>
            <Button startIcon={<FolderOpenRounded />} onClick={openMods}>
              Open folder
            </Button>
            <Button onClick={props.onRefreshMods}>Refresh</Button>
          </div>
          <small>
            Put each mod’s archives in its own folder. A manifest is optional.
          </small>
        </section>
      </div>
      <footer className="launcher-footer">
        <span>
          First time here? Import files from your own Total Annihilation
          installation.
        </span>
        <Button onClick={importData} disabled={importing || props.running}>
          {importing ? "Importing…" : "Import game data"}
        </Button>
      </footer>
    </main>
  );
}
