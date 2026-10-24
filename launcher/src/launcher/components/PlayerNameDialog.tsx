import {
  Button,
  Dialog,
  DialogActions,
  DialogContent,
  DialogContentText,
  TextField,
} from "@mui/material";
import * as React from "react";
import { normalizePlayerName } from "../preferences";

interface PlayerNameDialogProps {
  initialName?: string;
  open: boolean;
  onClose: () => void;
  onConfirm: (name: string) => void;
}
interface PlayerNameDialogState {
  name: string;
}

export class PlayerNameDialog extends React.Component<
  PlayerNameDialogProps,
  PlayerNameDialogState
> {
  constructor(props: PlayerNameDialogProps) {
    super(props);
    this.state = { name: props.initialName || "" };

    this.handleConfirm = this.handleConfirm.bind(this);
    this.handleChange = this.handleChange.bind(this);
    this.handleClose = this.handleClose.bind(this);
  }

  handleChange(event: React.SyntheticEvent<EventTarget>) {
    const value = (event.target as HTMLInputElement).value;
    this.setState({ name: value });
  }

  componentDidUpdate(previous: PlayerNameDialogProps) {
    if (!previous.open && this.props.open) {
      this.setState({ name: this.props.initialName || "" });
    }
  }

  handleConfirm(event: React.SyntheticEvent<EventTarget>) {
    event.preventDefault();
    const name = normalizePlayerName(this.state.name);
    if (name) {
      this.props.onConfirm(name);
    }
  }

  handleClose() {
    this.setState({ name: this.props.initialName || "" });
    this.props.onClose();
  }

  render() {
    return (
      <Dialog open={this.props.open} onClose={this.handleClose}>
        <form onSubmit={this.handleConfirm}>
          <DialogContent>
            <DialogContentText>Enter your player name</DialogContentText>
            <TextField
              autoFocus
              label="Player name"
              value={this.state.name}
              onChange={this.handleChange}
            />
          </DialogContent>
          <DialogActions>
            <Button
              color="primary"
              type="submit"
              disabled={!normalizePlayerName(this.state.name)}
            >
              Join
            </Button>
            <Button onClick={this.handleClose}>Cancel</Button>
          </DialogActions>
        </form>
      </Dialog>
    );
  }
}
