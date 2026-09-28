"""Total Annihilation's network traffic: DirectPlay framing, TA's packet transforms, and 0x2c unit state.

Shared by the tools in this directory. docs/TA-NETWORK.md is the reference for everything here;
docs/TA-DEMOS.md for the subpacket table and the 0x2c bit layout, which a demo shares with the wire.
"""
import struct
from collections import defaultdict

# ---------------------------------------------------------------------------------------------
# DirectPlay 4

DP_TOKEN = 0xFAB00000
HEADER = 20                   # size+token u32, then a sockaddr_in: family u16, port u16 (big-endian), ip u32, 8 pad
PLAY_HEADER = 28              # HEADER, "play", command u16, dialect u16

CMDS = {
    0x01: "ENUMSESSIONSREPLY", 0x02: "ENUMSESSIONS", 0x05: "REQUESTPLAYERID", 0x07: "REQUESTPLAYERREPLY",
    0x08: "CREATEPLAYER", 0x0B: "DELETEPLAYER", 0x13: "ADDFORWARDREQUEST", 0x16: "PING", 0x17: "PINGREPLY",
    0x1A: "SESSIONDESCCHANGED", 0x29: "SUPERENUMPLAYERSREPLY", 0x2E: "ADDFORWARD", 0x2F: "ADDFORWARDACK",
}


def is_dp(m):
    """A DirectPlay system message, as opposed to application data carrying a TA packet."""
    return len(m) >= 24 and m[20:24] == b"play"


def dp_command(m):
    return struct.unpack_from("<H", m, 24)[0]


def dp_header(cmd, payload_len, reply_port):
    # A zero reply IP means "the source address of this packet".
    return (struct.pack("<I", DP_TOKEN | (PLAY_HEADER + payload_len)) + struct.pack("<H", 2) + struct.pack(">H", reply_port)
            + b"\0" * 12 + b"play" + struct.pack("<HH", cmd, 0x000E))


def dp_app_tcp(reply_port, from_id, to_id, ta_pkt):
    """Application data over TCP: the 20-byte header, from and to player ids, then the TA packet."""
    body = struct.pack("<II", from_id, to_id) + ta_pkt
    return struct.pack("<I", DP_TOKEN | (HEADER + len(body))) + struct.pack("<H", 2) + struct.pack(">H", reply_port) + b"\0" * 12 + body


def dp_app_udp(from_id, to_id, ta_pkt):
    """Application data over UDP has no DirectPlay header at all."""
    return struct.pack("<II", from_id, to_id) + ta_pkt


def ta_payload(m, proto):
    """The TA packet inside an application message, or None for a system message."""
    if is_dp(m):
        return None
    off = 28 if proto == "TCP" else 8
    return m[off:] if len(m) > off else None


def split_stream(buf):
    """Cut a reassembled TCP stream into DirectPlay messages by their size field. Returns (messages, rest)."""
    out = []
    while len(buf) >= 4:
        n = struct.unpack_from("<I", buf)[0] & 0xFFFFF
        if n < 4 or n > len(buf):
            break
        out.append(buf[:n]); buf = buf[n:]
    return out, buf


def read_capture(path):
    """Every DirectPlay message in a pcap as (seconds from start, "TCP"/"UDP", "sport->dport", bytes), in time order."""
    from scapy.all import IP, TCP, UDP, Raw, rdpcap
    pk = rdpcap(path)
    t0 = pk[0].time
    streams = defaultdict(list)
    out = []
    for p in pk:
        if IP not in p or Raw not in p:
            continue
        t = float(p.time - t0)
        if TCP in p:
            streams[(p[TCP].sport, p[TCP].dport)].append((p[TCP].seq, t, bytes(p[Raw].load)))
        elif UDP in p:
            out.append((t, "UDP", f"{p[UDP].sport}->{p[UDP].dport}", bytes(p[Raw].load)))
    for (sport, dport), segs in streams.items():
        segs.sort(); buf = b""; seen = set(); ends = []
        for seq, t, d in segs:
            if seq in seen:
                continue
            seen.add(seq); buf += d; ends.append((len(buf), t))
        start = 0
        for m in split_stream(buf)[0]:
            t = next(tt for end, tt in ends if end > start)
            out.append((t, "TCP", f"{sport}->{dport}", m)); start += len(m)
    return sorted(out, key=lambda x: x[0])


