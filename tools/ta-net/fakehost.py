#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["scapy"]
# ///
"""A DirectPlay host that is not Total Annihilation, for a real TA to join and play against.

It is driven by a capture of a real TA hosting a game that a second TA joined (record it with
tcpdump started *before* the join; docs/TA-NETWORK.md, "Test bed"). From that capture it takes the
player ids, map, battleroom status and launch sequence, and the host's units as they stood at
their first full-state record. Everything else is live:

  * the DirectPlay session handshake, and TA's pings in both directions
  * the battleroom keepalive while the launch is held
  * unit sync, by echoing the joiner's own unit ids, so no unit CRC is ever computed
  * after launch, the host's units: every owned slot's full state once a cycle, and the commander
    walking wherever it is told

    tools/ta-net/fakehost.py <capture.pcap> [--dir DIR] [--options 0x48] [--team N]

Then join from TA, and in DIR:
    touch go                  release the launch (the replay holds just before it)
    echo joiner > walk        walk the host's commander to the joiner's commander
    echo "x z" > walk         walk it to a world position
fakehost.log is the session log; fakehost-rx.jsonl is every TA subpacket the joiner sent.

It binds TCP 2300 and 47624 and UDP 2350 and 47624, so nothing else on the machine may be
hosting, and a DirectPlay helper left running by an earlier game has to be stopped first.
"""
import argparse, json, math, os, select, socket, struct, time

from tanet import (CMDS, decode, decode_2c, dp_app_tcp, dp_app_udp, dp_command, dp_header, encode_2c, is_dp,
                   ping, read_capture, subpackets_of, ta_packet, ta_payload)

HOST_TCP, HOST_UDP, ENUM_PORT = 2300, 2350, 47624
GAME_NAME = "rwe-spike"
SYNC_BATCH = 35               # keeps an uncompressed 0x1a message near the size of TA's compressed ones
COMMANDER_SPEED = 78643       # 16.16 world units a tick, as the recorded commander's full state reports


