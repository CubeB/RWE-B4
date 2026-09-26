#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""A network game on one machine, for testing the lockstep path.

Starts N peers of the same game on loopback, each with its own port, log and
sync hash log. Then it can kill one of them, and it always compares what the
survivors simulated.

The comparison is the point. Every peer writes RWE_HASH_LOG, one line a tick,
so two peers that stayed in step have identical files and two that did not
have a first differing line -- which is the tick, and the same tick the game's
own desync report names. A run that ends "peers agree" has been checked
against the only thing that can check it.

On Linux each peer runs under xvfb-run when there is no display, which is what
makes it runnable in CI as well as on a desktop. On Windows the peers start
wherever the window manager puts them; nothing here depends on user32.

  uv run tools/net-test.py                       # 2 peers, 60s, nobody killed
  uv run tools/net-test.py --peers 3 --kill 2    # 3 peers, kill player 2 midway
  uv run tools/net-test.py --ai                  # add a computer player at the end
  uv run tools/net-test.py --desync-at 200       # peer 0 reports a wrong hash from
                                                 # tick 200, to fire the report
  uv run tools/net-test.py --chat                # every peer says one line, to
                                                 # show chat crossing the wire
  uv run tools/net-test.py --rejoin              # kill a peer, then bring it back
                                                 # and check it is still in step
  uv run tools/net-test.py --rejoin --bridge     # the same, asked for the way a
                                                 # launcher asks: over the game's
                                                 # own stdin and stdout
  uv run tools/net-test.py --lag 1:50            # peer 1 sleeps 50 ms after every
                                                 # tick, to reproduce a slow machine

Two things it is good for beyond drop handling: any change to the simulation
can be run past it to see whether two peers still agree, and RWE_DESYNC_AT
gives the desync report something to report without waiting for a real fault.

