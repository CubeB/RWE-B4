import { AppAction } from "./actions";

export interface WizardState {
  step: "welcome" | "working" | "success" | "fail";
  error?: string;
}

export function wizardReducer(
  state: WizardState,
  action: AppAction
): WizardState {
  switch (action.type) {
    case "wizard/NEXT": {
      return { step: "working" };
    }
    case "wizard/START_AUTOMATIC": {
      return { step: "working" };
    }
    case "wizard/DONE": {
      return { step: "success" };
    }
    case "wizard/FAIL": {
      return { step: "fail", error: action.message };
    }
    default:
      return state;
  }
}