class Recording:
    """What the fake host takes from a capture of a real host and joiner."""

    def __init__(self, path, max_units, options, team):
        self.options, self.team = options, team
        ms = read_capture(path)
        enum = next(((f, m) for t, p, f, m in ms if p == "TCP" and is_dp(m) and dp_command(m) == 0x01), None)
        if enum is None:
            raise SystemExit("no ENUMSESSIONSREPLY in the capture: start tcpdump before the joiner looks for games")
        host_flow, self.enum_reply = enum
        replies = [m for t, p, f, m in ms if f == host_flow and is_dp(m) and dp_command(m) == 0x07]
        self.sys_id, self.player_id = (struct.unpack_from("<I", m, 28)[0] for m in replies[:2])
        self.superenum = next(m for t, p, f, m in ms if f == host_flow and is_dp(m) and dp_command(m) == 0x29)
        afr = next(m for t, p, f, m in ms if p == "TCP" and is_dp(m) and dp_command(m) == 0x13)
        sp = afr[48 + 0x30: 48 + 0x30 + 0x20]
        self.rec_join_tcp, self.rec_join_udp = sp[0:8], sp[16:24]
        anchor = next(t for t, p, f, m in ms if p == "TCP" and is_dp(m) and dp_command(m) == 0x08)
        self.host_player = next(struct.unpack_from("<I", m, 20)[0] for t, p, f, m in ms
                                if f == host_flow and not is_dp(m) and len(m) > 28)
        tl = [(t - anchor, p, m) for t, p, f, m in ms if t >= anchor and (
              (p == "TCP" and f == host_flow) or
              (p == "UDP" and len(m) > 8 and struct.unpack_from("<I", m)[0] == self.host_player))]

        first_loading = next(t for t, p, m in tl if any(s[0] == 0x08 for s in subpackets_of(m, p)))
        self.gate = max(t for t, p, m in tl if is_dp(m) and dp_command(m) == 0x1A and t <= first_loading)
        self.session_name = self.enum_reply[112:-2].decode("utf-16-le")
        changed = [m for t, p, m in tl if is_dp(m) and dp_command(m) == 0x1A and t <= self.gate]
        if changed:               # the map may have changed after the joiner first saw the game
            raw = changed[-1][120:]
            self.session_name = raw[:next(i for i in range(0, len(raw) - 1, 2) if raw[i:i + 2] == b"\0\0")].decode("utf-16-le")

        # Replay in game only until the commander's first full-state record; after that the host's units are ours.
        self.units, cut = {}, None
        for i, (t, p, m) in enumerate(tl):
            if p != "UDP" or t < self.gate:
                continue
            for s in subpackets_of(m, p):
                if s[0] != 0x2C:
                    continue
                tick, entries, full = decode_2c(s, max_units)
                self.last_tick = tick
                if full and not entries:
                    self.units[full["slot"]] = full
                    if full["slot"] == 0 and cut is None:
                        cut = i + 1
            if cut is not None:
                break
        if cut is None:
            raise SystemExit("no commander full-state record in the capture: stay in game for 10 seconds or more")

        self.timeline = []
        for t, p, m in tl[:cut]:
            if is_dp(m) and dp_command(m) == 0x0B:
                continue                                              # DELETEPLAYER
            subs = subpackets_of(m, p)
            if p == "UDP" and subs and all(s[0] == 0x02 for s in subs):
                continue                                              # pings carry a clock; answered live
            if p == "TCP" and any(s[0] == 0x1A for s in subs):       # unit sync is echoed live
                rest = [s for s in subs if s[0] != 0x1A]
                if not rest:
                    continue
                m = self.with_subs(p, m, rest)
            self.timeline.append((t, p, self.rewrite(p, m)))
        self.keepalive = [m for t, p, m in self.timeline if p == "TCP" and t < self.gate and not is_dp(m)][-2:]

    @staticmethod
    def with_subs(proto, m, subs, marker=None):
        pkt = ta_packet(subs, marker if marker is not None else decode(ta_payload(m, proto))[1][3:7])
        if proto == "UDP":
            return m[:8] + pkt
        return struct.pack("<I", (struct.unpack_from("<I", m)[0] & 0xFFF00000) | (28 + len(pkt))) + m[4:28] + pkt

    def rewrite(self, proto, m):
        """Apply --options (0x20[157] and the session description's dwUser1 high byte) and --team (0x24)."""
        if is_dp(m):
            if self.options is None:
                return m
            m = bytearray(m)
            for i in range(28, len(m) - 3):
                if m[i] == 0x01 and m[i + 1] == 0x00 and m[i + 2] in (0x01, 0x02, 0x32) and m[i + 3] & 0x40:
                    m[i + 3] = self.options
            return bytes(m)
        subs = subpackets_of(m, proto)
        new = []
        for s in subs:
            if s[0] == 0x20 and self.options is not None:
                s = s[:157] + bytes([self.options]) + s[158:]
            elif s[0] == 0x24 and self.team is not None:
                s = s[:5] + bytes([self.team])
            new.append(s)
        return m if new == subs else self.with_subs(proto, m, new)


