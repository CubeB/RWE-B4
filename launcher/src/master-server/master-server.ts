import * as http from "http";
import { Server as SocketIoServer } from "socket.io";
import yargs from "yargs";
import { hideBin } from "yargs/helpers";
import { getAddr } from "../common/util";
import { GameServer, Room } from "../game-server/game-server";
import * as protocol from "./protocol";

// The types are spelled out because yargs 18 does not infer them from the
// defaults the way the version this was written against did. Without
// `type: "boolean"`, --reverse-proxy parses as false and says so in the log:
// the server then records the proxy's address as every player's, and the
// lobby works while every game sends all the peers to the server.
const argv = yargs(hideBin(process.argv))
  .option("host", { alias: "h", type: "string", default: undefined })
  .option("port", { alias: "p", type: "number", default: 5000 })
  .option("reverse-proxy", { alias: "r", type: "boolean", default: false })
  .parseSync();

const host = argv.host;
const port = argv.port;
const reverseProxy = argv["reverse-proxy"];

console.log(`Running on host ${host}`);
console.log(`Running on port ${port}`);
console.log(`Reverse proxy mode is ${reverseProxy ? "ON" : "OFF"}`);

const server = http.createServer().listen(port, host);
const io = new SocketIoServer(server, { serveClient: false });

function roomToEntry(x: Room): protocol.GetGamesReponseEntry {
  return {
    description: x.description,
    players: x.players.filter(x => x.state === "filled").length,
    max_players: x.players.filter(x => x.state !== "closed").length,
  };
}

function log(msg: string) {
  console.log(`master server: ${msg}`);
}

const masterNamespace = io.of("/master");
const roomsNamespace = io.of("/rooms");

const gameServer = new GameServer(roomsNamespace, reverseProxy);

gameServer.gameUpdated.subscribe(([roomId, room]) => {
  const payload: protocol.GameUpdatedEventPayload = {
    game_id: roomId,
    game: roomToEntry(room),
  };
  masterNamespace.emit(protocol.GameUpdatedEvent, payload);
});

gameServer.gameDeleted.subscribe(id => {
  const payload: protocol.GameDeletedEventPayload = {
    game_id: id,
  };
  masterNamespace.emit(protocol.GameDeletedEvent, payload);
});

masterNamespace.on("connection", socket => {
  const addr = getAddr(socket, reverseProxy);
  log(`Received connection from ${addr}`);

  {
    const gamesList = Array.from(gameServer.getAllRooms()).map(
      ([roomId, room]) => {
        return { id: roomId, game: roomToEntry(room) };
      }
    );

    const payload: protocol.GetGamesResponsePayload = { games: gamesList };
    socket.emit(protocol.GetGamesResponse, payload);
  }

  socket.on(protocol.GetGames, () => {
    const gamesList = Array.from(gameServer.getAllRooms()).map(
      ([roomId, room]) => {
        return { id: roomId, game: roomToEntry(room) };
      }
    );

    const payload: protocol.GetGamesResponsePayload = { games: gamesList };
    socket.emit(protocol.GetGamesResponse, payload);
  });

  socket.on(
    protocol.CreateGameRequest,
    (data: protocol.CreateGameRequestPayload) => {
      const gameInfo = gameServer.createRoom(
        data.description,
        data.max_players
      );

      const payload: protocol.CreateGameResponsePayload = {
        game_id: gameInfo.gameId,
        admin_key: gameInfo.adminKey,
      };
      socket.emit(protocol.CreateGameResponse, payload);

      const eventPayload: protocol.GameCreatedEventPayload = {
        game_id: gameInfo.gameId,
        game: {
          description: data.description,
          players: 0,
          max_players: data.max_players,
        },
      };
      masterNamespace.emit(protocol.GameCreatedEvent, eventPayload);
    }
  );

  socket.on("disconnect", () => {
    log(`Client from ${addr} disconnected`);
  });

  socket.on("error", (error: Error) => {
    log(`Error from ${addr}: ${error}`);
  });
});

log("master server started");
