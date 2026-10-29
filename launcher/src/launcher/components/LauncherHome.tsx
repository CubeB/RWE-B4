import * as React from "react";
import { Alert, Button, Chip, FormControlLabel, Switch, TextField } from "@mui/material";
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
  communityPatch: boolean;
  onCommunityPatchChange: (enabled: boolean) => void;
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
        <span className="launcher-eyebrow">ROBOT WAR ENGINE</span>
        <h1>Play</h1>
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
        <FormControlLabel
          className="launcher-community"
          control={
            <Switch
              size="small"
              checked={props.communityPatch}
              onChange={event =>
                props.onCommunityPatchChange(event.target.checked)
              }
            />
          }
          label="Community Patch"
        />
      </section>
      <div className="launcher-cards">
        <section className="launcher-card">
          <h2>Player name</h2>
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
              label="Name"
              value={name}
              fullWidth
              size="small"
              onChange={event => setName(event.target.value)}
              error={!validName}
              helperText={!validName ? "Enter a name." : undefined}
            />
            <Button
              type="submit"
              variant="outlined"
              disabled={!validName || validName === props.playerName}
            >
              Save
            </Button>
          </form>
        </section>
        <section className="launcher-card">
          <h2>
            <ExtensionRounded /> Mods and maps
          </h2>
          <div className="launcher-mods">
            {props.activeMods.map(mod => (
              <Chip
                key={mod}
                label={mod === "ta" ? "Total Annihilation data" : mod}
                variant="outlined"
              />
            ))}
            {!props.installedMods.length && (
              <span>
                No mods or maps found yet. Import game data to get started.
              </span>
            )}
          </div>
          <div className="launcher-card-actions">
            <Button variant="outlined" onClick={props.onManageMods}>
              Manage mods and maps
            </Button>
            <Button startIcon={<FolderOpenRounded />} onClick={openMods}>
              Open folder
            </Button>
            <Button onClick={props.onRefreshMods}>Refresh</Button>
          </div>
        </section>
      </div>
      <footer className="launcher-footer">
        <Button onClick={importData} disabled={importing || props.running}>
          {importing ? "Importing…" : "Import game data"}
        </Button>
      </footer>
    </main>
  );
}