class FakeHost:
    def __init__(self, rec, workdir, max_units):
        self.rec, self.dir, self.max_units = rec, workdir, max_units
        self.log_file = open(os.path.join(workdir, "fakehost.log"), "a", buffering=1)
        self.rx = open(os.path.join(workdir, "fakehost-rx.jsonl"), "a", buffering=1)
        self.go_file, self.walk_file = os.path.join(workdir, "go"), os.path.join(workdir, "walk")
        self.reset()

    def reset(self):
        self.ip = self.tcp_port = self.udp_port = self.out = None
        self.anchor = None; self.cursor = 0; self.held_at = None; self.shift = 0.0
        self.last_keepalive = self.last_ping = 0.0
        self.owned_since = None; self.owned_sent = 0; self.marker = 0xFFFFFEAD
        self.units = {k: dict(v, pos=list(v["pos"]), rot_yzx=list(v["rot_yzx"])) for k, v in self.rec.units.items()}
        self.walk = None; self.pending = []; self.joiner_at = None; self.synced = 0; self.sync_count = None
        for f in (self.go_file, self.walk_file):
            if os.path.exists(f):
                os.remove(f)

    def log(self, *a):
        line = f"{time.strftime('%H:%M:%S')} " + " ".join(str(x) for x in a)
        print(line, flush=True); self.log_file.write(line + "\n")

    # ---- sending

    def send_tcp(self, m, what=None):
        if self.out is None:
            self.out = socket.create_connection((self.ip, self.tcp_port), timeout=3)
            self.log("outbound TCP to joiner", (self.ip, self.tcp_port))
        self.out.sendall(m)
        if what:
            self.log("->", what)

    def send_udp(self, udp, to, subs, marker=b"\xff\xff\xff\xff"):
        udp.sendto(dp_app_udp(self.rec.host_player, to, ta_packet(subs, marker)), (self.ip, self.udp_port))

    def app_tcp(self, to, subs):
        self.send_tcp(dp_app_tcp(HOST_TCP, self.rec.host_player, to, ta_packet(subs)))

    # ---- DirectPlay

    def enum_reply(self):
        name = (GAME_NAME.ljust(16) + self.rec.session_name[16:]).encode("utf-16-le") + b"\0\0"
        body = self.rec.enum_reply[28:112] + name
        return self.rec.rewrite("TCP", dp_header(0x0001, len(body), HOST_TCP) + body)

    def superenum(self):
        m = bytearray(self.rec.superenum)
        for recorded, port in ((self.rec.rec_join_tcp, self.tcp_port), (self.rec.rec_join_udp, self.udp_port)):
            i = m.find(recorded)
            if i >= 0:
                m[i + 2:i + 4] = struct.pack(">H", port)
        return self.rec.rewrite("TCP", bytes(m))

    def on_dp(self, m, src_ip):
        cmd = dp_command(m)
        port = struct.unpack_from(">H", m, 6)[0]
        self.log("<-", CMDS.get(cmd, hex(cmd)))
        if cmd == 0x02:
            self.ip, self.tcp_port = src_ip, port
            self.send_tcp(self.enum_reply(), f"ENUMSESSIONSREPLY '{GAME_NAME}' on {self.rec.session_name[16:].strip()}")
        elif cmd == 0x05:
            self.ip = self.ip or src_ip; self.tcp_port = self.tcp_port or port
            pid = self.rec.sys_id if struct.unpack_from("<I", m, 28)[0] & 1 else self.rec.player_id
            body = struct.pack("<I", pid) + b"\0" * 36
            self.send_tcp(dp_header(0x0007, len(body), HOST_TCP) + body, f"REQUESTPLAYERREPLY {pid:#x}")
        elif cmd == 0x13:
            sp = m[48 + 0x30: 48 + 0x30 + 0x20]
            self.udp_port = struct.unpack_from(">H", sp, 18)[0]
            self.send_tcp(self.superenum(), "SUPERENUMPLAYERSREPLY")
        elif cmd == 0x08:
            self.anchor = time.monotonic()
            self.app_tcp(self.rec.player_id, [bytes([0x1A, 0x00]) + b"\0" * 12])
            self.log(f"joiner created its player; launch held at +{self.rec.gate:.1f}s until '{self.go_file}' exists")
        elif cmd == 0x0B and self.anchor is not None:
            self.log("joiner left; ready for a fresh join")
            if self.out:
                self.out.close()
            self.reset()

    # ---- TA

    def on_unit_sync(self, subs):
        batch = []
        for s in subs:
            if s[0] == 0x1A and s[1] == 0x01:
                self.sync_count = struct.unpack_from("<I", s, 10)[0]
            elif s[0] == 0x1A and s[1] == 0x02:
                batch.append(struct.unpack_from("<I", s, 6)[0])
        if not batch:
            return
        # A real host answers each id first as not yet confirmed and then as in use; 1 + 2n records in all.
        recs = [bytes([0x1A, 0x03]) + b"\0" * 4 + struct.pack("<I", uid) + status
                for status in (b"\x01\x00\xff\xff", b"\x01\x01\xff\xff") for uid in batch]
        for i in range(0, len(recs), SYNC_BATCH):
            self.app_tcp(self.rec.player_id, recs[i:i + SYNC_BATCH])
        self.synced += len(batch)
        self.log(f"unit sync: echoed {self.synced}/{self.sync_count} ids")

    def on_app(self, m, proto, udp):
        p = ta_payload(m, proto)
        if p is None:
            return
        ok, hdr, subs = decode(p)
        self.rx.write(json.dumps({"t": time.time(), "proto": proto, "ck": ok, "subs": [s.hex() for s in subs]}) + "\n")
        if proto == "TCP":
            self.on_unit_sync(subs)
        for s in subs:
            if s[0] == 0x02 and len(s) == 13:
                a, b, who = struct.unpack_from("<III", s, 1)
                if b == 0 and who != self.rec.host_player:
                    self.send_udp(udp, who, [ping(a, int(time.monotonic() * 1000) & 0xFFFFFFFF, who)])
            elif s[0] == 0x2C:
                tick, entries, full = decode_2c(s, self.max_units)
                for e in entries:
                    if e["slot"] == 0 and e["waypoints"]:
                        self.joiner_at = e["waypoints"][-1]
                if full and full["slot"] == 0 and "pos" in full:
                    self.joiner_at = (full["pos"][0] / 65536, full["pos"][2] / 65536)

    # ---- the host's units

    def start_walk(self, target):
        cmd = self.units[0]
        x, z = cmd["pos"][0] / 65536, cmd["pos"][2] / 65536
        tx, tz = target
        d = math.hypot(tx - x, tz - z)
        if d > 200:           # stop short of whatever is standing at the target
            tx, tz = x + (tx - x) * (d - 150) / d, z + (tz - z) * (d - 150) / d
        self.walk = (tx, tz)
        # Heading is atan2(dx, dz) + half a turn, fitted to the recorded commander.
        cmd["rot_yzx"][0] = int(math.atan2(tx - x, tz - z) / (2 * math.pi) * 65536 + 32768) & 0xFFFF
        cmd["speed"] = COMMANDER_SPEED
        self.pending.append({"slot": 0, "type": cmd["type"], "waypoints": [(int(x), int(z)), (int(tx), int(tz))]})
        self.log(f"walk: commander ({x:.0f},{z:.0f}) -> ({tx:.0f},{tz:.0f}), about {d / 1.2 / 30:.0f}s")

    def step_units(self):
        if not self.walk:
            return
        cmd = self.units[0]
        x, z = cmd["pos"][0] / 65536, cmd["pos"][2] / 65536
        tx, tz = self.walk
        d = math.hypot(tx - x, tz - z)
        if d <= COMMANDER_SPEED / 65536:
            cmd["pos"][0], cmd["pos"][2] = int(tx * 65536), int(tz * 65536)
            cmd["speed"] = 0; self.walk = None
            self.pending.append({"slot": 0, "type": cmd["type"], "waypoints": []})
            self.log("walk: arrived")
        else:
            k = COMMANDER_SPEED / 65536 / d
            cmd["pos"][0] += int((tx - x) * k * 65536); cmd["pos"][2] += int((tz - z) * k * 65536)

    def check_walk_file(self):
        if not os.path.exists(self.walk_file):
            return
        arg = open(self.walk_file).read().split(); os.remove(self.walk_file)
        if arg == ["joiner"]:
            if self.joiner_at is None:
                self.log("walk: the joiner's commander has not been seen yet")
            else:
                self.start_walk(self.joiner_at)
        elif len(arg) == 2:
            self.start_walk((float(arg[0]), float(arg[1])))

    # ---- the clock

    def pump(self, udp):
        if self.anchor is None:
            return
        now = time.monotonic()
        if now - self.last_ping > 2.0 and self.udp_port:
            self.last_ping = now
            self.send_udp(udp, 0, [ping(int(now * 1000) & 0xFFFFFFFF, 0, self.rec.host_player)], b"\xf8\xff\xff\xff")
        if self.held_at is not None and now - self.last_keepalive > 2.0:
            # A host that falls silent in the battleroom is offered for rejection.
            self.last_keepalive = now
            for m in self.rec.keepalive:
                self.send_tcp(m)
        if self.owned_since is None:
            self.replay(udp, now)
            return
        self.check_walk_file()
        due = int((now - self.owned_since) * 30)
        if due - self.owned_sent < 6:
            return
        subs = []
        for k in range(self.owned_sent, due):
            tick = self.rec.last_tick + 1 + k
            self.step_units()
            entries, self.pending = self.pending, []
            # Every slot's full state, every cycle: an empty-slot record deletes whatever the receiver has there.
            subs.append(encode_2c(tick, entries, self.units.get(tick % self.max_units)))
        self.owned_sent = due
        self.send_udp(udp, 0, subs, struct.pack("<I", self.marker))
        self.marker = (self.marker - 1) & 0xFFFFFFFF

    def replay(self, udp, now):
        t_now = now - self.anchor - self.shift
        tl = self.rec.timeline
        while self.cursor < len(tl) and tl[self.cursor][0] <= t_now:
            t, proto, m = tl[self.cursor]
            if t >= self.rec.gate and not os.path.exists(self.go_file):
                if self.held_at is None:
                    self.held_at = now; self.log("launch held; touch go to release it")
                return
            if self.held_at is not None:
                self.shift += now - self.held_at; self.held_at = None; self.log("launching")
                t_now = now - self.anchor - self.shift
            if proto == "TCP":
                self.send_tcp(m)
            else:
                udp.sendto(m, (self.ip, self.udp_port))
            self.cursor += 1
        if self.cursor == len(tl):
            self.owned_since = now; self.owned_sent = 0
            self.log(f"replay done at host tick {self.rec.last_tick}; the host's units {sorted(self.units)} are now ours")

    def serve(self):
        tcp = socket.socket(); tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); tcp.bind(("0.0.0.0", HOST_TCP)); tcp.listen(8)
        etcp = socket.socket(); etcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); etcp.bind(("0.0.0.0", ENUM_PORT)); etcp.listen(8)
        eudp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); eudp.bind(("0.0.0.0", ENUM_PORT))
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); udp.bind(("0.0.0.0", HOST_UDP))
        self.log(f"fake host up: '{GAME_NAME}' on {self.rec.session_name[16:].strip()}, "
                 f"host units {sorted(self.rec.units)}, replay to host tick {self.rec.last_tick}")
        conns = {}
        while True:
            r, _, _ = select.select([tcp, etcp, eudp, udp] + list(conns), [], [], 0.005)
            for s in r:
                if s in (tcp, etcp):
                    c, a = s.accept(); conns[c] = [a, b""]
                elif s in (eudp, udp):
                    m, a = s.recvfrom(65536)
                    if is_dp(m):
                        self.on_dp(m, a[0])
                    else:
                        self.udp_port = self.udp_port or a[1]
                        self.on_app(m, "UDP", udp)
                else:
                    try:
                        d = s.recv(65536)
                    except OSError:
                        d = b""
                    if not d:
                        del conns[s]; continue
                    buf = conns[s][1] + d
                    while len(buf) >= 4 and 4 <= (n := struct.unpack_from("<I", buf)[0] & 0xFFFFF) <= len(buf):
                        m, buf = buf[:n], buf[n:]
                        if is_dp(m):
                            self.on_dp(m, conns[s][0][0])
                        else:
                            self.on_app(m, "TCP", udp)
                    conns[s][1] = buf
            try:
                self.pump(udp)
            except OSError as e:
                self.log("send failed:", e, "- waiting for a fresh join"); self.reset()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture")
    ap.add_argument("--dir", default=".", help="where the log, the received traffic and the go/walk files live")
    ap.add_argument("--options", type=lambda x: int(x, 0), help="host options byte, e.g. 0x48 (docs/TA-NETWORK.md)")
    ap.add_argument("--team", type=int, help="the host's team in the battleroom")
    ap.add_argument("--max-units", type=int, default=250, help="the game's unit limit, which sets the full-state cycle")
    a = ap.parse_args()
    os.makedirs(a.dir, exist_ok=True)
    FakeHost(Recording(a.capture, a.max_units, a.options, a.team), a.dir, a.max_units).serve()


if __name__ == "__main__":
    main()