# ---------------------------------------------------------------------------------------------
# TA packets: type, checksum u16, a u32 marker, then subpackets; encrypted, optionally compressed

def decrypt(d):
    d = bytearray(d)
    if len(d) < 4:
        return d, True
    stored = d[1] | d[2] << 8; check = 0; key = 3
    for i in range(3, len(d) - 3):              # the last three bytes are never encrypted
        check = (check + d[i]) & 0xFFFF; d[i] ^= key & 0xFF; key += 1
    return d, stored == check


def encrypt(d):
    d = bytearray(d)
    if len(d) < 4:
        return bytes(d)             # too short to carry a checksum, and decrypt agrees
    check = 0; key = 3
    for i in range(3, len(d) - 3):
        d[i] ^= key & 0xFF; check = (check + d[i]) & 0xFFFF; key += 1
    d[1], d[2] = check & 0xFF, check >> 8
    return bytes(d)


def compress(d, header_size=3):
    """The inverse of `decompress`, transcribed from rwe/io/tad/tad_util.cpp's `tadCompress`.

    Not byte-for-byte TA's own packer: the two agree on what a packet says and not always on how
    it is packed, so this is for re-packing a packet whose plain form has been edited (a fixture
    with a name taken out of it), never for reproducing a capture. `ta-capture.py --check` does
    the other half, re-encoding `0x2c` against the capture itself.
    """
    if len(d) <= header_size:
        return bytes(d)
    out = bytearray(); count = 7; control = 0; i = 4; match = 0; n = len(d)
    while i < n + 1:
        if count == 7:
            count = 0; out.append(0); control = len(out) - 1
        else:
            count += 1
        if i < 6 or i > 2000:                       # the reference never looks for a match here
            out.append(d[i - 1]); i += 1; continue
        best = 2
        for a in range(4, i - 1):
            c = 0
            while a + c < i and i + c < n and d[a + c - 1] == d[i + c - 1]:
                c += 1
            if c > best:
                best = c; match = a
                if best > 17:
                    break
        run = 0                                     # a run of the byte just written, which the
        while i + run < n and d[i + run - 1] == d[i - 2]:   # search above cannot express
            run += 1
        if run > best:
            best = run; match = i - 1
        if best > 2:
            out[control] |= 1 << count
            length = (best - 2) & 0x0F
            op = ((match - header_size) << 4) | length
            out.append(op & 0xFF); out.append((op >> 8) & 0xFF)
            i += length + 2
        else:
            out.append(d[i - 1]); i += 1
    if count == 7:
        out.append(0xFF)
    else:
        out[control] |= (0xFF << (count + 1)) & 0xFF
    out.append(0); out.append(0)                    # a zero offset in the next slot ends the stream
    if len(out) + header_size < n:
        return bytes([0x04, d[1], d[2]]) + bytes(out)
    return bytes([0x03]) + bytes(d[1:])


def decompress(d, header_size=3):
    # The offset is absolute into the output, not a distance back, and a copy may overrun its own source.
    if not d or d[0] != 0x04:
        return bytes(d)
    out = bytearray(d[:header_size]); out[0] = 0x03; i = header_size
    while i < len(d):
        ctl = d[i]; i += 1
        for slot in range(8):
            if i >= len(d):
                return bytes(out)
            if not (ctl >> slot) & 1:
                out.append(d[i]); i += 1; continue
            op = d[i] | d[i + 1] << 8; i += 2
            a = op >> 4
            if a == 0:
                return bytes(out)
            for b in range(a + header_size, a + header_size + (op & 0xF) + 2):
                out.append(out[b - 1] if b <= len(out) else 0)
    return bytes(out)


