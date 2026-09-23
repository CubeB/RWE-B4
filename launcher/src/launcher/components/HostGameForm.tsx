import { MenuItem } from "@mui/material";
import Button from "@mui/material/Button";
import TextField from "@mui/material/TextField";
import * as React from "react";
import { connect } from "react-redux";
import { Dispatch } from "redux";
import { hostGameFormCancel, hostGameFormConfirm } from "../actions";

interface HostGameFormDispatchProps {
  onConfirm: (
    playerName: string,
    gameDescription: string,
    players: number
  ) => void;
  onCancel: () => void;
}

type HostGameFormProps = HostGameFormDispatchProps;

interface HostGameFormState {
  playerName: string;
  gameDescription: string;
  players: number;
}

class UnconnectedHostGameForm extends React.Component<
  HostGameFormProps,
  HostGameFormState
> {
  constructor(props: HostGameFormProps) {
    super(props);
    this.state = { playerName: "", gameDescription: "", players: 2 };

    this.handlePlayerNameChange = this.handlePlayerNameChange.bind(this);
    this.handleGameDescriptionChange =
      this.handleGameDescriptionChange.bind(this);
    this.handlePlayersChange = this.handlePlayersChange.bind(this);
    this.handleSubmit = this.handleSubmit.bind(this);
  }
  handlePlayerNameChange(event: React.SyntheticEvent<EventTarget>) {
    const value = (event.target as HTMLInputElement).value;
    this.setState({ ...this.state, playerName: value });
  }
  handleGameDescriptionChange(event: React.SyntheticEvent<EventTarget>) {
    const value = (event.target as HTMLInputElement).value;
    this.setState({ ...this.state, gameDescription: value });
  }
  handlePlayersChange(event: React.SyntheticEvent<EventTarget>) {
    const value = parseInt((event.target as HTMLSelectElement).value);
    this.setState({ ...this.state, players: value });
  }
  handleSubmit(event: React.SyntheticEvent<EventTarget>) {
    event.preventDefault();
    this.props.onConfirm(
      this.state.playerName,
      this.state.gameDescription,
      this.state.players
    );
  }

  render() {
    const playerCountList = [2, 3, 4, 5, 6, 7, 8, 9, 10];
    return (
      <div className="host-game-form-container">
        <form onSubmit={this.handleSubmit}>
          <div className="host-game-form-main-panel">
            <TextField
              sx={{ flexGrow: 1 }}
              label="Your Name"
              value={this.state.playerName}
              onChange={this.handlePlayerNameChange}
            />
            <TextField
              sx={{ marginTop: 1, flexGrow: 1 }}
              label="Game Description"
              value={this.state.gameDescription}
              onChange={this.handleGameDescriptionChange}
            />
            <TextField
              sx={{ marginTop: 1, flexGrow: 0 }}
              select
              label="Players"
              value={this.state.players}
              onChange={this.handlePlayersChange}
            >
              {playerCountList.map(i => (
                <MenuItem key={i} value={i}>
                  {i}
                </MenuItem>
              ))}
            </TextField>
          </div>

          <div className="host-game-form-bottom-panel">
            <Button type="submit" variant="contained" color="primary">
              Create Game Room
            </Button>
            <Button
              variant="contained"
              sx={{ marginLeft: 1 }}
              onClick={this.props.onCancel}
            >
              Cancel
            </Button>
          </div>
        </form>
      </div>
    );
  }
}

function mapDispatchToProps(dispatch: Dispatch): HostGameFormDispatchProps {
  return {
    onConfirm: (playerName: string, gameDescription: string, players: number) =>
      dispatch(hostGameFormConfirm(playerName, gameDescription, players)),
    onCancel: () => dispatch(hostGameFormCancel()),
  };
}

export default connect(undefined, mapDispatchToProps)(UnconnectedHostGameForm);
