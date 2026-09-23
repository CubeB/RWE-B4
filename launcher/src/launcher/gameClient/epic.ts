import { StateObservable } from "redux-observable";
import * as rx from "rxjs";
import * as rxop from "rxjs/operators";
import {
  disconnectGame,
  receiveChatMessage,
  receiveHandshakeResponse,
  receiveMapChanged,
  receivePlayerChangedColor,
  receivePlayerChangedSide,
  receivePlayerChangedTeam,
  receivePlayerJoined,
  receivePlayerLeft,
  receivePlayerReady,
  receiveSlotClosed,
  receiveSlotOpened,
  receiveStartGame,
  receiveActiveModsChanged,
  receivePlayerArchivesChanged,
  receivePlayerDroppedFromGame,
  receiveRejoinRequested,
  receiveRejoinBundle,
  receiveRejoinRefused,
} from "./actions";
import { State } from "../state";
import { EpicDependencies } from "../middleware/EpicDependencies";
import { AppAction } from "../actions";

export const gameClientEventsEpic = (
  action$: rx.Observable<AppAction>,
  state$: StateObservable<State>,
  { clientService }: EpicDependencies
): rx.Observable<AppAction> => {
  return rx.merge(
    clientService.onDisconnect.pipe(rxop.map(disconnectGame)),
    clientService.onHandshakeResponse.pipe(rxop.map(receiveHandshakeResponse)),
    clientService.onPlayerJoined.pipe(rxop.map(receivePlayerJoined)),
    clientService.onPlayerLeft.pipe(rxop.map(receivePlayerLeft)),
    clientService.onPlayerChatMessage.pipe(rxop.map(receiveChatMessage)),
    clientService.onPlayerChangedSide.pipe(rxop.map(receivePlayerChangedSide)),
    clientService.onPlayerChangedTeam.pipe(rxop.map(receivePlayerChangedTeam)),
    clientService.onPlayerChangedColor.pipe(
      rxop.map(receivePlayerChangedColor)
    ),
    clientService.onSlotOpened.pipe(rxop.map(receiveSlotOpened)),
    clientService.onSlotClosed.pipe(rxop.map(receiveSlotClosed)),
    clientService.onActiveModsChanged.pipe(rxop.map(receiveActiveModsChanged)),
    clientService.onPlayerArchivesChanged.pipe(
      rxop.map(receivePlayerArchivesChanged)
    ),
    clientService.onPlayerReady.pipe(rxop.map(receivePlayerReady)),
    clientService.onMapChanged.pipe(rxop.map(receiveMapChanged)),
    clientService.onStartGame.pipe(rxop.map(receiveStartGame)),
    clientService.onPlayerDroppedFromGame.pipe(
      rxop.map(receivePlayerDroppedFromGame)
    ),
    clientService.onRejoinRequested.pipe(rxop.map(receiveRejoinRequested)),
    clientService.onRejoinBundle.pipe(rxop.map(receiveRejoinBundle)),
    clientService.onRejoinRefused.pipe(rxop.map(receiveRejoinRefused))
  );
};