SUBPACKET_SIZES = {
    0x02: 13, 0x03: 7, 0x05: 65, 0x06: 1, 0x07: 1, 0x08: 1, 0x09: 23, 0x0A: 7, 0x0B: 9, 0x0C: 11, 0x0D: 36, 0x0E: 14,
    0x0F: 6, 0x10: 22, 0x11: 4, 0x12: 5, 0x14: 24, 0x15: 1, 0x16: 17, 0x17: 2, 0x18: 2, 0x19: 3, 0x1A: 14, 0x1B: 6,
    0x1E: 2, 0x1F: 5, 0x20: 186, 0x21: 10, 0x22: 6, 0x23: 14, 0x24: 6, 0x26: 41, 0x28: 58, 0x29: 3, 0x2A: 2, 0x2E: 9,
    0xF6: 1, 0xF9: 73, 0xFA: 1, 0xFC: 5, 0xFE: 5, 0xFF: 1,
}


def subpacket_size(s):
    """Length of the subpacket starting at s, or 0 if unknown."""
    if s[0] == 0x2C:
        return s[1] | s[2] << 8 if len(s) >= 3 else 0
    if s[0] == 0xFB:
        return s[1] + 3 if len(s) >= 2 else 0
    return SUBPACKET_SIZES.get(s[0], 0)


def split_subpackets(p):
    subs = []; i = 0
    while i < len(p):
        if p[i] == 0:
            i += 1; continue
        n = subpacket_size(p[i:])
        if n == 0 or i + n > len(p):
            subs.append(bytes(p[i:])); break      # unsized: kept whole so a desynchronised walk is visible
        subs.append(bytes(p[i:i + n])); i += n
    return subs


def decode(pkt):
    """(checksum ok, 7-byte header, subpackets) of one TA packet as it came off the wire."""
    d, ok = decrypt(pkt)
    d = decompress(d)
    return ok, bytes(d[:7]), split_subpackets(d[7:])


def ta_packet(subs, marker=b"\xff\xff\xff\xff"):
    """An uncompressed, encrypted TA packet. Replies carry marker ffffffff; TA's own requests count down from it."""
    return encrypt(bytes([0x03, 0, 0]) + bytes(marker) + b"".join(subs))


def subpackets_of(m, proto):
    p = ta_payload(m, proto)
    return decode(p)[2] if p else []


def ping(a, b, requester):
    """0x02: the requester's tick, the responder's tick (0 in a request), the requester's player id."""
    return struct.pack("<BIII", 0x02, a, b, requester)


# ---------------------------------------------------------------------------------------------
# A joining client: the DirectPlay messages it sends, and the TA records it owns.
# The player-info blob in ADDFORWARDREQUEST and CREATEPLAYER is transcribed from the recorded TA
# joiner in the three spike captures; the ids, the two ports and the name are the only fields that
# differ between two joins, and the name is the same length in both, so nothing here says whether
# DirectPlay would accept a longer one.

TA_APP_GUID = bytes.fromhex("20747999f5f5cf11982700a0241496c8")   # {99797420-F5F5-11CF-9827-00A0241496C8}
ENUM_TAIL = b"\0\0\0\0\x81\0\0\0"       # the last two words of TA's EnumSessions request
PLAYER_NAME = 9                         # a NUL-terminated utf-16le name, four characters wide


def reply_port(m):
    """The port a received message says its sender wants its replies on."""
    return struct.unpack_from(">H", m, 6)[0]


def _sockaddr(port):
    # family 2, a big-endian port, a zero IP: the sender's reply address, meaning this packet's source.
    return struct.pack("<H", 2) + struct.pack(">H", port) + b"\0" * 10


def _name(name):
    b = name.encode("utf-16-le")[:PLAYER_NAME - 1]
    return b + b"\0" * (PLAYER_NAME - len(b))


def _u32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def _u32s(*values):
    return b"".join(_u32(v) for v in values)


def dp_enum_sessions(where):
    """The UDP broadcast a joiner sends to 47624; the header carries the port it listens on."""
    body = TA_APP_GUID + ENUM_TAIL
    return dp_header(0x02, len(body), where) + body


def dp_request_player_id(where, system):
    """Flags 9 asks the host for a system player, 8 for a game player."""
    return dp_header(0x05, 4, where) + struct.pack("<I", 9 if system else 8)


def dp_add_forward_request(where, sys_id, tcp_port, udp_port, service_id):
    """ADDFORWARDREQUEST: its own player info, carrying the joiner's two listening addresses."""
    body = (_u32s(0, sys_id, 0, 0x1C, 0x6C, 0x50, 9, sys_id, 0, 0, 0x20, 0, 0, sys_id, 0x30, 0x0E, 0)
            + _sockaddr(tcp_port) + b"\0\0" + _sockaddr(udp_port) + b"\0" * 4 + _u32(service_id))
    return dp_header(0x13, len(body), where) + body


