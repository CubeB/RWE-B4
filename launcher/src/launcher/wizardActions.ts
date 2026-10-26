export interface NextAction {
  type: "wizard/NEXT";
  path?: string;
}

export interface StartAutomaticAction {
  type: "wizard/START_AUTOMATIC";
}

export function startAutomatic(): StartAutomaticAction {
  return { type: "wizard/START_AUTOMATIC" };
}

export function next(path?: string): NextAction {
  return {
    type: "wizard/NEXT",
    path,
  };
}

export interface DoneAction {
  type: "wizard/DONE";
}

export function done(): DoneAction {
  return {
    type: "wizard/DONE",
  };
}

export interface FailAction {
  type: "wizard/FAIL";
  message?: string;
}

export function fail(message?: string): FailAction {
  return {
    type: "wizard/FAIL",
    message,
  };
}

export interface OpenAction {
  type: "wizard/OPEN";
}

export function open(): OpenAction {
  return { type: "wizard/OPEN" };
}

export interface CloseAction {
  type: "wizard/CLOSE";
}

export function close(): CloseAction {
  return {
    type: "wizard/CLOSE",
  };
}

export type WizardAction =
  | NextAction
  | StartAutomaticAction
  | DoneAction
  | FailAction
  | OpenAction
  | CloseAction;
