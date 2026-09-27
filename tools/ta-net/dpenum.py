#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Ask whatever is hosting a DirectPlay game for its sessions, and print the replies.

Sends a DirectPlay 4 EnumSessions request by UDP to port 47624 (loopback, the given address and
broadcast) and by TCP to 47624 and 2300, then listens for ENUMSESSIONSREPLY on a TCP port of its
own. A TA host answers only if something owns 47624; under Proton that takes native DirectPlay
(docs/TA-NETWORK.md, "Test bed").

    tools/ta-net/dpenum.py [my-lan-ip]
"""
import select, socket, struct, sys, time

from tanet import CMDS, dp_command, dp_header, is_dp

REPLY_PORT = 2399
ENUM_PORT = 47624


def main():
    my_ip = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    enum = bytes(16) + struct.pack("<IIH", 32, 0x10, 0)      # a null application GUID asks for every session
    pkt = bytearray(dp_header(0x0002, len(enum), REPLY_PORT) + enum)
    pkt[8:12] = socket.inet_aton(my_ip)

    lst = socket.socket(); lst.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    lst.bind(("0.0.0.0", REPLY_PORT)); lst.listen(4)
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); u.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    for target in (("127.0.0.1", ENUM_PORT), (my_ip, ENUM_PORT), ("255.255.255.255", ENUM_PORT)):
        u.sendto(pkt, target); print("udp enum ->", target)
    for port in (ENUM_PORT, 2300):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=1); s.sendall(pkt); s.close()
            print("tcp enum ->", port)
        except OSError as e:
            print("tcp", port, e)

    socks, deadline, got = [lst], time.time() + 4, 0
    while time.time() < deadline:
        r, _, _ = select.select(socks, [], [], 0.2)
        for s in r:
            if s is lst:
                c, a = lst.accept(); socks.append(c); continue
            data = s.recv(65536)
            if not data:
                socks.remove(s); continue
            got += 1
            if is_dp(data) and dp_command(data) == 0x01:
                app_guid = data[52:68]
                max_p, cur_p = struct.unpack_from("<II", data, 68)
                name = data[112:].decode("utf-16-le", "replace").rstrip("\0")
                print(f"{CMDS[0x01]} from {s.getpeername()[0]}: '{name}', {cur_p}/{max_p} players, app guid {app_guid.hex()}")
            else:
                print(len(data), "bytes:", data.hex(" "))
    if not got:
        print("no reply: nothing is hosting, or nothing owns port 47624")


if __name__ == "__main__":
    main()
