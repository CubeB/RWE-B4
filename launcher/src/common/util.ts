import type { Socket } from "socket.io";

export function masterServer() {
  if (process.env["RWE_MASTER_SERVER"]) {
    return process.env["RWE_MASTER_SERVER"];
  }
  return "https://master.rwe.michaelheasell.com";
}

export function getAddr(socket: Socket, reverseProxy: boolean) {
  if (reverseProxy) {
    const addrs = socket.handshake.headers["x-forwarded-for"] as string;
    const addrsList = addrs.split(", ");
    return addrsList[addrsList.length - 1];
  }
  return socket.handshake.address;
}

export function findAndMap<T, R>(
  arr: T[],
  f: (x: T) => R | undefined
): R | undefined {
  for (const e of arr) {
    const v = f(e);
    if (v !== undefined) {
      return v;
    }
  }
  return undefined;
}

export function choose<T, R>(
  arr: T[],
  f: (x: T, i: number) => R | undefined
): R[] {
  const out: R[] = [];
  let i = 0;
  for (const e of arr) {
    const v = f(e, i);
    if (v !== undefined) {
      out.push(v);
    }
    ++i;
  }
  return out;
}

export function assertNever(x: never): never {
  throw new Error(`Unexpected object: ${x}`);
}

export function toggleItem<T>(arr: T[], item: T): T[] {
  return arr.includes(item) ? arr.filter(x => x !== item) : [...arr, item];
}

export function canMoveUp<T>(arr: T[], item: T): boolean {
  const index = arr.indexOf(item);
  if (index === -1) {
    return false;
  }
  if (index === 0) {
    return false;
  }
  return true;
}

export function moveUp<T>(arr: T[], item: T): T[] {
  const index = arr.indexOf(item);
  if (index === -1) {
    return arr;
  }
  if (index === 0) {
    return arr;
  }
  return [
    ...arr.slice(0, index - 1),
    arr[index],
    arr[index - 1],
    ...arr.slice(index + 1),
  ];
}

export function canMoveDown<T>(arr: T[], item: T): boolean {
  const index = arr.indexOf(item);
  if (index === -1) {
    return false;
  }
  if (index === arr.length - 1) {
    return false;
  }
  return true;
}

export function moveDown<T>(arr: T[], item: T): T[] {
  const index = arr.indexOf(item);
  if (index === -1) {
    return arr;
  }
  if (index === arr.length - 1) {
    return arr;
  }
  return [
    ...arr.slice(0, index),
    arr[index + 1],
    arr[index],
    ...arr.slice(index + 2),
  ];
}
