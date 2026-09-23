import { Observable, Subject } from "rxjs";
import { io as socketioClient, Socket } from "socket.io-client";
import { ModFingerprint } from "../common/archives";
import * as protocol from "./protocol";

export class GameClientService {
  private client: Socket | undefined;

  private readonly _onDisconnect = new Subject<void>();
  private readonly _onHandshakeResponse =
    new Subject<protocol.HandshakeResponsePayload>();
  private readonly _onPlayerJoined =
    new Subject<protocol.PlayerJoinedPayload>();
  private readonly _onPlayerLeft = new Subject<protocol.PlayerLeftPayload>();
  private readonly _onPlayerChatMessage =
    new Subject<protocol.PlayerChatMessagePayload>();
  private readonly _onPlayerChangedSide =
    new Subject<protocol.PlayerChangedSidePayload>();
  private readonly _onPlayerChangedTeam =
    new Subject<protocol.PlayerChangedTeamPayload>();
  private readonly _onPlayerChangedColor =
    new Subject<protocol.PlayerChangedColorPayload>();
  private readonly _onSlotOpened = new Subject<protocol.SlotOpenedPayload>();
  private readonly _onSlotClosed = new Subject<protocol.SlotClosedPayload>();
  private readonly _onActiveModsChanged =
    new Subject<protocol.ActiveModsChangedPayload>();
  private readonly _onPlayerArchivesChanged =
    new Subject<protocol.PlayerArchivesChangedPayload>();
  private readonly _onPlayerReady = new Subject<protocol.PlayerReadyPayload>();
  private readonly _onMapChanged = new Subject<protocol.MapChangedPayload>();
  private readonly _onStartGame = new Subject<protocol.StartGamePayload>();
  private readonly _onPlayerDroppedFromGame =
    new Subject<protocol.PlayerDroppedFromGameBroadcastPayload>();
  private readonly _onRejoinRequested =
    new Subject<protocol.RejoinRequestedPayload>();
  private readonly _onRejoinBundle =
    new Subject<protocol.RejoinBundlePayload>();
  private readonly _onRejoinRefused =
    new Subject<protocol.RejoinRefusedPayload>();

  get onDisconnect(): Observable<void> {
    return this._onDisconnect;
  }
  get onHandshakeResponse(): Observable<protocol.HandshakeResponsePayload> {
    return this._onHandshakeResponse;
  }
  get onPlayerJoined(): Observable<protocol.PlayerJoinedPayload> {
    return this._onPlayerJoined;
  }
  get onPlayerLeft(): Observable<protocol.PlayerLeftPayload> {
    return this._onPlayerLeft;
  }
  get onPlayerChatMessage(): Observable<protocol.PlayerChatMessagePayload> {
    return this._onPlayerChatMessage;
  }
  get onPlayerChangedSide(): Observable<protocol.PlayerChangedSidePayload> {
    return this._onPlayerChangedSide;
  }
  get onPlayerChangedTeam(): Observable<protocol.PlayerChangedTeamPayload> {
    return this._onPlayerChangedTeam;
  }
  get onPlayerChangedColor(): Observable<protocol.PlayerChangedColorPayload> {
    return this._onPlayerChangedColor;
  }
  get onSlotOpened(): Observable<protocol.SlotOpenedPayload> {
    return this._onSlotOpened;
  }
  get onSlotClosed(): Observable<protocol.SlotClosedPayload> {
    return this._onSlotClosed;
  }
  get onActiveModsChanged(): Observable<protocol.ActiveModsChangedPayload> {
    return this._onActiveModsChanged;
  }
  get onPlayerArchivesChanged(): Observable<protocol.PlayerArchivesChangedPayload> {
    return this._onPlayerArchivesChanged;
  }
  get onPlayerReady(): Observable<protocol.PlayerReadyPayload> {
    return this._onPlayerReady;
  }
  get onMapChanged(): Observable<protocol.MapChangedPayload> {
    return this._onMapChanged;
  }
  get onStartGame(): Observable<protocol.StartGamePayload> {
    return this._onStartGame;
  }
  get onPlayerDroppedFromGame(): Observable<protocol.PlayerDroppedFromGameBroadcastPayload> {
    return this._onPlayerDroppedFromGame;
  }
  get onRejoinRequested(): Observable<protocol.RejoinRequestedPayload> {
    return this._onRejoinRequested;
  }
  get onRejoinBundle(): Observable<protocol.RejoinBundlePayload> {
    return this._onRejoinBundle;
  }
  get onRejoinRefused(): Observable<protocol.RejoinRefusedPayload> {
    return this._onRejoinRefused;
  }

