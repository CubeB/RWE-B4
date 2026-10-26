jest.mock("./util", () => ({
  ...jest.requireActual("./util"),
  getRweModsPath: jest.fn(),
  automaticallySetUpTaMod: jest.fn(),
  automaticallySetUpTaModManual: jest.fn(),
}));

import fs from "fs";
import os from "os";
import path from "path";
import { BehaviorSubject, Subject, Subscription } from "rxjs";
import { StateObservable } from "redux-observable";
import { AppAction } from "./actions";
import { State } from "./state";
import { EpicDependencies } from "./middleware/EpicDependencies";
import { automaticallySetUpTaModManual, getRweModsPath } from "./util";
import { next } from "./wizardActions";
import { wizardEpic } from "./wizardEpic";

const mockGetRweModsPath = jest.mocked(getRweModsPath);
const mockSetUpTaModManual = jest.mocked(automaticallySetUpTaModManual);

describe("wizardEpic", () => {
  let directory: string;
  let subscriptions: Subscription[];

  beforeEach(() => {
    directory = fs.mkdtempSync(path.join(os.tmpdir(), "rwe-wizard-epic-"));
    subscriptions = [];
    jest.clearAllMocks();
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

  it("does not open the setup wizard at startup when mods/ta is absent", async () => {
    const { emitted } = startEpic(path.join(directory, "mods"));

    // Give an fs.exists-style startup check time to answer before asserting
    // that no wizard was opened.
    await new Promise(resolve => setTimeout(resolve, 100));

    expect(emitted).toEqual([]);
    expect(mockSetUpTaModManual).not.toHaveBeenCalled();
  });

  it("emits wizard/FAIL when the chosen directory holds no TA data", async () => {
    mockSetUpTaModManual.mockResolvedValue(false);
    const { action$, emitted } = startEpic(path.join(directory, "mods"));

    action$.next(next(directory));

    expect(await waitForEmission(emitted)).toEqual({ type: "wizard/FAIL" });
  });

  it("emits wizard/FAIL when the setup lookup fails", async () => {
    mockSetUpTaModManual.mockRejectedValue(new Error("EBUSY"));
    const { action$, emitted } = startEpic(path.join(directory, "mods"));

    action$.next(next(directory));

    expect(await waitForEmission(emitted)).toEqual({ type: "wizard/FAIL" });
  });

  it("emits wizard/DONE when setup succeeds", async () => {
    mockSetUpTaModManual.mockResolvedValue(true);
    const { action$, emitted } = startEpic(path.join(directory, "mods"));

    action$.next(next(directory));

    expect(await waitForEmission(emitted)).toEqual({ type: "wizard/DONE" });
  });
});
