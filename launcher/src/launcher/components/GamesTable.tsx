import {
  TableBody,
  TableCell,
  TableHead,
  TableRow,
  Typography,
} from "@mui/material";
import Table from "@mui/material/Table";
import * as React from "react";
import { connect } from "react-redux";
import { Dispatch } from "redux";
import { selectGame } from "../actions";
import { State } from "../state";

interface GameTableEntry {
  id: number;
  description: string;
  players: number;
  maxPlayers: number;
}

interface GamesTableStateProps {
  games: GameTableEntry[];
  selectedIndex?: number;
}

interface GamesTableDispatchProps {
  onRowClick?: (id: number) => void;
}

// The header stays put while the list under it scrolls.
const stickyHead = {
  backgroundColor: "#fafafa",
  position: "sticky",
  top: 0,
} as const;

interface UnstyledGamesTableProps
  extends GamesTableStateProps, GamesTableDispatchProps {}
type GamesTableProps = UnstyledGamesTableProps;

const UnstyledGamesTable = (props: GamesTableProps) => {
  const rows = props.games.map((g, i) => {
    const onClick = () => {
      if (props.onRowClick) {
        props.onRowClick(g.id);
      }
    };
    return (
      <TableRow
        key={g.id}
        selected={props.selectedIndex === i}
        onClick={onClick}
      >
        <TableCell>{g.description}</TableCell>
        <TableCell>
          {g.players} / {g.maxPlayers}
        </TableCell>
      </TableRow>
    );
  });

  const rowsOrMessage =
    props.games.length > 0 ? (
      rows
    ) : (
      <TableRow>
        <TableCell colSpan={2}>
          <Typography align="center">
            There are no online games being hosted right now.
          </Typography>
        </TableCell>
      </TableRow>
    );

  return (
    <Table className="games-table">
      <TableHead>
        <TableRow>
          <TableCell sx={stickyHead}>Description</TableCell>
          <TableCell sx={stickyHead}>Players</TableCell>
        </TableRow>
      </TableHead>
      <TableBody>{rowsOrMessage}</TableBody>
    </Table>
  );
};

const UnconnectedGamesTable = UnstyledGamesTable;

function mapStateToProps(state: State): UnstyledGamesTableProps {
  const gameIndex = state.masterClient.games.findIndex(
    g => g.id === state.selectedGameId
  );
  const selectedIndex = gameIndex === -1 ? undefined : gameIndex;
  return {
    games: state.masterClient.games,
    selectedIndex,
  };
}

function mapDispatchToProps(dispatch: Dispatch): GamesTableDispatchProps {
  return {
    onRowClick: (id: number) => dispatch(selectGame(id)),
  };
}

export default connect(
  mapStateToProps,
  mapDispatchToProps
)(UnconnectedGamesTable);
