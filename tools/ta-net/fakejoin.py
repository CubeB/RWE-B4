#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["scapy"]
# ///
"""A DirectPlay joiner that is not Total Annihilation: the acceptance test for an RWE host.

The other side of fakehost.py, and what the RWE host work is written against. It finds a host with
ENUMSESSIONS, does the join handshake, sits in the battleroom, runs unit sync as a joiner, readies,
follows the launch and then owns a commander in the game: 0x2c for it six ticks to a packet, thirty
times a second, a waypoint entry while it walks, 0x0d and 0x0b when it fires at one of the host's
units, and 0x0c when the host's damage should have killed it.

It checks the host as well, and its exit status is the verdict. Each check is a rule in
docs/TA-NETWORK.md and prints a PASS or a FAIL line: the session handshake completed, the host's
status arrived, the unit-sync echo covered all 1 + 2n records, the launch sequence arrived, every
host-owned slot got a correct full-state record once a cycle, and pings were answered live.

    tools/ta-net/fakejoin.py <capture.pcap> [options]
    tools/ta-net/fakejoin.py <capture.pcap> --walk 2000,1500 --attack 3 --play-seconds 60

It computes no unit CRC and builds no session description of its own: the ids, the CRCs, the two
status records and the commander's state come from a capture of a real TA joining a real host,
patched for this run's ports, player ids and name. So it needs a capture of the data set the host is
serving, and a host that hands out ids, which every DirectPlay host does and the spike's fake host
does by replaying the ones it recorded.

    --host ADDR --host-tcp 2300 --host-udp 2350 --enum-port 47624
    --tcp-port 2301 --udp-port 2351     its own listening ports, so it can run beside a host
    --name NAME --team N                the player name field is four characters wide
    --play-seconds N                    how long to play after the launch
    --walk X,Z        walk its commander to a world position
    --attack ID       fire at a host unit id, every half second
    --simulate-hit N  take N damage on its own commander, to exercise the 0x0c path
"""
import argparse, math, os, select, socket, struct, sys, time

from tanet import (CMDS, commander_build, damage, death, decode, decode_2c, dp_add_forward_request, dp_app_tcp,
                   dp_app_udp, dp_command, dp_create_player, dp_delete_player, dp_enum_sessions, dp_request_player_id,
                   encode_2c, is_dp, ping, read_capture, shot, split_stream, status, subpackets_of, sync_progress,
                   ta_packet, ta_payload, team, unit_state, unit_sync_join)

ENUM_PORT, HOST_TCP, HOST_UDP = 47624, 2300, 2350
TICK_RATE, TICKS_PER_PACKET = 30, 6
SYNC_BATCH = 77              # the ids to a message, as the recorded joiner sent them
COMMANDER_SPEED = 78643       # 16.16 world units a tick, as the recorded commander's full state reports
HEARTBEAT = 2.0               # a host that goes quiet in the battleroom is offered for rejection
PING_EVERY, LOADING_EVERY = 1.0, 0.3
READY = 0x20                 # bit 0x20 of the state byte, as gpgnet4ta reads it
STATE_IN_GAME = 0x32         # the state byte the recorded joiner sent over UDP
PROGRESS = (0x00, 0x25, 0x50, 0x75, 0x64)   # the loading ladder, over TCP
LAUNCH_RECORDS = (0x15, 0x07, 0x2A, 0x20, 0x24, 0x09, 0x11)   # what a joiner sends over UDP at the launch


