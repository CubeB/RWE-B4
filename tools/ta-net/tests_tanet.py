#!/usr/bin/env python3
"""Tests for the TA network codec, stdlib only.

    python3 tools/ta-net/tests_tanet.py

No capture is needed, and none is checked in: the two handshake messages below are transcribed from
a real TA joining a real host, with the player's name replaced and every address left as the zeros
the recording had, because DirectPlay's reply address is "the source of this packet". The rest is
synthetic. The fakejoin cases are the host's own rules -- what its checks call a pass and a fail --
which is where a regression in a check would otherwise be invisible.
"""
import struct
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import fakejoin  # noqa: E402
import tanet  # noqa: E402

NAME_FIELD = "rwe\0".encode("utf-16-le") + b"\0"        # nine bytes: the recording's four characters

REC_SYS_ID, REC_PLAYER_ID = 0x09069207, 0x09069206


class DirectPlayJoinMessages(unittest.TestCase):
    """The three messages a joining client sends, against the recording they came from."""

    def test_enum_sessions(self):
        self.assertEqual(tanet.dp_enum_sessions(2301).hex(), (
            "3400b0fa020008fd000000000000000000000000706c617902000e00"
            "20747999f5f5cf11982700a0241496c80000000081000000"))

    def test_request_player_id(self):
        for system, flags in ((True, 9), (False, 8)):
            m = tanet.dp_request_player_id(2301, system)
            self.assertEqual(len(m), 32)
            self.assertEqual(struct.unpack_from("<I", m, 28)[0], flags)

    def test_add_forward_request(self):
        # The recorded TA joiner's message, verbatim: only the ids, the ports and the name vary.
        self.assertEqual(tanet.dp_add_forward_request(2301, REC_SYS_ID, 2301, 2351, 0x09079205).hex(),
                         "8600b0fa020008fd000000000000000000000000706c617913000e000000000007920609000000001c0000006c000000500000000900000007920609000000000000000020000000000000000000000007920609300000000e00000000000000020008fd0000000000000000000000000200092f000000000000000000000000000005920709")

    def test_create_player(self):
        got = tanet.dp_create_player(2301, REC_PLAYER_ID, REC_SYS_ID, 2301, 2351, "rwe")
        self.assertEqual(len(got), 175)
        self.assertEqual(got[:28].hex(), "af00b0fa020008fd000000000000000000000000706c617908000e00")
        self.assertEqual(struct.unpack_from("<I", got, 28 + 4)[0], REC_PLAYER_ID)
        self.assertEqual(struct.unpack_from("<I", got, 28 + 52)[0], REC_SYS_ID)
        self.assertEqual(got[28 + 68:28 + 77], NAME_FIELD)
        self.assertEqual(got[28 + 77:28 + 86], NAME_FIELD)
        self.assertEqual(struct.unpack_from(">H", got, 28 + 88)[0], 2301)
        self.assertEqual(struct.unpack_from(">H", got, 28 + 102)[0], 2351)

    def test_a_name_longer_than_the_field_is_cut(self):
        got = tanet.dp_create_player(2301, REC_PLAYER_ID, REC_SYS_ID, 2301, 2351, "rwe-ta-join")
        self.assertEqual(len(got), 175)
        self.assertEqual(got[28 + 68:28 + 77], "rwe-".encode("utf-16-le") + b"\0")

    def test_reply_port(self):
        self.assertEqual(tanet.reply_port(tanet.dp_request_player_id(34701, True)), 34701)


class Framing(unittest.TestCase):
    def test_a_ta_packet_round_trips(self):
        for subs in ([], [b"\x07"], [b"\x09" + b"\0" * 22], [b"\x2a\x00"] * 4,
                     [tanet.encode_2c(7, [{"slot": 1, "type": 34, "waypoints": [(1, 2)]}])]):
            pkt = tanet.ta_packet(subs)
            out, ok = tanet.decrypt(pkt)
            self.assertTrue(ok)
            self.assertEqual(out[7:], b"".join(subs))
            self.assertEqual(tanet.split_subpackets(out[7:]), subs)
            self.assertEqual(tanet.encrypt(out), pkt)

    def test_a_payload_too_short_to_carry_a_checksum_is_left_alone(self):
        for n in (0, 1, 2, 3):
            self.assertEqual(tanet.encrypt(b"\0" * n), b"\0" * n)

    def test_a_corrupt_byte_fails_the_checksum(self):
        m = bytearray(tanet.ta_packet([b"\x09" + b"\0" * 22]))
        m[10] ^= 0xFF
        out, ok = tanet.decrypt(bytes(m))
        self.assertFalse(ok)
        self.assertEqual(out[:3], b"\x03\x3b\x05")

    def test_split_stream_keeps_a_partial_message(self):
        m = tanet.dp_header(0x07, 8, 2300) + b"\0" * 8
        msgs, rest = tanet.split_stream(m[:20])
        self.assertEqual((msgs, rest), ([], m[:20]))
        msgs, rest = tanet.split_stream(m + m)
        self.assertEqual((msgs, rest), ([m, m], b""))

    def test_a_truncated_0x2c_stops_where_its_bits_run_out(self):
        whole = tanet.encode_2c(1200, [{"slot": 3, "type": 34, "waypoints": [(10, 20), (30, 40)]}],
                                {"type": 34, "health": 3000, "build": 0, "flags": 1, "motion": 1,
                                 "pos": [1 << 16, 0, 2 << 16], "rot_yzx": [100, 0, 0], "speed": 78643})
        self.assertEqual(tanet.encode_2c(*tanet.decode_2c(whole, 250)), whole)
        for cut in range(3, len(whole)):
            tick, entries, full = tanet.decode_2c(whole[:cut], 250)      # must terminate
            if cut >= 7:
                self.assertEqual(tick, 1200)

    def test_an_unsized_subpacket_is_kept_whole(self):
        subs = tanet.split_subpackets(b"\x2c\x0b\x00" + b"\0" * 8 + b"\x99\x99")
        self.assertEqual(subs[-1], b"\x99\x99")