  connectToServer(
    connectionString: string,
    gameId: number,
    playerName: string,
    ipv4Address: string,
    installedMods: string[],
    adminKey?: string
  ) {
    this.client = socketioClient(connectionString);

    this.client.on("connect", () => {
      this.log("Connected");
    });
    this.client.on("connect_error", (error: Error) => {
      this.log(`Connection error: ${error}`);
    });
    this.client.on("connect_timeout", () => {
      this.log("Connection timeout");
    });
    this.client.on("error", (error: Error) => {
      this.log(`Error: ${error}`);
    });

    this.client.on("reconnect", (attempt: number) => {
      this.log(`successful reconnect attempt ${attempt}`);
    });
    this.client.on("reconnect_attempt", () => {
      this.log("reconnect attempt");
    });
    this.client.on("reconnecting", (attempt: number) => {
      this.log(`reconnecting attempt ${attempt}`);
    });
    this.client.on("reconnect_failed", () => {
      this.log("failed to reconnect and gave up");
    });

    this.client.on("disconnect", () => {
      this.log("Disconnected");
      this.client = undefined;
      this._onDisconnect.next();
    });

    const handshakePayload: protocol.HandshakePayload = {
      gameId,
      name: playerName,
      ipv4Address,
      installedMods,
    };
    if (adminKey !== undefined) {
      handshakePayload.adminKey = adminKey;
    }
    this.client.emit(protocol.Handshake, handshakePayload);

    this.client.on(
      protocol.HandshakeResponse,
      (data: protocol.HandshakeResponsePayload) => {
        this._onHandshakeResponse.next(data);
      }
    );

    this.client.on(
      protocol.PlayerJoined,
      (data: protocol.PlayerJoinedPayload) => {
        this._onPlayerJoined.next(data);
      }
    );

    this.client.on(protocol.PlayerLeft, (data: protocol.PlayerLeftPayload) => {
      this._onPlayerLeft.next(data);
    });

    this.client.on(
      protocol.PlayerChatMessage,
      (data: protocol.PlayerChatMessagePayload) => {
        this._onPlayerChatMessage.next(data);
      }
    );

    this.client.on(
      protocol.PlayerChangedSide,
      (data: protocol.PlayerChangedSidePayload) => {
        this._onPlayerChangedSide.next(data);
      }
    );

    this.client.on(
      protocol.PlayerChangedTeam,
      (data: protocol.PlayerChangedTeamPayload) => {
        this._onPlayerChangedTeam.next(data);
      }
    );

    this.client.on(
      protocol.PlayerChangedColor,
      (data: protocol.PlayerChangedColorPayload) => {
        this._onPlayerChangedColor.next(data);
      }
    );

    this.client.on(protocol.SlotOpened, (data: protocol.SlotOpenedPayload) => {
      this._onSlotOpened.next(data);
    });

    this.client.on(protocol.SlotClosed, (data: protocol.SlotClosedPayload) => {
      this._onSlotClosed.next(data);
    });

    this.client.on(
      protocol.ActiveModsChanged,
      (data: protocol.ActiveModsChangedPayload) => {
        this._onActiveModsChanged.next(data);
      }
    );

    this.client.on(
      protocol.PlayerArchivesChanged,
      (data: protocol.PlayerArchivesChangedPayload) => {
        this._onPlayerArchivesChanged.next(data);
      }
    );

    this.client.on(
      protocol.PlayerReady,
      (data: protocol.PlayerReadyPayload) => {
        this._onPlayerReady.next(data);
      }
    );

    this.client.on(protocol.MapChanged, (data: protocol.MapChangedPayload) => {
      this._onMapChanged.next(data);
    });

    this.client.on(protocol.StartGame, (data: protocol.StartGamePayload) => {
      this._onStartGame.next(data);
    });

    this.client.on(
      protocol.PlayerDroppedFromGameBroadcast,
      (data: protocol.PlayerDroppedFromGameBroadcastPayload) => {
        this._onPlayerDroppedFromGame.next(data);
      }
    );

    this.client.on(
      protocol.RejoinRequested,
      (data: protocol.RejoinRequestedPayload) => {
        this._onRejoinRequested.next(data);
      }
    );

    this.client.on(
      protocol.RejoinBundle,
      (data: protocol.RejoinBundlePayload) => {
        this._onRejoinBundle.next(data);
      }
    );

    this.client.on(
      protocol.RejoinRefused,
      (data: protocol.RejoinRefusedPayload) => {
        this._onRejoinRefused.next(data);
      }
    );
  }