class Template:
    """What the joiner takes from a capture of a real TA joining a real host."""

    def __init__(self, path, max_units):
        if not os.path.isfile(path):
            raise SystemExit(f"no capture at {path}: it is the source of this joiner's unit ids and CRCs")
        try:
            ms = read_capture(path)
        except Exception as e:
            raise SystemExit(f"{path} could not be read as a capture: {e}")
        host_flow = next((f for t, p, f, m in ms if p == "TCP" and is_dp(m) and dp_command(m) == 0x01), None)
        if host_flow is None:
            raise SystemExit("no ENUMSESSIONSREPLY in the capture: start tcpdump before anyone looks for games")
        self.host_flow = host_flow
        replies = [m for t, p, f, m in ms if f == host_flow and is_dp(m) and dp_command(m) == 0x07]
        if len(replies) < 2:
            raise SystemExit("no REQUESTPLAYERREPLY pair in the capture: it does not hold a join")
        self.rec_sys_id, self.rec_player_id = (struct.unpack_from("<I", m, 28)[0] for m in replies[:2])

        def mine(t, p, f, m):
            return f != host_flow and (p == "TCP" or (len(m) > 8 and struct.unpack_from("<I", m)[0] == self.rec_player_id))

        self.units, self.unit_count = [], None
        for t, p, f, m in ms:
            if not mine(t, p, f, m):
                continue
            for s in subpackets_of(m, p):
                if s[0] == 0x1A and s[1] == 1 and len(s) >= 14:
                    self.unit_count = struct.unpack_from("<I", s, 10)[0]
                elif s[0] == 0x1A and s[1] == 2 and len(s) >= 14:
                    self.units.append(struct.unpack_from("<II", s, 6))
        if not self.units:
            raise SystemExit("no unit sync sub-type 2 in the capture: that join never reached unit sync")
        if self.unit_count is not None and self.unit_count != len(self.units):
            raise SystemExit(f"the capture says {self.unit_count} unit types but carries {len(self.units)} CRCs")

        first = {}
        for t, p, f, m in ms:
            for s in subpackets_of(m, p):
                if s[0] in (0x20, 0x09, 0x11) and (s[0], p == "UDP") not in first and mine(t, p, f, m):
                    first[(s[0], p == "UDP")] = s
        self.status_battleroom, self.status_game = first.get((0x20, False)), first.get((0x20, True))
        self.build, self.state = first.get((0x09, True)), first.get((0x11, True))
        if not (self.status_battleroom and self.status_game and self.build and self.state):
            raise SystemExit("the capture has no player info or commander from the joiner")
        self.cmd_type = struct.unpack_from("<H", self.build, 1)[0]
        self.cmd_unit = struct.unpack_from("<H", self.build, 3)[0]
        self.cmd_pos = list(struct.unpack_from("<iii", self.build, 5))

        self.full = {}
        for t, p, f, m in ms:
            if p != "UDP" or not mine(t, p, f, m):
                continue
            for s in subpackets_of(m, p):
                if s[0] == 0x2C:
                    fl = decode_2c(s, max_units)[2]
                    if fl and "pos" in fl:
                        self.full.setdefault(fl["slot"], fl)
        if 0 not in self.full:
            raise SystemExit("the capture has no full-state record for the joiner's commander")

        # The DirectPlay service id the joiner echoes in ADDFORWARDREQUEST is the one the host put in
        # the session description, so the live reply is read at the offset the recorded one was at.
        self.service_id, self.service_offset = self.rec_sys_id, None
        afr = next((m for t, p, f, m in ms if p == "TCP" and is_dp(m) and dp_command(m) == 0x13 and mine(t, p, f, m)), None)
        if afr is not None and len(afr) >= 32:
            self.service_id = struct.unpack_from("<I", afr, len(afr) - 4)[0]
            enum = next(m for t, p, f, m in ms if p == "TCP" and is_dp(m) and dp_command(m) == 0x01)
            at = enum.find(struct.pack("<I", self.service_id))
            if 0 <= at and at + 4 <= len(enum):
                self.service_offset = at


