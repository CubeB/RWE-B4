import React, { useState } from "react";
import {
  Dialog,
  Typography,
  CircularProgress,
  DialogActions,
  Button,
  DialogTitle,
  DialogContent,
  Grid,
} from "@mui/material";
import { invoke } from "../browserApi";
import { assertNever } from "../../common/util";

export type State = "welcome" | "working" | "success" | "fail";
export interface WizardProps {
  state: State;
  error?: string;
  onNext: (path?: string) => void;
  onClose: () => void;
}

export function Wizard(props: WizardProps) {
  const [filePath, setFilePath] = useState<string>();
  const [selecting, setSelecting] = useState(false);
  const chooseDirectory = async () => {
    setSelecting(true);
    try {
      setFilePath(
        (await invoke<string | null>("choose-directory")) || undefined
      );
    } finally {
      setSelecting(false);
    }
  };
  return (
    <Dialog open onClose={props.onClose}>
      <DialogTitle>Total Annihilation Mod Setup</DialogTitle>
      <DialogContent>
        <WizardContents
          state={props.state}
          error={props.error}
          path={filePath}
          onChange={p => {
            console.log(p);
            setFilePath(p);
          }}
          onChoose={chooseDirectory}
          selecting={selecting}
        />
      </DialogContent>
      <DialogActions>
        {(props.state === "welcome" || props.state === "fail") && (
          <Button onClick={() => props.onNext(filePath)}>Next</Button>
        )}
        {props.state === "success" && (
          <Button onClick={() => props.onClose()}>Finish</Button>
        )}
      </DialogActions>
    </Dialog>
  );
}

function WizardContents(props: {
  state: State;
  error?: string;
  path?: string;
  onChange: (path?: string) => void;
  onChoose: () => void;
  selecting: boolean;
}) {
  switch (props.state) {
    case "welcome":
      return <WizardWelcome />;
    case "working":
      return <WizardWorking />;
    case "success":
      return <WizardSuccess />;
    case "fail":
      return (
        <WizardManual
          path={props.path}
          error={props.error}
          onChange={props.onChange}
          onChoose={props.onChoose}
          selecting={props.selecting}
        />
      );
  }
  assertNever(props.state);
}

function WizardWelcome() {
  return (
    <Typography>
      This wizard will automatically detect your existing installation of Total
      Annihilation and create a Total Annihilation mod for Robot War Engine.
    </Typography>
  );
}

function WizardWorking() {
  return (
    <Grid container direction="row" justifyContent="center">
      <Grid>
        <CircularProgress />
      </Grid>
    </Grid>
  );
}

function WizardSuccess() {
  return (
    <>
      <Typography>
        Total Annihilation game data is ready to use in Robot War Engine.
      </Typography>
      <Typography>
        The launcher will refresh the available game data automatically.
      </Typography>
    </>
  );
}

function WizardManual(props: {
  path?: string;
  error?: string;
  onChange: (path?: string) => void;
  onChoose: () => void;
  selecting: boolean;
}) {
  return (
    <>
      <Typography>
        The wizard was unable to locate Total Annihilation game data.
      </Typography>
      {props.error && <Typography color="error">{props.error}</Typography>}
      <Typography>Manually locate your Total Annihilation folder:</Typography>
      <Button onClick={props.onChoose} disabled={props.selecting}>
        {props.selecting ? "Selecting…" : "Choose folder"}
      </Button>
      {props.path && <Typography>{props.path}</Typography>}
    </>
  );
}
