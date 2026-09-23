import Button from "@mui/material/Button";
import * as React from "react";
import { Grid } from "@mui/material";

interface BottomPanelStateProps {
  hostEnabled: boolean;
  joinEnabled: boolean;
  launchEnabled: boolean;
}

interface BottomPanelDispatchProps {
  onHostGame: () => void;
  onJoinGame: () => void;
  onLaunchRwe: () => void;
  onOpenModsDialog: () => void;
  onOpenConfigDialog: () => void;
}

interface BottomPanelProps
  extends BottomPanelStateProps, BottomPanelDispatchProps {}

const BottomPanel = (props: BottomPanelProps) => {
  return (
    <div className="bottom-panel">
      <div className="bottom-panel-left">
        <Button
          variant="contained"
          disabled={!props.hostEnabled}
          onClick={props.onHostGame}
        >
          Host Game
        </Button>
        <Button
          variant="contained"
          color="primary"
          sx={{ marginLeft: 1 }}
          disabled={!props.joinEnabled}
          onClick={props.onJoinGame}
        >
          Join Game
        </Button>
      </div>
      <div className="bottom-panel-right">
        <Grid container spacing={1}>
          <Grid>
            <Button variant="contained" onClick={props.onOpenConfigDialog}>
              RWE Settings
            </Button>
          </Grid>
          <Grid>
            <Button variant="contained" onClick={props.onOpenModsDialog}>
              Single Player Mods
            </Button>
          </Grid>
          <Grid>
            <Button
              variant="contained"
              disabled={!props.launchEnabled}
              onClick={props.onLaunchRwe}
            >
              Launch RWE
            </Button>
          </Grid>
        </Grid>
      </div>
    </div>
  );
};

export default BottomPanel;