Exit code is 0 when the peers agree. It is non-zero when they diverge, when a
peer failed to start, or when nothing was compared. With --desync-at the
divergence is the expected result, so the exit code asks the opposite question:
non-zero if the peers agreed when one was told to lie.
"""

import argparse
import json
import os
import queue
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from typing import Any

NAMES = ["Alice", "Bob", "Carol", "Dave", "Erin", "Frank", "Grace", "Heidi"]
SIDES = ["ARM", "CORE"]

LOCKSTEP_PATTERN = re.compile(r"Lockstep summary:")
INTERESTING_PATTERN = re.compile(
    r"waiting for|quiet for|dropped|left the game|Desync|diverged at tick|critical|Chat from player"
)
BUNDLE_PATTERN = re.compile(r"tick=(?P<tick>\d+) file=(?P<file>.+)$")
REJOIN_BUNDLE_PATTERN = re.compile(r"REJOIN-BUNDLE")
LAG_PATTERN = re.compile(r"\d+(@\d+)?")

STARTUP_TIMEOUT = 30.0
KILL_GRACE = 5.0


def say(*parts: object) -> None:
    print(*parts, flush=True)


def read_lines(path: Path) -> list[str]:
    try:
        return path.read_text(errors="replace").splitlines()
    except OSError:
        return []


def last_matching_line(path: Path, pattern: re.Pattern[str]) -> str | None:
    for line in reversed(read_lines(path)):
        if pattern.search(line):
            return line
    return None


def use_xvfb() -> bool:
    return sys.platform.startswith("linux") and not os.environ.get("DISPLAY")


def x_display_locks() -> set[str]:
    return {path.name for path in Path("/tmp").glob(".X*-lock")}


def os_proc_kwargs() -> dict[str, Any]:
    if os.name == "nt":
        return {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP}
    return {"start_new_session": True}


def default_exe(repo: Path) -> Path:
    names = ["rwe.exe", "rwe"] if os.name == "nt" else ["rwe", "rwe.exe"]
    for directory in ("build-release", "build"):
        for name in names:
            candidate = repo / directory / name
            if candidate.is_file():
                return candidate
    return repo / "build-release" / names[0]


def pick_free_ports(count: int) -> list[int]:
    """Bind count UDP sockets to port 0 and keep them until every port is chosen.

    Binding to 0 and reading the number back gives ports the kernel believes
    are free; holding all of them open until they are all chosen keeps the
    kernel from handing the same one out twice.
    """
    held: list[socket.socket] = []
    try:
        for _ in range(count):
            sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
            sock.bind(("::1", 0))
            held.append(sock)
        return [sock.getsockname()[1] for sock in held]
    finally:
        for sock in held:
            sock.close()


class BridgeReader:
    """Reads the game's stdout on a thread so the harness never blocks on it.

    A launcher holds the game's stdin and stdout; a blocking read would hang
    the harness for good on a game that has stopped saying anything. Lines are
    queued from a daemon thread, so every wait has a timeout.
    """

    def __init__(self, stream) -> None:
        self._lines: queue.Queue[str | None] = queue.Queue()
        self.eof = False
        self._thread = threading.Thread(target=self._pump, args=(stream,), daemon=True)
        self._thread.start()

    def _pump(self, stream) -> None:
        try:
            for line in stream:
                line = line.rstrip("\r\n")
                if line:
                    self._lines.put(line)
        except Exception:
            pass
        self.eof = True
        self._lines.put(None)

    def read_line(self, timeout: float) -> str | None:
        try:
            item = self._lines.get(timeout=timeout)
        except queue.Empty:
            return None
        if item is None:
            self.eof = True
        return item

    def wait_event(self, name: str, timeout: float) -> dict | None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self.read_line(1.0)
            if line is None:
                if self.eof:
                    return None
                continue
            try:
                event = json.loads(line)
            except ValueError:
                continue
            # To the console, the way the launcher would see it.
            say(f"bridge said: {line}")
            if event.get("event") == name:
                return event
        return None


class Peer:
    def __init__(self, index: int, proc: subprocess.Popen, log: Path, hashes: Path) -> None:
        self.index = index
        self.proc = proc
        self.log = log
        self.hashes = hashes
        self.out_handle: Any = None


class Harness:
    def __init__(self, args: argparse.Namespace, exe: Path, out_dir: Path, ports: list[int],
                 lag_peer: int, lag_spec: str) -> None:
        self.args = args
        self.exe = exe
        self.out_dir = out_dir
        self.ports = ports
        self.lag_peer = lag_peer
        self.lag_spec = lag_spec
        self.count = args.peers
        self.kill = args.kill
        self.peers: list[Peer] = []
        self.bridge_reader: BridgeReader | None = None
        self.use_xvfb = use_xvfb()
        self.base_env = self._child_env()
        self.diverged = False
        self.first_divergence = 0
        self.compared = False

    def _child_env(self) -> dict[str, str]:
        env = dict(os.environ)
        env.setdefault("SDL_AUDIODRIVER", "dummy")
        if sys.platform.startswith("linux"):
            runtime = env.get("XDG_RUNTIME_DIR")
            valid = False
            if runtime:
                path = Path(runtime)
                try:
                    stat = path.stat()
                    valid = (
                        path.is_dir()
                        and stat.st_uid == os.getuid()
                        and (stat.st_mode & 0o777) == 0o700
                    )
                except OSError:
                    valid = False
            if not valid:
                path = self.out_dir / "xdg"
                path.mkdir(parents=True, exist_ok=True)
                os.chmod(path, 0o700)
                env["XDG_RUNTIME_DIR"] = str(path)
        return env

    def peer_args(self, me: int, log: Path, record_replay: bool) -> list[str]:
        args = [
            "--map", self.args.map,
            "--width", "320",
            "--height", "240",
            "--seed", "7",
            "--port", str(self.ports[me]),
        ]
        for p in range(self.count):
            side = SIDES[p % 2]
            if p == me:
                spec = f"{NAMES[p]};Human;{side};{p}"
            else:
                spec = f"{NAMES[p]};Network,[::1]:{self.ports[p]};{side};{p}"
            args += ["--player", spec]
        if self.args.ai:
            args += ["--player", f"Computer;Computer;{SIDES[self.count % 2]};{self.count}"]
        args += ["--log", str(log)]
        if record_replay:
            args += ["--record-replay", str(self.out_dir / f"peer{me}.rwereplay")]
        for path in self.args.data_path or []:
            args += ["--data-path", path]
        return args

    def peer_env(self, me: int, hash_log: Path, rejoining: bool = False) -> dict[str, str]:
        env = dict(self.base_env)
        env["RWE_HASH_LOG"] = str(hash_log)
        # A returning peer is asking for nothing back and has already said its
        # chat line, so only the switch that reproduces a slow machine applies.
        if not rejoining:
            # A rejoin needs somebody to have left. The peer entitled to declare
            # both the drop and the return is the lowest-numbered survivor, so
            # every survivor is told and only that one acts on it. Over the bridge
            # the launcher asks instead, so the test switch stays unset.
            if self.args.rejoin and me != self.kill and not self.args.bridge:
                env["RWE_REJOIN_TEST"] = f"{self.kill}:{self.args.rejoin_after}"
            # Only one peer may counterfeit a desync: the report exists to show
            # two peers disagreeing, and both lying would be two peers agreeing
            # again.
            if self.args.desync_at > 0 and me == 0:
                env["RWE_DESYNC_AT"] = str(self.args.desync_at)
            if self.args.chat:
                env["RWE_CHAT_TEST"] = f"{300 + 60 * me}:hello from {NAMES[me]}"
        if self.lag_peer == me:
            env["RWE_SIM_LAG"] = self.lag_spec
        return env

    def launch(self, me: int, args: list[str], env: dict[str, str], bridge: bool) -> Peer:
        log = self.out_dir / f"peer{me}.log"
        hashes = self.out_dir / f"peer{me}.hashes"
        if bridge and me == 0:
            args.append("--bridge")
        command = ["xvfb-run", "-a", str(self.exe), *args] if self.use_xvfb else [str(self.exe), *args]
        out_handle = open(self.out_dir / f"peer{me}.stdout", "wb")
        if bridge and me == 0:
            proc = subprocess.Popen(
                command,
                env=env,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=out_handle,
                text=True,
                **os_proc_kwargs(),
            )
            self.bridge_reader = BridgeReader(proc.stdout)
        else:
            proc = subprocess.Popen(
                command,
                env=env,
                stdout=out_handle,
                stderr=subprocess.STDOUT,
                **os_proc_kwargs(),
            )
        peer = Peer(me, proc, log, hashes)
        peer.out_handle = out_handle
        return peer

    def start(self) -> None:
        for me in range(self.count):
            args = self.peer_args(me, self.out_dir / f"peer{me}.log", record_replay=bool(self.args.rejoin))
            env = self.peer_env(me, self.out_dir / f"peer{me}.hashes")
            locks_before = x_display_locks()
            self.peers.append(self.launch(me, args, env, self.args.bridge))
            if self.use_xvfb and me + 1 < self.count:
                # xvfb-run -a picks its display by looking for a free
                # /tmp/.X<n>-lock, and two launched together both see the same
                # number free and share one server. Then killing one peer's
                # group kills the display the other is drawing on. Waiting for
                # each new lock before starting the next keeps them apart.
                self.wait_for_new_x_lock(locks_before)
        pids = ", ".join(str(p.proc.pid) for p in self.peers)
        say(f"{self.count} peers, pids {pids}, logs in {self.out_dir}")

    @staticmethod
    def wait_for_new_x_lock(locks_before: set[str]) -> None:
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            if x_display_locks() - locks_before:
                return
            time.sleep(0.05)

    def wait_until_ready(self) -> None:
        """Wait until every peer has started ticking, or a bound has passed.

        The Windows original moved each window off-screen instead; there is no
        window to find here, and the first hash line says the same thing --
        the simulation is running -- from any platform.
        """
        deadline = time.monotonic() + STARTUP_TIMEOUT
        while time.monotonic() < deadline:
            ready = all(p.hashes.exists() and p.hashes.stat().st_size > 0 for p in self.peers)
            if ready:
                return
            if all(p.proc.poll() is not None for p in self.peers):
                return
            time.sleep(0.25)

    def run(self) -> int:
        self.start()
        self.wait_until_ready()

        if self.kill >= 0:
            time.sleep(self.args.kill_after)
            say(f"killing player {self.kill}")
            self.terminate(self.peers[self.kill])
            if not self.args.rejoin:
                time.sleep(max(0.0, self.args.seconds - self.args.kill_after))
        else:
            time.sleep(self.args.seconds)

        rejoin_succeeded = False
        if self.args.rejoin:
            rejoin_succeeded = self.run_rejoin()

        agreed, compared = self.compare(rejoin_succeeded)
        return self.finish(agreed, compared, rejoin_succeeded)

    def run_rejoin(self) -> bool:
        # Peer 0 announces the bundle when it reaches the tick the returning
        # peer has to arrive at: by then its recording holds exactly the ticks
        # that peer is missing, and every peer is stalled waiting for it.
        say("waiting for the rejoin to be agreed and the bundle named")
        at_tick = 0
        bundle_src = None

        if self.args.bridge:
            # The launcher's half, in three messages: the game says who it
            # lost, the launcher asks for them back, and the game says where
            # the recording they need is. Nothing here reads the log.
            assert self.bridge_reader is not None
            dropped = self.bridge_reader.wait_event("player-dropped", 60)
            if not dropped:
                say("RESULT: the game never reported a drop over the bridge")
            else:
                time.sleep(self.args.rejoin_after)
                say(f"asking over the bridge for player {dropped['player']} back")
                stdin = self.peers[0].proc.stdin
                assert stdin is not None
                stdin.write(json.dumps({"command": "rejoin", "player": int(dropped["player"])}) + "\n")
                stdin.flush()
                bundle_event = self.bridge_reader.wait_event("rejoin-bundle", 120)
                if bundle_event:
                    at_tick = int(bundle_event["tick"])
                    bundle_src = bundle_event["file"]
        else:
            bundle_line = None
            for _ in range(120):
                time.sleep(1)
                bundle_line = last_matching_line(self.peers[0].log, REJOIN_BUNDLE_PATTERN)
                if bundle_line:
                    break
            if bundle_line:
                match = BUNDLE_PATTERN.search(bundle_line)
                if not match:
                    raise RuntimeError(f"Could not read the bundle line: {bundle_line}")
                at_tick = int(match.group("tick"))
                bundle_src = match.group("file").strip()

        if not bundle_src:
            say("RESULT: no bundle was named; the rejoin never got that far")
            return False

        # Copied rather than read in place, because the host is still writing
        # to it. This is the step a lobby would do over its own connection.
        bundle = self.out_dir / "rejoin-bundle.rwereplay"
        shutil.copyfile(bundle_src, bundle)
        say(f"bundle at tick {at_tick} copied from {bundle_src}")

        args = self.peer_args(self.kill, self.peers[self.kill].log, record_replay=False)
        args += ["--rejoin", str(bundle), "--rejoin-tick", str(at_tick)]
        env = self.peer_env(self.kill, self.peers[self.kill].hashes, rejoining=True)
        if self.peers[self.kill].out_handle is not None:
            self.peers[self.kill].out_handle.close()
        self.peers[self.kill] = self.launch(self.kill, args, env, bridge=False)
        say(f"player {self.kill} restarted to rejoin at tick {at_tick}, pid {self.peers[self.kill].proc.pid}")
        time.sleep(self.args.seconds)
        return True

    def compare(self, rejoin_succeeded: bool) -> tuple[bool, bool]:
        if rejoin_succeeded:
            survivors = list(range(self.count))
        else:
            survivors = [s for s in range(self.count) if s != self.kill]

        for s in survivors:
            lines = read_lines(self.peers[s].hashes)
            last = lines[-1] if lines else ""
            exited = self.peers[s].proc.poll() is not None
            suffix = "  (EXITED ON ITS OWN)" if exited else ""
            say(f"player {s} : last hash line '{last}'{suffix}")

        agreed = True
        compared = False
        if len(survivors) < 2:
            return agreed, compared

        reference = read_lines(self.peers[survivors[0]].hashes)
        for s in survivors[1:]:
            other = read_lines(self.peers[s].hashes)
            n = min(len(reference), len(other))
            if n > 0:
                compared = True
            first_diff = 0
            for i in range(n):
                if reference[i] != other[i]:
                    first_diff = i + 1
                    break
            if first_diff > 0:
                agreed = False
                if not self.diverged:
                    self.diverged = True
                    self.first_divergence = first_diff
                say(f"DIVERGED: player {survivors[0]} and player {s} differ from hash line {first_diff}")
                say(f"  {survivors[0]}: {reference[first_diff - 1]}")
                say(f"  {s}: {other[first_diff - 1]}")
            else:
                say(f"player {survivors[0]} and player {s} agree on all {n} compared ticks")
        return agreed, compared

    def finish(self, agreed: bool, compared: bool, rejoin_succeeded: bool) -> int:
        self.terminate_all()

        expected_divergence = self.args.desync_at > 0
        if not compared:
            say("RESULT: nothing was compared")
            code = 1
        elif self.diverged:
            say(f"RESULT: peers diverged from hash line {self.first_divergence}")
            code = 0 if expected_divergence else 1
        elif expected_divergence:
            say("RESULT: --desync-at asked for a divergence and the peers agreed")
            code = 1
        else:
            say("RESULT: peers agree")
            code = 0

        if self.args.rejoin and not rejoin_succeeded:
            code = 1

        self.report_logs()
        return code

    def report_logs(self) -> None:
        # Each peer's own account of the run, beside the hash comparison above.
        # The numbers say whether a lagged peer slowed the game smoothly or
        # stalled it stop start, which is the question --lag is asked to answer.
        for s in range(self.count):
            summaries = [line for line in read_lines(self.peers[s].log) if LOCKSTEP_PATTERN.search(line)]
            if summaries:
                say(f"=================== player {s} lockstep ===================")
                for line in summaries:
                    say(line)

        for s in range(self.count):
            interesting = [line for line in read_lines(self.peers[s].log) if INTERESTING_PATTERN.search(line)]
            if interesting:
                say(f"=================== player {s} ===================")
                for line in interesting[:10]:
                    say(line)

    def terminate(self, peer: Peer) -> None:
        proc = peer.proc
        if proc.poll() is not None:
            return
        if os.name == "nt":
            try:
                subprocess.run(
                    ["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    check=False,
                )
            except OSError:
                proc.terminate()
        else:
            try:
                os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
            except (ProcessLookupError, PermissionError):
                pass
        try:
            proc.wait(timeout=KILL_GRACE)
        except subprocess.TimeoutExpired:
            if os.name == "nt":
                proc.kill()
            else:
                try:
                    os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
                except (ProcessLookupError, PermissionError):
                    pass
                proc.wait()

    def terminate_all(self) -> None:
        for peer in self.peers:
            self.terminate(peer)

    def cleanup(self) -> None:
        self.terminate_all()
        for peer in self.peers:
            if peer.proc.stdin is not None:
                try:
                    peer.proc.stdin.close()
                except OSError:
                    pass
            if peer.out_handle is not None:
                try:
                    peer.out_handle.close()
                except OSError:
                    pass


def parse_lag(spec: str, peers: int) -> tuple[int, str]:
    if not spec:
        return -1, ""
    peer_text, colon, rest = spec.partition(":")
    if not colon or not peer_text.isdigit() or int(peer_text) >= peers or not LAG_PATTERN.fullmatch(rest or ""):
        raise SystemExit(
            f"Bad --lag '{spec}': expected <peer>:<ms>[@<fromTick>] with a peer from "
            f"0 to {peers - 1}, e.g. --lag 1:50"
        )
    return int(peer_text), rest


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--peers", type=int, default=2, help="how many peers (default 2)")
    parser.add_argument("--seconds", type=int, default=60, help="how long to let them play (default 60)")
    parser.add_argument("--kill", type=int, default=-1, metavar="P", help="kill peer P part way through")
    parser.add_argument("--kill-after", type=int, default=20, metavar="SEC", help="seconds before the kill (default 20)")
    parser.add_argument("--ai", action="store_true", help="add a computer player at the end")
    parser.add_argument("--desync-at", type=int, default=0, metavar="T", help="make peer 0 report a wrong hash from tick T")
    parser.add_argument("--chat", action="store_true", help="have every peer say one line")
    parser.add_argument("--lag", default="", metavar="P:MS[@TICK]", help="sleep MS after every tick on peer P")
    parser.add_argument("--rejoin", action="store_true", help="kill a peer and bring it back")
    parser.add_argument("--bridge", action="store_true", help="drive the rejoin over the game's stdin and stdout")
    parser.add_argument("--rejoin-after", type=int, default=8, metavar="SEC", help="seconds after the drop before asking (default 8)")
    parser.add_argument("--map", default="Coast To Coast", help="map name (default 'Coast To Coast')")
    parser.add_argument("--base-port", type=int, default=0, metavar="N", help="first port to use; default picks free ports")
    parser.add_argument("--exe", type=Path, help="engine binary; default build-release/rwe then build/rwe")
    parser.add_argument("--out-dir", type=Path, help="where logs and hash logs go; default a temp directory")
    parser.add_argument("--data-path", action="append", metavar="DIR", help="game data search path; may be repeated")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv if argv is not None else sys.argv[1:])

    repo = Path(__file__).resolve().parent.parent
    exe = (args.exe or default_exe(repo)).resolve()
    if not exe.is_file():
        raise SystemExit(f"No engine at {exe}. Build the rwe target first.")
    if use_xvfb() and shutil.which("xvfb-run") is None:
        raise SystemExit("No DISPLAY and no xvfb-run; install Xvfb or set DISPLAY.")

    if args.peers < 1:
        raise SystemExit("--peers must be at least 1")
    if args.peers > len(NAMES):
        raise SystemExit(f"--peers is at most {len(NAMES)}")
    if args.rejoin and args.kill < 0:
        args.kill = args.peers - 1
    if args.kill >= args.peers:
        raise SystemExit(f"--kill {args.kill} is not one of the {args.peers} peers")

    lag_peer, lag_spec = parse_lag(args.lag, args.peers)

    if args.out_dir:
        out_dir = args.out_dir.resolve()
    else:
        out_dir = Path(tempfile.mkdtemp(prefix="rwe-net-test-")).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    for stale in list(out_dir.glob("*.log")) + list(out_dir.glob("*.hashes")):
        stale.unlink(missing_ok=True)

    if args.base_port:
        ports = list(range(args.base_port, args.base_port + args.peers))
    else:
        ports = pick_free_ports(args.peers)

    harness = Harness(args, exe, out_dir, ports, lag_peer, lag_spec)
    try:
        return harness.run()
    except KeyboardInterrupt:
        say("interrupted")
        return 130
    finally:
        harness.cleanup()


if __name__ == "__main__":
    sys.exit(main())
