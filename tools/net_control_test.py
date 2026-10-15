"""Live multiplayer control checks, invoked by net-test.py --controls."""

import json
import time


def exercise_controls(peers, readers):
    def command(peer, name):
        proc = peers[peer].proc
        if proc.poll() is not None:
            raise RuntimeError(f"player {peer} exited during control checks")
        proc.stdin.write(json.dumps({"command": name}) + "\n")
        proc.stdin.flush()

    def status(peer):
        command(peer, "game-status")
        state = readers[peer].wait_event("game-status", 5)
        if state is None:
            raise RuntimeError(f"player {peer} did not answer game-status")
        return state

    def all_states():
        return [status(i) for i in range(len(peers))]

    def wait_pause(paused):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            states = all_states()
            if all(s["paused"] == paused for s in states):
                return states
            time.sleep(0.05)
        raise RuntimeError(f"shared pause did not converge to {paused}: {states}")

    def require(condition, description):
        if not condition:
            raise RuntimeError(description)
        print(f"PASS control: {description}", flush=True)

    def advancing(before, after):
        return all(b["tick"] < a["tick"] for b, a in zip(before, after))

    for player in range(len(peers)):
        command(player, "game-menu")
        time.sleep(0.2)
        before = all_states()
        require(before[player]["menuOpen"], f"player {player}'s F2 opens options")
        require(all(not s["paused"] for s in before), "F2 does not pause any client")
        time.sleep(0.8)
        require(advancing(before, all_states()), "all clients advance while F2 is open")
        command(player, "game-menu")
        time.sleep(0.2)
        require(not status(player)["menuOpen"], "F2 closes options")

    # Both players pause, and both the original player and another player
    # resume. No player retains a private pause that can block somebody else.
    for pauser, resumer in ((0, 1), (1, 0), (0, 0), (1, 1)):
        command(pauser, "pause")
        before = wait_pause(True)
        time.sleep(0.8)
        after = all_states()
        require(all(s["paused"] for s in after), "all clients display the shared pause")
        require(all(b["tick"] == a["tick"] for b, a in zip(before, after)), "all simulation clocks stop while paused")
        command(resumer, "pause")
        before = wait_pause(False)
        time.sleep(0.8)
        require(advancing(before, all_states()), f"player {resumer} resumes player {pauser}'s pause for everyone")

    command(0, "pause")
    wait_pause(True)
    command(1, "game-menu")
    time.sleep(0.2)
    require(all(s["paused"] for s in all_states()), "opening F2 preserves an existing shared pause")
    command(1, "pause")
    wait_pause(False)
    require(status(1)["menuOpen"], "another player can resume while their F2 options stay open")
    command(0, "pause")
    wait_pause(True)
    command(1, "game-menu")
    time.sleep(0.2)
    require(all(s["paused"] for s in all_states()), "closing F2 does not release somebody else's pause")
    command(1, "pause")
    wait_pause(False)