def dp_create_player(where, player_id, sys_id, tcp_port, udp_port, name):
    """CREATEPLAYER: the same player info, named this time."""
    body = (_u32s(0, player_id, 0, 0x1C, 0, 0x79, 8, player_id, 0x0A, 0x0A, 0x20, 0x15, 0, sys_id, 0x30, 0x0E, 0)
            + _name(name) * 2 + _sockaddr(tcp_port) + _sockaddr(udp_port) + b"\0" * 28 + b"\x50" + b"\0" * 4)
    return dp_header(0x08, len(body), where) + body


def dp_delete_player(where, player_id):
    """DELETEPLAYER: a player leaves by naming each of its two ids, the game one and the system one."""
    return dp_header(0x0B, 20, where) + _u32s(0, player_id, 0, 0, 0)


def status(template, player_id, name=None, state=None, options=None):
    """0x20, the 186-byte player info, patched from a recorded one: the id is at 145, the state
    byte at 156 (gpgnet4ta reads bit 0x20 as ready) and the host's options byte at 157."""
    s = bytearray(template)
    struct.pack_into("<I", s, 145, player_id)
    if name is not None:
        s[1:33] = (name.encode("utf-16-le")[:30] + b"\0" * 32)[:32]
    if state is not None:
        s[156] = state
    if options is not None:
        s[157] = options
    return bytes(s)


def team(player_id, team_id):
    """0x24: whose team, and which. An untouched battleroom sends 5, no team."""
    return bytes([0x24]) + struct.pack("<IB", player_id, team_id)


def sync_types(count):
    """0x1a sub-type 1: how many unit types this side has (278 in the GOG install)."""
    return bytes([0x1A, 0x01]) + b"\0" * 8 + _u32(count)


def sync_unit(uid, crc):
    """0x1a sub-type 2: one unit type's id and its content-derived checksum. Only a joiner sends these."""
    return bytes([0x1A, 0x02]) + b"\0" * 4 + _u32(uid) + _u32(crc)


def sync_echo(uid, in_use):
    """0x1a sub-type 3: one id, with status 0x0001 or 0x0101 (in use) and the limit."""
    return bytes([0x1A, 0x03]) + b"\0" * 4 + _u32(uid) + (b"\x01\x01" if in_use else b"\x01\x00") + b"\xff\xff"


def sync_progress(count):
    """0x1a sub-type 4: how many of the host's 1 + 2n records have been processed."""
    return bytes([0x1A, 0x04]) + b"\0" * 8 + _u32(count)


def unit_sync_join(units, batch=35):
    """The joiner's side of unit sync: the count, then an id and its CRC for each type."""
    out = [sync_types(len(units))]
    for i in range(0, len(units), batch):
        out += [sync_unit(uid, crc) for uid, crc in units[i:i + batch]]
    return out


def commander_build(type_index, unit, pos):
    """0x09: a unit appeared. The type is a 1-based load-order index, not an index into the 0x1a table."""
    return (bytes([0x09]) + struct.pack("<HH", type_index, unit)
            + b"".join(struct.pack("<i", c) for c in pos) + b"\0" * 6)


def unit_state(unit, value):
    """0x11: a unit's state word -- a u16 unit id and one byte whose meaning is not decoded."""
    return bytes([0x11]) + struct.pack("<HB", unit, value)


def shot(origin, aim, rot, target, shooter, slot):
    """0x0d: 0x0d shot fired. slot is the 0-based index of the shooter's Weapon1/2/3."""
    return (bytes([0x0D]) + b"".join(struct.pack("<i", c) for c in origin)
            + b"".join(struct.pack("<i", c) for c in aim) + b"".join(struct.pack("<H", c & 0xFFFF) for c in rot)
            + struct.pack("<HHB", target, shooter, slot))


def damage(victim, attacker, amount):
    """0x0b: damage. The trailing word is not remaining health and nothing reads it yet (TA-DEMOS)."""
    return bytes([0x0B]) + struct.pack("<HHHH", victim, attacker, amount, 0)