class Joiner:
    def __init__(self, tpl, args):
        self.tpl, self.a = tpl, args
        self.max_units = args.max_units
        self.commander = dict(tpl.full[0], pos=list(tpl.full[0]["pos"]), rot_yzx=list(tpl.full[0]["rot_yzx"]))
        self.health, self.dead, self.walk = self.commander["health"], False, None
        self.attack = args.attack
        self.walk_to = args.walk_to
        self.host_unit_pos = {}
        self.host_team = 0

        self.sys_id = self.player_id = self.host_player = 0
        self.host_ip = args.host
        self.out, self.conns = None, {}
        self.state = "enum"
        self.started = time.monotonic()
        self.last = dict(enum=0.0, beat=0.0, ping=0.0, sync=0.0, sync_id=0.0, attack=0.0, progress=0.0)
        self.game_since = self.ticks_sent = 0
        self.loading_since = 0.0
        self.progress_step = 0
        self.pending = []
        self.sync_out = []
        self.marker = 0xFFFFFEAD
        self.readied = self.hit = False
        self.shots = 0

        self.seen = dict(enum=False, ids=0, superenum=False, status=0, team=0, records=0, echoed=[],
                         loading=False, start=False, launch=set(), host_ticks=0, host_units={}, host_full={},
                         bad_cksum=0, pings=0, answered=0, replies=0, damage=0, host_damage=0)
        self.checks = []

    def log(self, *a):
        print(f"{time.strftime('%H:%M:%S')} " + " ".join(str(x) for x in a), flush=True)

    def check(self, name, ok, detail):
        self.checks.append((name, bool(ok), detail))

    # ---- sending

    def send_tcp(self, m, what=None):
        if self.out is None:
            self.out = socket.create_connection((self.host_ip, self.a.host_tcp), timeout=5)
            self.log(f"outbound TCP to the host's {self.a.host_tcp}")
        try:
            self.out.sendall(m)
        except OSError as e:
            self.log("send to the host failed:", e, "- waiting for it to open another session")
            self.out.close()
            self.out = None
            return
        if what:
            self.log("->", what)

    def app_tcp(self, to, subs, what=None):
        self.send_tcp(dp_app_tcp(self.a.tcp_port, self.player_id, to, ta_packet(subs)), what)

    def app_udp(self, subs, to=0, what=None):
        self.udp.sendto(dp_app_udp(self.player_id, to, ta_packet(subs, struct.pack("<I", self.marker))),
                        (self.host_ip, self.a.host_udp))
        self.marker = (self.marker - 1) & 0xFFFFFFFF
        if what:
            self.log("->", what)

    def my_status(self, state):
        return status(self.tpl.status_battleroom, self.player_id, name=self.a.name, state=state)

    # ---- the session handshake

    def on_dp(self, m, src_ip):
        cmd = dp_command(m)
        self.log("<-", CMDS.get(cmd, hex(cmd)))
        if cmd == 0x01 and self.state == "enum":                  # ENUMSESSIONSREPLY
            self.seen["enum"] = True
            if self.tpl.service_offset is not None and self.tpl.service_offset + 4 <= len(m):
                self.tpl.service_id = struct.unpack_from("<I", m, self.tpl.service_offset)[0]
            self.log(f"ENUMSESSIONSREPLY from {src_ip}, service id {self.tpl.service_id:#x}")
            self.state = "handshake"
            self.send_tcp(dp_request_player_id(self.a.tcp_port, True), "REQUESTPLAYERID (system player)")
        elif cmd == 0x07:                                         # REQUESTPLAYERREPLY
            pid = struct.unpack_from("<I", m, 28)[0]
            self.seen["ids"] += 1
            if self.seen["ids"] == 1:
                self.sys_id = pid
                self.send_tcp(dp_add_forward_request(self.a.tcp_port, self.sys_id, self.a.tcp_port,
                                                    self.a.udp_port, self.tpl.service_id),
                              f"ADDFORWARDREQUEST on {self.a.tcp_port}/{self.a.udp_port}, system id {pid:#x}")
            else:
                self.player_id = pid
                self.state = "battleroom"
                self.log(f"game player id {pid:#x}; our commander is unit {self.tpl.cmd_unit} in slot 0")
                self.send_tcp(dp_create_player(self.a.tcp_port, self.player_id, self.sys_id, self.a.tcp_port,
                                              self.a.udp_port, self.a.name), f"CREATEPLAYER '{self.a.name}'")
                self.sync_out = unit_sync_join(self.tpl.units)
        elif cmd == 0x29:                                         # SUPERENUMPLAYERSREPLY
            if not self.seen["superenum"]:
                self.seen["superenum"] = True
                self.send_tcp(dp_request_player_id(self.a.tcp_port, False), "REQUESTPLAYERID (game player)")

    # ---- the host's TA traffic

    def on_sync(self, subs):
        """Unit sync: the host's records are 1 + 2n of them, and each one advances our counter."""
        counted = 0
        for s in subs:
            if s[0] != 0x1A or len(s) < 14:
                continue
            if s[1] in (0, 3):
                self.seen["records"] += 1
                counted += 1
                if s[1] == 3:
                    self.seen["echoed"].append((struct.unpack_from("<I", s, 6)[0], s[10:12]))
            elif s[1] == 2:
                self.log(f"<- the host sent a checksum for unit type {struct.unpack_from('<I', s, 6)[0]:#x}")
        if counted:
            first = self.seen["records"] - counted
            self.app_tcp(self.player_id, [sync_progress(n) for n in range(first + 1, first + counted + 1)])

    def on_app(self, m, proto):
        p = ta_payload(m, proto)
        if p is None:
            return
        ok, _, subs = decode(p)
        if not ok:
            self.seen["bad_cksum"] += 1
        if proto == "TCP":
            self.on_sync(subs)
        for s in subs:
            code = s[0]
            if proto == "UDP" and code in LAUNCH_RECORDS:
                self.seen["launch"].add(code)
            if code == 0x02 and len(s) == 13:
                a, b, who = struct.unpack_from("<III", s, 1)
                if b == 0 and who != self.player_id:
                    # Replayed pings carry another game's clock, so every request is answered live.
                    self.seen["pings"] += 1
                    if self.player_id:
                        self.app_udp([ping(a, int(time.monotonic() * 1000) & 0xFFFFFFFF, who)], who)
                        self.seen["answered"] += 1
                elif b:
                    self.seen["replies"] += 1
            elif code == 0x20 and len(s) >= 158:
                self.seen["status"] += 1
                if self.seen["status"] == 1:
                    name = s[1:33].decode("utf-16-le", "ignore").split("\0")[0]
                    self.log(f"<- status '{name}' id {struct.unpack_from('<I', s, 145)[0]:#x} "
                             f"state {s[156]:#04x} options {s[157]:#04x}")
            elif code == 0x24 and len(s) == 6:
                self.seen["team"] += 1
                self.host_team = s[5]
            elif code == 0x08 and len(s) == 1:
                self.seen["loading"] = True
                self.state = "loading"
                self.loading_since = time.monotonic()
                self.log("<- 0x08 loading started")
            elif code == 0x1E and len(s) == 2:
                self.seen["start"] = True
                self.log(f"<- 0x1e start {s[1]}")
                self.app_tcp(self.player_id, [bytes([0x15]), bytes([0x07]),
                                              bytes([0x1F]) + struct.pack("<I", self.player_id)], "0x15, 0x07, 0x1f")
            elif code == 0x09 and len(s) == 23:
                self.host_unit_pos[struct.unpack_from("<H", s, 3)[0]] = list(struct.unpack_from("<iii", s, 5))
                self.log(f"<- 0x09 the host built unit {struct.unpack_from('<H', s, 3)[0]}")
            elif code == 0x0B and len(s) == 9:
                self.seen["host_damage"] += 1
                self.take_damage(struct.unpack_from("<H", s, 1)[0], struct.unpack_from("<H", s, 5)[0])
            elif code == 0x2C and proto == "UDP":
                self.on_unit_state(s)

    def on_unit_state(self, s):
        tick, entries, fl = decode_2c(s, self.max_units)
        self.seen["host_ticks"] = max(self.seen["host_ticks"], tick)
        for e in entries:
            self.seen["host_units"].setdefault(e["slot"], e["type"])
        if fl:
            self.seen["host_full"][fl["slot"]] = (tick, fl)
        if not self.game_since:
            self.start_game()

    # ---- in game

    def start_game(self):
        self.game_since = time.monotonic()
        self.state = "game"
        self.log(f"in game: the host is at tick {self.seen['host_ticks']} and owns slots "
                 f"{sorted(self.seen['host_units'])}; ours is {self.tpl.cmd_unit} in slot 0")
        self.app_udp([bytes([0x15]), bytes([0x07]), bytes([0x2A, 0x64]),
                      status(self.tpl.status_game, self.player_id, name=self.a.name, state=STATE_IN_GAME),
                      team(self.player_id, self.a.team),
                      commander_build(self.tpl.cmd_type, self.tpl.cmd_unit, self.tpl.cmd_pos),
                      unit_state(self.tpl.cmd_unit, self.tpl.state[3])],
                     what="0x15, 0x07, 0x2a 0x64, status, team, 0x09 commander, 0x11")

    def step_commander(self):
        c = self.commander
        if self.dead:
            return
        if self.walk_to and self.walk is None:
            tx, tz = self.walk_to
            self.walk = (int(tx * 65536), int(tz * 65536))
            # Heading is atan2(dx, dz) + half a turn, in 65536ths, fitted to the recorded commander.
            c["rot_yzx"][0] = int(math.atan2(tx - c["pos"][0] / 65536, tz - c["pos"][2] / 65536)
                                  / (2 * math.pi) * 65536 + 32768) & 0xFFFF
            c["speed"] = COMMANDER_SPEED
            self.pending.append({"slot": 0, "type": c["type"],
                                 "waypoints": [(c["pos"][0] >> 16, c["pos"][2] >> 16),
                                               (self.walk[0] >> 16, self.walk[1] >> 16)]})
            self.log(f"walking its commander to ({tx:.0f},{tz:.0f})")
        if not self.walk:
            return
        x, z = c["pos"][0] / 65536, c["pos"][2] / 65536
        tx, tz = self.walk[0] / 65536, self.walk[1] / 65536
        d = math.hypot(tx - x, tz - z)
        if d <= COMMANDER_SPEED / 65536:
            c["pos"][0], c["pos"][2] = self.walk
            c["speed"] = 0
            self.walk = None
            self.pending.append({"slot": 0, "type": c["type"], "waypoints": []})
            self.log("its commander has arrived")
        else:
            k = COMMANDER_SPEED / 65536 / d
            c["pos"][0] += int((tx - x) * k * 65536)
            c["pos"][2] += int((tz - z) * k * 65536)

    def take_damage(self, victim, amount):
        """Incoming 0x0b against our own commander: the owner's record says what is left of it."""
        if victim != self.tpl.cmd_unit or self.dead:
            return
        self.seen["damage"] += amount
        self.health = max(0, self.health - amount)
        self.commander["health"] = self.health
        if self.health:
            return
        self.dead = True
        self.app_udp([death(self.tpl.cmd_unit, self.host_player, 0, 1, 1, 1)])
        self.log(f"0x0c: its commander died, {self.seen['damage']} damage against it")

    def fire(self):
        c = self.commander
        aim = self.host_unit_pos.get(self.attack) or [c["pos"][0] + 65536, c["pos"][1], c["pos"][2] + 65536]
        self.shots += 1
        self.app_udp([shot(c["pos"], aim, c["rot_yzx"], self.attack, self.tpl.cmd_unit, 0),
                      damage(self.attack, self.tpl.cmd_unit, self.a.attack_damage)])
        if self.shots == 1 or self.shots % 40 == 0:
            self.log(f"0x0d and 0x0b: shot {self.shots}, {self.a.attack_damage} damage at the host's "
                     f"unit {self.attack}")

    def send_ticks(self, due):
        subs = []
        for _ in range(min(TICKS_PER_PACKET, due - self.ticks_sent)):
            self.ticks_sent += 1
            self.step_commander()
            entries, self.pending = self.pending, []
            # Every slot's full state, every cycle: a record for an empty slot deletes the unit the
            # receiver has there, so the commander's slot gets its state and every other slot an empty one.
            slot = self.ticks_sent % self.max_units
            subs.append(encode_2c(self.ticks_sent, entries, None if self.dead or slot != 0 else self.commander))
        self.app_udp(subs)

    def pump(self, now):
        if self.state == "enum" and now - self.last["enum"] > 1.0:
            self.last["enum"] = now
            self.udp.sendto(dp_enum_sessions(self.a.tcp_port), (self.host_ip, self.a.enum_port))
        if not self.player_id:
            return
        if self.sync_out and now - self.last["sync_id"] > 0.02:
            self.last["sync_id"] = now
            batch, self.sync_out = self.sync_out[:SYNC_BATCH], self.sync_out[SYNC_BATCH:]
            what = f"unit sync: {len(self.sync_out) + len(batch)} of {len(self.tpl.units) + 1} records left to send"
            if not self.sync_out:
                what = f"unit sync: sent {len(self.tpl.units)} ids and their CRCs"
            self.app_tcp(self.player_id, batch, what)
        if now - self.last["beat"] > HEARTBEAT:
            self.last["beat"] = now
            if self.state == "battleroom":
                ready = self.seen["records"] >= 1 + 2 * len(self.tpl.units)
                self.app_tcp(self.player_id, [self.my_status(READY if ready else 0),
                                              team(self.player_id, self.a.team), bytes([0x07])])
                if ready and not self.readied:
                    self.readied = True
                    self.log(f"ready: unit sync reached {self.seen['records']} of "
                             f"{1 + 2 * len(self.tpl.units)} records")
            elif self.state == "loading":
                self.app_tcp(self.player_id, [self.my_status(0), bytes([0x07])])
        if now - self.last["ping"] > PING_EVERY:
            self.last["ping"] = now
            self.app_udp([ping(int(now * 1000) & 0xFFFFFFFF, 0, self.player_id)])
        if self.state == "loading" and now - self.last["progress"] > LOADING_EVERY \
                and self.progress_step < len(PROGRESS):
            self.last["progress"] = now
            self.app_tcp(self.player_id, [bytes([0x2A, PROGRESS[self.progress_step]]), bytes([0x07])])
            self.progress_step += 1
        if self.state == "battleroom" and now - self.last["sync"] > 0.5:
            self.last["sync"] = now
            self.app_tcp(self.player_id, [sync_progress(self.seen["records"])])
        if self.state == "loading" and now - self.loading_since > 2.0:
            self.start_game()            # a host that launches without a first 0x2c still gets a game
        if self.state != "game":
            return
        due = int((now - self.game_since) * TICK_RATE)
        if due - self.ticks_sent >= TICKS_PER_PACKET:
            self.send_ticks(due)
        if self.a.simulate_hit and not self.hit:
            self.hit = True
            self.take_damage(self.tpl.cmd_unit, self.a.simulate_hit)
        if self.attack is not None and now - self.last["attack"] > 0.5:
            self.last["attack"] = now
            self.fire()

    # ---- the loop

    def run(self):
        self.tcp = socket.socket()
        self.tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.tcp.bind(("0.0.0.0", self.a.tcp_port))
        self.tcp.listen(8)
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.udp.bind(("0.0.0.0", self.a.udp_port))
        self.log(f"fake joiner up: '{self.a.name}' on {self.a.tcp_port}/{self.a.udp_port}, joining "
                 f"{self.host_ip}:{self.a.host_tcp}/{self.a.host_udp}, {len(self.tpl.units)} unit ids from "
                 f"{self.a.capture}, commander {self.tpl.cmd_unit} (type {self.tpl.full[0]['type']})")
        deadline, play_until = self.started + self.a.timeout, None
        while True:
            now = time.monotonic()
            if self.state == "game":
                play_until = play_until or now + self.a.play_seconds
                if now >= play_until:
                    break
            elif now > deadline:
                self.log(f"gave up after {self.a.timeout:.0f}s in state '{self.state}'")
                break
            watch = [self.tcp, self.udp] + list(self.conns) + ([self.out] if self.out else [])
            r, _, _ = select.select(watch, [], [], 0.005)
            for s in r:
                if s is self.tcp:
                    c, _ = s.accept()
                    self.conns[c] = bytearray()
                elif s is self.udp:
                    m, a = s.recvfrom(65536)
                    if is_dp(m):
                        self.on_dp(m, a[0])
                    elif len(m) > 8:
                        self.learn_player(struct.unpack_from("<I", m)[0])
                        self.on_app(m, "UDP")
                elif s is self.out:
                    s.recv(65536)          # the host never answers down its own connection
                else:
                    try:
                        d = s.recv(65536)
                    except OSError:
                        d = b""
                    if not d:
                        del self.conns[s]
                        continue
                    buf = bytes(self.conns[s]) + d
                    msgs, self.conns[s] = split_stream(buf)
                    for m in msgs:
                        if is_dp(m):
                            self.on_dp(m, "host")
                        elif len(m) > 28:
                            self.learn_player(struct.unpack_from("<I", m, 20)[0])
                            self.on_app(m, "TCP")
            self.pump(now)
        self.leave()
        return self.finish()

    def learn_player(self, pid):
        if not self.host_player:
            self.host_player = pid
            self.log(f"the host's player id is {pid:#x}")

    # ---- the verdict

    def leave(self):
        """A player leaves by naming both of its ids; the host is free to offer the session again."""
        if self.player_id:
            for pid in (self.player_id, self.sys_id):
                self.send_tcp(dp_delete_player(self.a.tcp_port, pid), f"DELETEPLAYER {pid:#x}")
            self.log(f"left the game after {self.ticks_sent} ticks")

    def finish(self):
        tpl, seen = self.tpl, self.seen
        n = len(tpl.units)
        self.check("handshake", seen["enum"] and seen["ids"] >= 2 and seen["superenum"] and self.state != "enum",
                   f"session {'found' if seen['enum'] else 'not found'}, {seen['ids']} player ids, player list "
                   f"{'received' if seen['superenum'] else 'missing'}")
        self.check("status", seen["status"] > 0,
                   f"the host sent {seen['status']} status records and {seen['team']} team records, "
                   f"its team is {self.host_team}")
        ids = {uid for uid, _ in seen["echoed"]}
        self.check("unit sync echo", len(seen["echoed"]) == 2 * n and ids == {u for u, _ in tpl.units},
                   f"{len(seen['echoed'])} of {2 * n} echo records covering {len(ids)} of {n} ids, "
                   f"our counter reached {seen['records']} of {1 + 2 * n}")
        missing = [hex(c) for c in LAUNCH_RECORDS if c not in seen["launch"]]
        self.check("launch", seen["loading"] and seen["start"] and not missing,
                   f"0x08 {'seen' if seen['loading'] else 'missing'}, 0x1e {'seen' if seen['start'] else 'missing'}, "
                   f"over UDP {sorted(hex(c) for c in LAUNCH_RECORDS if c in seen['launch'])}"
                   f"{' missing ' + ' '.join(missing) if missing else ''}")
        cycles = self.ticks_sent // self.max_units
        self.check("our own 0x2c", self.ticks_sent > 0,
                   f"{self.ticks_sent} ticks sent six to a packet, the commander's slot described "
                   f"{cycles} time{'s' if cycles != 1 else ''}")
        ok, detail = self.coverage()
        self.check("host full-state coverage", ok, detail)
        self.check("pings answered", seen["pings"] > 0 and seen["answered"] == seen["pings"],
                   f"{seen['answered']} of the host's {seen['pings']} ping requests answered live, "
                   f"{seen['replies']} of ours answered")
        should_die = seen["damage"] > 0 and self.health == 0
        self.check("damage", should_die == self.dead,
                   f"{seen['host_damage']} 0x0b records from the host, {seen['damage']} damage against its "
                   f"commander, {'a 0x0c sent' if self.dead else 'no 0x0c sent'}")
        self.check("host checksums", seen["bad_cksum"] == 0, f"{seen['bad_cksum']} of the host's packets failed")
        print()
        for name, ok, detail in self.checks:
            print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")
        return all(ok for _, ok, _ in self.checks)

    def coverage(self):
        """Every slot the host owns has to be described, non-empty and of the right type, once a cycle."""
        owned, full = self.seen["host_units"], self.seen["host_full"]
        missing = sorted(s for s in owned if s not in full)
        wrong = sorted(s for s, t in owned.items() if s in full and full[s][1]["type"] != t)
        empty = sorted(s for s, t in owned.items() if s in full and not full[s][1]["type"])
        ok = not (missing or wrong or empty)
        detail = (f"the host's {len(owned)} unit slots, {len(owned) - len(missing) - len(wrong)} described "
                  f"correctly, over {self.seen['host_ticks']} of its ticks")
        for label, slots in (("never described", missing), ("wrong type", wrong), ("described as empty", empty)):
            if slots:
                detail += f"; {label}: {slots}"
        return ok, detail


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture", help="a capture of a real TA joining a real host, for its unit ids and CRCs")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--host-tcp", type=int, default=HOST_TCP)
    ap.add_argument("--host-udp", type=int, default=HOST_UDP)
    ap.add_argument("--enum-port", type=int, default=ENUM_PORT)
    ap.add_argument("--tcp-port", type=int, default=2301)
    ap.add_argument("--udp-port", type=int, default=2351)
    ap.add_argument("--name", default="rwe", help="the player name; the field it goes in is four characters wide")
    ap.add_argument("--team", type=int, default=5)
    ap.add_argument("--play-seconds", type=float, default=30.0, help="how long to play after the launch")
    ap.add_argument("--timeout", type=float, default=120.0, help="give up if the game has not started by then")
    ap.add_argument("--walk", help="X,Z world position for its commander to walk to")
    ap.add_argument("--attack", type=int, help="a host unit id to fire at, every half second")
    ap.add_argument("--attack-damage", type=int, default=60)
    ap.add_argument("--simulate-hit", type=int, help="take this much damage on its own commander, to send a 0x0c")
    ap.add_argument("--max-units", type=int, default=250, help="the game's unit limit, which sets the full-state cycle")
    a = ap.parse_args()
    if len(a.name) > 4:
        print(f"note: the player name field is four characters wide, so '{a.name}' will be truncated")
    a.walk_to = parse_xy(a.walk) if a.walk else None
    sys.exit(0 if Joiner(Template(a.capture, a.max_units), a).run() else 1)


def parse_xy(text):
    try:
        x, z = (float(v) for v in text.split(","))
    except ValueError:
        raise SystemExit(f"--walk wants X,Z as two numbers, not '{text}'") from None
    return x, z


if __name__ == "__main__":
    main()