  disconnect() {
    if (!this.client) {
      return;
    }
    this.client.close();
  }

  /** Says that this client's game has declared a peer lost. */
  playerDroppedFromGame(playerId: number, tick: number) {
    if (!this.client) {
      return;
    }
    const payload: protocol.PlayerDroppedFromGamePayload = { playerId, tick };
    this.client.emit(protocol.PlayerDroppedFromGame, payload);
  }

  /** Asks to be let back into the game this client was dropped from. */
  requestRejoin() {
    if (!this.client) {
      return;
    }
    this.client.emit(protocol.RequestRejoin);
  }

  /**
   * Hands over the recording a returning player needs. Sent as a binary
   * payload, socket.io carrying one without base64 in it.
   */
  sendRejoinBundle(playerId: number, tick: number, data: ArrayBuffer) {
    if (!this.client) {
      return;
    }
    const payload: protocol.RejoinBundlePayload = { playerId, tick, data };
    this.client.emit(protocol.RejoinBundle, payload);
  }

  /** Says that a rejoin that was asked for will not be happening. */
  sendRejoinRefused(playerId: number, reason: string) {
    if (!this.client) {
      return;
    }
    const payload: protocol.RejoinRefusedPayload = { playerId, reason };
    this.client.emit(protocol.RejoinRefused, payload);
  }

  sendChatMessage(message: string) {
    if (!this.client) {
      return;
    }
    const payload: protocol.ChatMessagePayload = message;
    this.client.emit(protocol.ChatMessage, payload);
  }

  changeSide(side: protocol.PlayerSide) {
    if (!this.client) {
      return;
    }
    const payload: protocol.ChangeSidePayload = { side };
    this.client.emit(protocol.ChangeSide, payload);
  }

  changeTeam(team?: number) {
    if (!this.client) {
      return;
    }
    const payload: protocol.ChangeTeamPayload = { team };
    this.client.emit(protocol.ChangeTeam, payload);
  }

  changeColor(color: number) {
    if (!this.client) {
      return;
    }
    const payload: protocol.ChangeColorPayload = { color };
    this.client.emit(protocol.ChangeColor, payload);
  }

  openSlot(slotId: number) {
    if (!this.client) {
      return;
    }
    const payload: protocol.OpenSlotPayload = { slotId };
    this.client.emit(protocol.OpenSlot, payload);
  }

  closeSlot(slotId: number) {
    if (!this.client) {
      return;
    }
    const payload: protocol.CloseSlotPayload = { slotId };
    this.client.emit(protocol.CloseSlot, payload);
  }

  setActiveMods(mods: string[]) {
    if (!this.client) {
      return;
    }
    const payload: protocol.SetActiveModsPayload = { mods };
    this.client.emit(protocol.SetActiveMods, payload);
  }

  setArchives(mods: ModFingerprint[]) {
    if (!this.client) {
      return;
    }
    const payload: protocol.SetArchivesPayload = { mods };
    this.client.emit(protocol.SetArchives, payload);
  }

  setReadyState(value: boolean) {
    if (!this.client) {
      return;
    }
    const payload: protocol.ReadyPayload = value;
    this.client.emit(protocol.Ready, payload);
  }

  changeMap(mapName: string) {
    if (!this.client) {
      return;
    }
    const payload: protocol.ChangeMapPayload = { mapName };
    this.client.emit(protocol.ChangeMap, payload);
  }

  requestStartGame() {
    if (!this.client) {
      return;
    }
    this.client.emit(protocol.RequestStartGame);
  }

  private log(msg: string) {
    console.log(`Game client: ${msg}`);
  }
}
