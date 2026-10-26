jest.mock("./util", () => ({
  ...jest.requireActual("./util"),
  getRweModsPath: jest.fn(),
  automaticallyImportTaData: jest.fn(),
  checkExistingTaData: jest.fn(),
  automaticallySetUpTaModManual: jest.fn(),
  hasUsableTaMod: jest.fn(),
}));

import fs from "fs";
import os from "os";
import path from "path";
import { BehaviorSubject, Subject, Subscription } from "rxjs";
import { StateObservable } from "redux-observable";
import { AppAction } from "./actions";
import { State } from "./state";
import { EpicDependencies } from "./middleware/EpicDependencies";
import {
  automaticallyImportTaData,
  automaticallySetUpTaModManual,
  checkExistingTaData,
  getRweModsPath,
  hasUsableTaMod,
} from "./util";
import { next } from "./wizardActions";
import { wizardEpic } from "./wizardEpic";

const mockGetRweModsPath = jest.mocked(getRweModsPath);
const mockImportTaData = jest.mocked(automaticallyImportTaData);
const mockCheckExistingTaData = jest.mocked(checkExistingTaData);
const mockHasUsableTaMod = jest.mocked(hasUsableTaMod);
const mockSetUpTaModManual = jest.mocked(automaticallySetUpTaModManual);