class Records(unittest.TestCase):
    """Every record the joiner builds has to be the length the subpacket table says it is."""

    def lengths(self, *records):
        for r in records:
            self.assertEqual(tanet.subpacket_size(r), len(r), f"0x{r[0]:02x} is {len(r)} bytes")
            self.assertEqual(tanet.SUBPACKET_SIZES.get(r[0]), len(r), f"0x{r[0]:02x} length in the table")

    def test_the_small_records(self):
        self.lengths(tanet.sync_types(278), tanet.sync_unit(0xDEADBEEF, 0x12345678),
                     tanet.sync_echo(0xDEADBEEF, False), tanet.sync_echo(0xDEADBEEF, True),
                     tanet.sync_progress(557), tanet.team(REC_PLAYER_ID, 5),
                     tanet.commander_build(34, 251, [1 << 20, 0, 2 << 20]), tanet.unit_state(251, 1),
                     tanet.shot([0, 0, 0], [1 << 16, 0, 0], [100, 0, 0], 3, 251, 0),
                     tanet.damage(3, 251, 60), tanet.death(251, REC_PLAYER_ID, 0, 1, 1, 1))

    def test_the_status_record_keeps_its_length(self):
        template = bytes(186)
        for name, state in ((None, None), ("a name", 0), ("rwe", 0x20)):
            s = tanet.status(template, REC_PLAYER_ID, name=name, state=state, options=0x48)
            self.assertEqual(len(s), 186)
            self.assertEqual(struct.unpack_from("<I", s, 145)[0], REC_PLAYER_ID)
            if state is not None:
                self.assertEqual(s[156], state)
            self.assertEqual(s[157], 0x48)
        self.assertEqual(tanet.status(bytes(186), 1, name="a name")[1:33], "a name".encode("utf-16-le") + b"\0" * 20)

    def test_unit_sync_join_leads_with_the_count(self):
        units = [(i, i * 3) for i in range(10)]
        subs = tanet.unit_sync_join(units, batch=4)
        self.assertEqual(struct.unpack_from("<I", subs[0], 10)[0], 10)
        self.assertEqual([struct.unpack_from("<II", s, 6) for s in subs[1:]], units)

    def test_the_echo_statuses_are_the_two_the_host_sends(self):
        self.assertEqual(tanet.sync_echo(1, False)[10:14], b"\x01\x00\xff\xff")
        self.assertEqual(tanet.sync_echo(1, True)[10:14], b"\x01\x01\xff\xff")


class HostChecks(unittest.TestCase):
    """The full-state coverage check, which is what catches a host that eradicates its own units."""

    def setUp(self):
        class Args:
            walk = attack = simulate_hit = walk_to = None
            max_units, name, team, attack_damage, host = 250, "rwe", 5, 60, "127.0.0.1"
        tpl = type("T", (), {"full": {0: {"type": 34, "health": 3000, "build": 0, "flags": 1, "motion": 1,
                                        "pos": [0, 0, 0], "rot_yzx": [0, 0, 0], "speed": 0}},
                            "cmd_unit": 251, "units": [(1, 2)] * 278})()
        self.j = fakejoin.Joiner(tpl, Args())
        self.j.seen["host_ticks"] = 300

    def coverage(self, owned, full):
        self.j.seen["host_units"] = dict(owned)
        self.j.seen["host_full"] = {s: (250, {"slot": s, "type": t}) for s, t in full.items()}
        return self.j.coverage()[0]

    def test_a_host_that_describes_its_own_slots_passes(self):
        self.assertTrue(self.coverage({0: 34}, {0: 34}))
        self.assertTrue(self.coverage({0: 34, 5: 78}, {0: 34, 5: 78}))

    def test_a_host_that_describes_nothing_fails(self):
        self.assertFalse(self.coverage({0: 34}, {}))
        self.assertFalse(self.coverage({0: 34, 5: 78}, {0: 34}))

    def test_a_host_that_describes_the_wrong_type_fails(self):
        self.assertFalse(self.coverage({0: 34}, {0: 78}))

    def test_a_slot_described_as_empty_eradicates_the_unit_and_fails(self):
        self.assertFalse(self.coverage({0: 34}, {0: 0}))

    def test_a_host_with_no_units_at_all_passes_vacuously(self):
        self.assertTrue(self.coverage({}, {}))


if __name__ == "__main__":
    unittest.main()