def death(unit, killer_player, killer, severity, cause, level):
    """0x0c: the only record that kills a unit, and only its owner may send one."""
    return bytes([0x0C]) + struct.pack("<HIHBB", unit, killer_player, killer, severity, cause << 4 | level)


# ---------------------------------------------------------------------------------------------
# 0x2c unit state: an LSB-first bit stream (TA-DEMOS.md, "0x2c, unit state")

TYPE_BITS = 9                 # bit length of the data set's unit type count: 278 types in the GOG install


class _Bits:
    def __init__(self, b, pos=56):
        self.v = int.from_bytes(b, "little"); self.pos = pos

    def take(self, k):
        x = (self.v >> self.pos) & ((1 << k) - 1); self.pos += k
        return x


class _BitWriter:
    def __init__(self):
        self.v = 0; self.pos = 0

    def put(self, x, k):
        self.v |= (x & ((1 << k) - 1)) << self.pos; self.pos += k

    def bytes(self):
        return self.v.to_bytes((self.pos + 7) // 8, "little")


def _signed(x, k):
    return x - (1 << k) if x >= 1 << (k - 1) else x


def decode_2c(s, max_units, type_bits=TYPE_BITS):
    """(tick, waypoint entries, full-state record or None) of a ground-unit 0x2c.

    Entries are {slot, type, blocked, waypoints [(x, z)]}. The full-state record is for block slot
    tick % max_units; None means that slot is empty. Positions are 16.16, rotations are y, z, x in
    65536ths of a turn, speed is 16.16 per tick and present only for a unit with a mover.
    Aircraft entries and attached units are not handled. A truncated record stops where its bits run
    out rather than reading past them, which is every 0x2c of a length the sender chose; one too
    short to hold a tick decodes to nothing at all.
    """
    if len(s) < 7:
        return 0, [], None
    tick = struct.unpack_from("<I", s, 3)[0]
    b = _Bits(s)
    entries = []
    while b.pos + 16 <= len(s) * 8:
        slot = b.take(16)
        if slot == 0xFFFF:
            break
        e = {"slot": slot, "type": b.take(type_bits), "blocked": b.take(1)}
        count = b.take(2)
        e["waypoints"] = [(_signed(b.take(16), 16), _signed(b.take(16), 16)) for _ in range(count)]
        entries.append(e)
    full = None
    if b.take(1):
        typ = b.take(type_bits)
        if typ:
            full = {"slot": tick % max_units, "type": typ, "health": b.take(16), "build": b.take(8),
                    "flags": b.take(8), "motion": b.take(2)}
            if b.take(1):
                full["attached"] = (b.take(15), b.take(8))
            else:
                full["pos"] = [_signed(b.take(32), 32) for _ in range(3)]
                full["rot_yzx"] = [b.take(16) for _ in range(3)]
                full["speed"] = _signed(b.take(32), 32) if len(s) * 8 - b.pos >= 32 else None
    return tick, entries, full


def encode_2c(tick, entries=(), full=None, type_bits=TYPE_BITS):
    """The inverse of decode_2c. full=None writes an empty slot, which deletes any unit the receiver has there."""
    w = _BitWriter()
    w.put(0x2C, 8); w.put(0, 16); w.put(tick, 32)
    for e in entries:
        w.put(e["slot"], 16); w.put(e["type"], type_bits); w.put(e.get("blocked", 0), 1); w.put(len(e["waypoints"]), 2)
        for x, z in e["waypoints"]:
            w.put(x, 16); w.put(z, 16)
    w.put(0xFFFF, 16); w.put(1, 1)
    if full is None:
        w.put(0, type_bits)
    else:
        w.put(full["type"], type_bits); w.put(full["health"], 16); w.put(full["build"], 8); w.put(full["flags"], 8)
        w.put(full["motion"], 2); w.put(0, 1)
        for c in full["pos"]:
            w.put(c, 32)
        for r in full["rot_yzx"]:
            w.put(r, 16)
        if full.get("speed") is not None:
            w.put(full["speed"], 32)
    out = bytearray(w.bytes())
    out[1:3] = len(out).to_bytes(2, "little")
    return bytes(out)