describe("wizardEpic", () => {
  let directory: string;
  let subscriptions: Subscription[];

  beforeEach(() => {
    directory = fs.mkdtempSync(path.join(os.tmpdir(), "rwe-wizard-epic-"));
    subscriptions = [];
    jest.clearAllMocks();
    mockCheckExistingTaData.mockResolvedValue(false);
    mockHasUsableTaMod.mockResolvedValue(true);
  });
  afterEach(() => {
    for (const subscription of subscriptions) {
      subscription.unsubscribe();
    }
    fs.rmSync(directory, { recursive: true, force: true });
  });

  function startEpic(modsPath: string): {
    action$: Subject<AppAction>;
    emitted: AppAction[];
  } {
    mockGetRweModsPath.mockReturnValue(modsPath);
    const action$ = new Subject<AppAction>();
    const state$ = new StateObservable(
      new BehaviorSubject({} as State),
      {} as State
    );
    const emitted: AppAction[] = [];
    subscriptions.push(
      wizardEpic(action$, state$, {} as EpicDependencies).subscribe(action =>
        emitted.push(action)
      )
    );
    return { action$, emitted };
  }

  async function waitForEmission(emitted: AppAction[]): Promise<AppAction> {
    const deadline = Date.now() + 2000;
    while (emitted.length === 0) {
      if (Date.now() >= deadline) {
        throw new Error("wizard epic did not emit an action");
      }
      await new Promise(resolve => setTimeout(resolve, 10));
    }
    return emitted[0];
  }

  it("does nothing when usable TA data is already configured", async () => {
    const { action$, emitted } = startEpic(path.join(directory, "mods"));
    action$.next({
      type: "RECEIVE_INSTALLED_MODS",
      mods: [{ name: "ta", path: "C:/RWE/Data" }],
    });
    await new Promise(resolve => setTimeout(resolve, 20));
    expect(emitted).toEqual([]);
    expect(mockImportTaData).not.toHaveBeenCalled();
  });

  it("checks for usable native data before showing setup UI", async () => {
    mockCheckExistingTaData.mockResolvedValue(true);
    const { action$, emitted } = startEpic(path.join(directory, "mods"));
    action$.next({ type: "RECEIVE_INSTALLED_MODS", mods: [] });
    await new Promise(resolve => setTimeout(resolve, 20));
    expect(emitted).toEqual([{ type: "REFRESH_INSTALLED_MODS" }]);
    expect(mockImportTaData).not.toHaveBeenCalled();
  });

  it("does not treat an incomplete configured TA folder as usable", async () => {
    mockHasUsableTaMod.mockResolvedValue(false);
    mockImportTaData.mockResolvedValue("No installation found");
    const { action$, emitted } = startEpic(path.join(directory, "mods"));
    action$.next({
      type: "RECEIVE_INSTALLED_MODS",
      mods: [{ name: "ta", path: path.join(directory, "mods", "ta") }],
    });
    expect(await waitForEmission(emitted)).toEqual({ type: "wizard/OPEN" });
    expect(mockHasUsableTaMod).toHaveBeenCalledWith(
      path.join(directory, "mods", "ta")
    );
    expect(mockImportTaData).toHaveBeenCalledTimes(1);
  });

  it("runs automatic import before presenting manual directory selection when data is missing", async () => {
    mockImportTaData.mockResolvedValue("No installation found");
    const { action$, emitted } = startEpic(path.join(directory, "mods"));
    action$.next({ type: "RECEIVE_INSTALLED_MODS", mods: [] });
    expect(await waitForEmission(emitted)).toEqual({ type: "wizard/OPEN" });
    expect(emitted.slice(0, 2)).toEqual([
      { type: "wizard/OPEN" },
      { type: "wizard/START_AUTOMATIC" },
    ]);
    expect(
      await new Promise<AppAction>(resolve => {
        const interval = setInterval(() => {
          if (emitted.some(x => x.type === "wizard/FAIL")) {
            clearInterval(interval);
            resolve(emitted.find(x => x.type === "wizard/FAIL")!);
          }
        }, 5);
      })
    ).toEqual({
      type: "wizard/FAIL",
      message: "Automatic import could not finish: No installation found",
    });
  });

  it("does not recursively re-run startup actions fed back through the epic", async () => {
    mockImportTaData.mockResolvedValue("No installation found");
    mockGetRweModsPath.mockReturnValue(path.join(directory, "mods"));
    const action$ = new Subject<AppAction>();
    const state$ = new StateObservable(
      new BehaviorSubject({} as State),
      {} as State
    );
    const emitted: AppAction[] = [];
    subscriptions.push(
      wizardEpic(action$, state$, {} as EpicDependencies).subscribe(action => {
        emitted.push(action);
        action$.next(action);
      })
    );
    action$.next({ type: "RECEIVE_INSTALLED_MODS", mods: [] });
    action$.next(next(directory));
    const deadline = Date.now() + 2000;
    while (!emitted.some(action => action.type === "wizard/FAIL")) {
      if (Date.now() > deadline)
        throw new Error("automatic setup did not settle");
      await new Promise(resolve => setTimeout(resolve, 5));
    }
    expect(mockImportTaData).toHaveBeenCalledTimes(1);
    expect(mockSetUpTaModManual).not.toHaveBeenCalled();
    expect(
      emitted.filter(action => action.type === "wizard/FAIL")
    ).toHaveLength(1);
  });

  it("refreshes installed mods after automatic import succeeds", async () => {
    mockImportTaData.mockResolvedValue(undefined);
    const { action$, emitted } = startEpic(path.join(directory, "mods"));
    action$.next({ type: "RECEIVE_INSTALLED_MODS", mods: [] });
    const deadline = Date.now() + 2000;
    while (!emitted.some(x => x.type === "REFRESH_INSTALLED_MODS")) {
      if (Date.now() > deadline)
        throw new Error("automatic import did not finish");
      await new Promise(resolve => setTimeout(resolve, 5));
    }
    expect(emitted.slice(-2)).toEqual([
      { type: "wizard/DONE" },
      { type: "REFRESH_INSTALLED_MODS" },
    ]);
  });

  it("emits failure when the chosen directory holds no TA data", async () => {
    mockSetUpTaModManual.mockResolvedValue(false);
    const { action$, emitted } = startEpic(path.join(directory, "mods"));

    action$.next(next(directory));

    expect(await waitForEmission(emitted)).toEqual({
      type: "wizard/FAIL",
      message: "The selected folder did not contain all required game data.",
    });
  });

  it("emits wizard/FAIL when the setup lookup fails", async () => {
    mockSetUpTaModManual.mockRejectedValue(new Error("EBUSY"));
    const { action$, emitted } = startEpic(path.join(directory, "mods"));

    action$.next(next(directory));

    expect(await waitForEmission(emitted)).toEqual({
      type: "wizard/FAIL",
      message: "Could not copy the selected game data: Error: EBUSY",
    });
  });

  it("emits wizard/DONE when setup succeeds", async () => {
    mockSetUpTaModManual.mockResolvedValue(true);
    const { action$, emitted } = startEpic(path.join(directory, "mods"));

    action$.next(next(directory));

    expect(await waitForEmission(emitted)).toEqual({ type: "wizard/DONE" });
    expect(emitted).toContainEqual({ type: "REFRESH_INSTALLED_MODS" });
  });
});
